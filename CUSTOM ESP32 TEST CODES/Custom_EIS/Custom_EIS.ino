#include <Arduino.h>
#include <SPI.h>
#include "ESP32Port.h"

extern "C" {
  #include "ad5940.h"
  #include "Impedance.h"
}

/* ================= USER BUFFER ================= */
#define APPBUFF_SIZE 512
uint32_t AppBuff[APPBUFF_SIZE];
float LFOSCFreq;

/* ================= EIS SWEEP VARIABLES ================= */
int totalSweepPoints = 1001;  // Set to 101 to match your SweepPoints parameter
int pointsCollected = 0;
bool testActive = false;

/* ================= PLATFORM CONFIG ================= */
static int32_t AD5940PlatformCfg(void)
{
  CLKCfg_Type clk_cfg;
  FIFOCfg_Type fifo_cfg;
  AGPIOCfg_Type gpio_cfg;
  LFOSCMeasure_Type LfoscMeasure;

  /* Use hardware reset */
  AD5940_HWReset();
  AD5940_Initialize();

  /* Step1. Configure clock */
  clk_cfg.ADCClkDiv = ADCCLKDIV_1;
  clk_cfg.ADCCLkSrc = ADCCLKSRC_HFOSC;
  clk_cfg.SysClkDiv = SYSCLKDIV_1;
  clk_cfg.SysClkSrc = SYSCLKSRC_HFOSC;
  clk_cfg.HfOSC32MHzMode = bFALSE;
  clk_cfg.HFOSCEn = bTRUE;
  clk_cfg.HFXTALEn = bFALSE;
  clk_cfg.LFOSCEn = bTRUE;
  AD5940_CLKCfg(&clk_cfg);

  /* Step2. Configure FIFO and Sequencer*/
  fifo_cfg.FIFOEn = bFALSE;
  fifo_cfg.FIFOMode = FIFOMODE_FIFO;
  fifo_cfg.FIFOSize = FIFOSIZE_4KB;                       
  fifo_cfg.FIFOSrc = FIFOSRC_DFT;
  fifo_cfg.FIFOThresh = 4;        
  AD5940_FIFOCfg(&fifo_cfg);
  
  fifo_cfg.FIFOEn = bTRUE;
  AD5940_FIFOCfg(&fifo_cfg);
  
  /* Step3. Interrupt controller */
  AD5940_INTCCfg(AFEINTC_1, AFEINTSRC_ALLINT, bTRUE);   
  AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
  AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH | AFEINTSRC_ENDSEQ | AFEINTSRC_CUSTOMINT0, bTRUE); 
  AD5940_INTCClrFlag(AFEINTSRC_ALLINT);

  /* Step4: Reconfigure GPIO */
  gpio_cfg.FuncSet = GP0_INT | GP1_SLEEP | GP2_SYNC;
  gpio_cfg.InputEnSet = 0;
  gpio_cfg.OutputEnSet = AGPIO_Pin0 | AGPIO_Pin1 | AGPIO_Pin2;
  gpio_cfg.OutVal = 0;
  gpio_cfg.PullEnSet = 0;
  AD5940_AGPIOCfg(&gpio_cfg);

  AD5940_SleepKeyCtrlS(SLPKEY_UNLOCK);  

  /* Measure LFOSC for Wakeup Timer Accuracy */
  LfoscMeasure.CalDuration = 1000.0;
  LfoscMeasure.CalSeqAddr = 0;
  LfoscMeasure.SystemClkFreq = 16000000.0f;
  AD5940_LFOSCMeasure(&LfoscMeasure, &LFOSCFreq);

  return 0;
}

