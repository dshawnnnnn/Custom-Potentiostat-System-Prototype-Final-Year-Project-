
#include <Arduino.h>
#include <SPI.h>
#include "ESP32Port.h"

extern "C" {
  #include "ad5940.h"
  #include "Amperometric.h"
}

/* ================= USER BUFFER ================= */
#define APPBUFF_SIZE 1000
uint32_t AppBuff[APPBUFF_SIZE];
float LFOSCFreq;

/* ================= MULTI-STEP CA VARIABLES ================= */
// Set your desired voltage sequence here!
float voltageSteps[] = {0, 200.0, 400.0, 0}; 
int totalSteps = 4;

unsigned long stepDuration = 5000; // Time per step in milliseconds (5 seconds)

int currentStepIndex = 0;
unsigned long stepStartTime = 0;
unsigned long totalTestStartTime = 0;
uint32_t currentDataStep = 0;
bool testActive = false;

/* ================= DIRECT DAC VOLTAGE CONTROL ================= */
void AD5940_SetVoltage(float targetBias_mV, float vZero_mV) {
  float dac12_lsb = 2200.0f / 4095.0f;
  float dac6_lsb  = dac12_lsb * 64.0f;
  
  uint32_t vZeroCode = (uint32_t)((vZero_mV - 200.0f) / dac6_lsb);
  int32_t vBiasCode  = (int32_t)(targetBias_mV / dac12_lsb) + vZeroCode * 64;
  
  if (vBiasCode > vZeroCode * 64) {
    vBiasCode--;
  }
  
  // THE FIX: Safety Clamps to prevent 12-bit DAC overflow!
  if (vBiasCode > 4095) vBiasCode = 4095; 
  if (vZeroCode > 63)   vZeroCode = 63;
  
  AD5940_WriteReg(REG_AFE_LPDACDAT0, (vZeroCode << 12) | (uint32_t)vBiasCode);
}

/* ================= PLATFORM CONFIG ================= */
static int32_t AD5940PlatformCfg(void)
{
  CLKCfg_Type clk_cfg;
  FIFOCfg_Type fifo_cfg;
  SEQCfg_Type seq_cfg;
  AGPIOCfg_Type gpio_cfg;
  LFOSCMeasure_Type LfoscMeasure;

  AD5940_HWReset();
  AD5940_Initialize();

  clk_cfg.HFOSCEn = bTRUE;
  clk_cfg.HFXTALEn = bFALSE;
  clk_cfg.LFOSCEn = bTRUE;
  clk_cfg.HfOSC32MHzMode = bFALSE;
  clk_cfg.SysClkSrc = SYSCLKSRC_HFOSC;
  clk_cfg.SysClkDiv = SYSCLKDIV_1;
  clk_cfg.ADCCLkSrc = ADCCLKSRC_HFOSC;
  clk_cfg.ADCClkDiv = ADCCLKDIV_1;
  AD5940_CLKCfg(&clk_cfg);

  fifo_cfg.FIFOEn = bFALSE;
  fifo_cfg.FIFOMode = FIFOMODE_FIFO;
  fifo_cfg.FIFOSize = FIFOSIZE_4KB;
  fifo_cfg.FIFOSrc = FIFOSRC_DFT;
  fifo_cfg.FIFOThresh = 4;
  AD5940_FIFOCfg(&fifo_cfg);

  fifo_cfg.FIFOEn = bTRUE;
  AD5940_FIFOCfg(&fifo_cfg);

  seq_cfg.SeqMemSize = SEQMEMSIZE_2KB;
  seq_cfg.SeqBreakEn = bFALSE;
  seq_cfg.SeqIgnoreEn = bFALSE;
  seq_cfg.SeqCntCRCClr = bTRUE;
  seq_cfg.SeqEnable = bFALSE;
  seq_cfg.SeqWrTimer = 0;
  AD5940_SEQCfg(&seq_cfg);

  AD5940_INTCCfg(AFEINTC_1, AFEINTSRC_ALLINT, bTRUE);
  AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH | AFEINTSRC_ENDSEQ | AFEINTSRC_CUSTOMINT0, bTRUE);
  AD5940_INTCClrFlag(AFEINTSRC_ALLINT);

  gpio_cfg.FuncSet = GP1_SLEEP | GP0_INT;
  gpio_cfg.InputEnSet = 0;
  gpio_cfg.OutputEnSet = AGPIO_Pin0 | AGPIO_Pin1;
  gpio_cfg.OutVal = 0;
  gpio_cfg.PullEnSet = 0;
  AD5940_AGPIOCfg(&gpio_cfg);

  AD5940_SleepKeyCtrlS(SLPKEY_UNLOCK);

  LfoscMeasure.CalDuration = 1000.0;
  LfoscMeasure.CalSeqAddr = 0;
  LfoscMeasure.SystemClkFreq = 16000000.0f;
  AD5940_LFOSCMeasure(&LfoscMeasure, &LFOSCFreq);

  return 0;
}