/* ================= EIS PARAMETERS (From your C Code) ================= */
void AD5940ImpedanceStructInit(void)
{
  AppIMPCfg_Type *pImpedanceCfg;
  AppIMPGetCfg(&pImpedanceCfg);

  /* System timings */
  pImpedanceCfg->WuptClkFreq = LFOSCFreq;
  pImpedanceCfg->SeqStartAddr = 0;
  pImpedanceCfg->MaxSeqLen = 512; 

  /* Hardware values */
  pImpedanceCfg->RcalVal = 10000.0;     // <-- The crucial Calibration Resistor!
  pImpedanceCfg->SinFreq = 60000.0;
  pImpedanceCfg->FifoThresh = 4;
    
  /* Set switch matrix to onboard(EVAL-AD5940ELECZ) dummy sensor. */
  pImpedanceCfg->DswitchSel = SWD_CE0;
  pImpedanceCfg->PswitchSel = SWP_RE0;
  pImpedanceCfg->NswitchSel = SWN_SE0;
  pImpedanceCfg->TswitchSel = SWT_SE0LOAD;
  
  /* The dummy sensor is as low as 5kOhm. We need to make sure RTIA is small enough that HSTIA won't be saturated. */
  pImpedanceCfg->HstiaRtiaSel = HSTIARTIA_5K;  
    
  /* Configure the sweep function. */
  pImpedanceCfg->SweepCfg.SweepEn = bTRUE;
  pImpedanceCfg->SweepCfg.SweepStart = 0.1f;    /* Start from 100Hz */
  pImpedanceCfg->SweepCfg.SweepStop = 100e3f;     /* Stop at 100kHz */
  pImpedanceCfg->SweepCfg.SweepPoints = totalSweepPoints;      /* Points is 101 */
  pImpedanceCfg->SweepCfg.SweepLog = bTRUE;
  
  /* Configure Power Mode & Filters */
  pImpedanceCfg->PwrMod = AFEPWR_HP;
  pImpedanceCfg->ADCSinc3Osr = ADCSINC3OSR_2;     /* Sample rate is 800kSPS/2 = 400kSPS */
  pImpedanceCfg->DftNum = DFTNUM_16384;
  pImpedanceCfg->DftSrc = DFTSRC_SINC3;
}

/* ================= ARDUINO SETUP ================= */
void setup()
{
  Serial.begin(115200);
  while (!Serial);
  delay(200);

  Serial.println("\nAD5940 EIS Measurement Test");

  AD5940_MCUResourceInit(0);

  // --- THE SANITY CHECK ---
  AD5940_HWReset(); 
  delay(10);
  uint16_t chipID = AD5940_ReadReg(REG_AFECON_CHIPID);
  
  if (chipID != 0x5502 && chipID != 0x5503) {
    Serial.println("❌ SPI FAILURE: Cannot see AD5940.");
    while(1); 
  }
  Serial.println("✅ SPI SUCCESS: AD5940 Connected!");
  
  AD5940PlatformCfg();
  AD5940ImpedanceStructInit();

  // --- SHOW RCAL VALUE ---
  AppIMPCfg_Type *pCfg;
  AppIMPGetCfg(&pCfg);
  Serial.println("==========================================");
  Serial.print("🛠️  Calibration Resistor (RcalVal): ");
  Serial.print(pCfg->RcalVal, 1);
  Serial.println(" Ohms");
  Serial.println("==========================================\n");

  if (AppIMPInit(AppBuff, APPBUFF_SIZE) != AD5940ERR_OK)
  {
    Serial.println("❌ Init Failed!");
    while (1);
  }

  // --- CSV HEADER ---
  Serial.println("Frequency(Hz),Magnitude(Ohm),Phase(Deg)"); 
  
  AppIMPCtrl(IMPCTRL_START, 0);
  testActive = true;
}

/* ================= ARDUINO LOOP ================= */
void loop()
{
  if (testActive) 
  {
    // Dual-Alarm Trap Prevention
    if (AD5940_GetMCUIntFlag() || digitalRead(AD5940_INTR_PIN) == LOW)
    {
      AD5940_ClrMCUIntFlag();

      uint32_t count = APPBUFF_SIZE;
      AppIMPISR(AppBuff, &count);

      // Cast raw buffer to the Impedance structs
      fImpPol_Type *pImp = (fImpPol_Type*)AppBuff;

      // Grab the exact frequency that this data point corresponds to
      float freq;
      AppIMPCtrl(IMPCTRL_GETFREQ, &freq);

      for (uint32_t i = 0; i < count; i++)
      {
        // Print: Frequency, Magnitude, Phase (converted to Degrees!)
        Serial.print(freq, 2); Serial.print(",");
        Serial.print(pImp[i].Magnitude, 4); Serial.print(",");
        Serial.println(pImp[i].Phase * 180.0 / PI, 4); 

        pointsCollected++;
      }
      
      // Stop the sweep when we hit our target number of points (101)
      if (pointsCollected >= totalSweepPoints)
      {
        AppIMPCtrl(IMPCTRL_STOPNOW, 0);
        testActive = false;

        Serial.println("\n=== EIS SWEEP COMPLETE ===");
        Serial.println("Copy the data above, save as a .csv file, and plot your Nyquist/Bode graph!");
      }
    }
  }  
}