/* ================= AMPEROMETRIC AS "CA" PARAMETERS ================= */
void AD5940AMPStructInit(void)
{
  AppAMPCfg_Type *pAMPCfg;
  AppAMPGetCfg(&pAMPCfg);

  pAMPCfg->WuptClkFreq = LFOSCFreq;
  pAMPCfg->SeqStartAddr = 0;
  pAMPCfg->MaxSeqLen = 512;
  pAMPCfg->RcalVal = 10000.0;
  pAMPCfg->NumOfData = -1;

  pAMPCfg->AmpODR = 0.01;         // 100 Hz Sample Rate (Smooth decay curve)
  pAMPCfg->FifoThresh = 4;

  // Set the hardware to boot up on the very first voltage in your array
  pAMPCfg->SensorBias = voltageSteps[0]; 
  pAMPCfg->Vzero = 1100;                
  
  pAMPCfg->LptiaRtiaSel = LPTIARTIA_24K; // Lower this if current overloads!
  pAMPCfg->LpTiaRl = LPTIARLOAD_SHORT;
  pAMPCfg->ADCRefVolt = 1.82;
}

/* ================= ARDUINO SETUP ================= */
void setup()
{
  Serial.begin(115200);
  while (!Serial);
  delay(200);

  Serial.println("\nAD5940 Multi-Step CA Test");

  AD5940_MCUResourceInit(0);

  AD5940_HWReset(); 
  delay(10);
  uint16_t chipID = AD5940_ReadReg(REG_AFECON_CHIPID);
  
  if (chipID != 0x5502 && chipID != 0x5503) {
    Serial.println("❌ SPI FAILURE: Cannot see AD5940.");
    while(1); 
  }
  
  AD5940PlatformCfg();
  AD5940AMPStructInit();

  if (AppAMPInit(AppBuff, APPBUFF_SIZE) != AD5940ERR_OK)
  {
    Serial.println("❌ Init Failed!");
    while (1);
  }

  // --- CSV HEADER (Added Voltage Column!) ---
  Serial.println("DataIndex,TotalTime(ms),Voltage(mV),Current(uA)"); 
  
  AppAMPCtrl(AMPCTRL_START, 0);
  
  // Start the timers!
  unsigned long now = millis();
  stepStartTime = now;
  totalTestStartTime = now;
  testActive = true;
}

/* ================= ARDUINO LOOP ================= */
void loop()
{
  if (testActive) 
  {
    unsigned long currentMillis = millis();

    // 1. Process Data
    if (AD5940_GetMCUIntFlag() || digitalRead(AD5940_INTR_PIN) == LOW)
    {
      AD5940_ClrMCUIntFlag();

      uint32_t count = APPBUFF_SIZE;
      AppAMPISR(AppBuff, &count);
      float *pCurrent = (float*)AppBuff;

      for (uint32_t i = 0; i < count; i++)
      {
        unsigned long elapsedTime = currentMillis - totalTestStartTime;

        // Print: Index, Total Time, Applied Voltage, Current
        Serial.print(currentDataStep); Serial.print(",");
        Serial.print(elapsedTime); Serial.print(",");
        Serial.print(voltageSteps[currentStepIndex], 1); Serial.print(",");
        Serial.println(pCurrent[i], 4);

        currentDataStep++;
      }
    }

    // 2. The Multi-Step Logic (Checks if 5 seconds have passed)
    if (currentMillis - stepStartTime >= stepDuration)
    {
      currentStepIndex++; // Move to the next voltage step

      if (currentStepIndex < totalSteps) 
      {
        // THE FIX: Make sure the vZero argument matches our 1100mV baseline!
        AD5940_SetVoltage(voltageSteps[currentStepIndex], 1100.0f);
        
        // Reset the stopwatch for this specific step
        stepStartTime = currentMillis; 
      }
      else 
      {
        // All steps in the array are finished!
        AppAMPCtrl(AMPCTRL_STOPNOW, 0);
        testActive = false;

        Serial.println("\n=== MULTI-STEP CA TEST COMPLETE ===");
        Serial.println("Copy the data above, save as a .csv file, and plot it in Excel!");
      }
    }
  }  
}