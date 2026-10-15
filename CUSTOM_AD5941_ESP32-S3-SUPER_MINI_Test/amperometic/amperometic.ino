

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

/* ================= RESULT PRINT ================= */
int32_t AMPShowResult(float *pData, uint32_t DataCount)
{
  for (uint32_t i = 0; i < DataCount; i++)
  {
    Serial.print("Index: ");
    Serial.print(i);
    Serial.print(", Current: ");
    Serial.print(pData[i], 6);
    Serial.println(" uA");
  }
  return 0;
}

/* ================= PLATFORM CONFIG ================= */
static int32_t AD5940PlatformCfg(void)
{
  CLKCfg_Type clk_cfg;
  FIFOCfg_Type fifo_cfg;
  SEQCfg_Type seq_cfg;
  AGPIOCfg_Type gpio_cfg;
  LFOSCMeasure_Type LfoscMeasure;

  /* Hardware reset */
  AD5940_HWReset();
  AD5940_Initialize();

  /* Clock config */
  clk_cfg.HFOSCEn = bTRUE;
  clk_cfg.HFXTALEn = bFALSE;
  clk_cfg.LFOSCEn = bTRUE;
  clk_cfg.HfOSC32MHzMode = bFALSE;
  clk_cfg.SysClkSrc = SYSCLKSRC_HFOSC;
  clk_cfg.SysClkDiv = SYSCLKDIV_1;
  clk_cfg.ADCCLkSrc = ADCCLKSRC_HFOSC;
  clk_cfg.ADCClkDiv = ADCCLKDIV_1;
  AD5940_CLKCfg(&clk_cfg);

  /* FIFO config */
  fifo_cfg.FIFOEn = bFALSE;
  fifo_cfg.FIFOMode = FIFOMODE_FIFO;
  fifo_cfg.FIFOSize = FIFOSIZE_4KB;
  fifo_cfg.FIFOSrc = FIFOSRC_DFT;
  fifo_cfg.FIFOThresh = 4;
  AD5940_FIFOCfg(&fifo_cfg);

  fifo_cfg.FIFOEn = bTRUE;
  AD5940_FIFOCfg(&fifo_cfg);

  /* Sequencer config */
  seq_cfg.SeqMemSize = SEQMEMSIZE_2KB;
  seq_cfg.SeqBreakEn = bFALSE;
  seq_cfg.SeqIgnoreEn = bFALSE;
  seq_cfg.SeqCntCRCClr = bTRUE;
  seq_cfg.SeqEnable = bFALSE;
  seq_cfg.SeqWrTimer = 0;
  AD5940_SEQCfg(&seq_cfg);

  /* Interrupts */
  AD5940_INTCCfg(AFEINTC_1, AFEINTSRC_ALLINT, bTRUE);
  AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH, bTRUE);
  AD5940_INTCClrFlag(AFEINTSRC_ALLINT);

  /* GPIO */
  gpio_cfg.FuncSet =
      GP6_SYNC | GP5_SYNC | GP4_SYNC |
      GP2_SYNC | GP1_SLEEP | GP0_INT;
  gpio_cfg.InputEnSet = 0;
  gpio_cfg.OutputEnSet =
      AGPIO_Pin0 | AGPIO_Pin1 | AGPIO_Pin2 |
      AGPIO_Pin4 | AGPIO_Pin5 | AGPIO_Pin6;
  gpio_cfg.OutVal = 0;
  gpio_cfg.PullEnSet = 0;
  AD5940_AGPIOCfg(&gpio_cfg);

  AD5940_SleepKeyCtrlS(SLPKEY_UNLOCK);

  /* Measure LFOSC */
  LfoscMeasure.CalDuration = 1000.0;
  LfoscMeasure.CalSeqAddr = 0;
  LfoscMeasure.SystemClkFreq = 16000000.0f;
  AD5940_LFOSCMeasure(&LfoscMeasure, &LFOSCFreq);

  Serial.print("LFOSC Frequency: ");
  Serial.println(LFOSCFreq);

  return 0;
}

/* ================= AMP PARAM INIT ================= */
void AD5940AMPStructInit(void)
{
  AppAMPCfg_Type *pAMPCfg;
  AppAMPGetCfg(&pAMPCfg);

  pAMPCfg->WuptClkFreq = LFOSCFreq;
  pAMPCfg->SeqStartAddr = 0;
  pAMPCfg->MaxSeqLen = 512;
  pAMPCfg->RcalVal = 10000.0;
  pAMPCfg->NumOfData = -1;

  pAMPCfg->AmpODR = 1.0;          // 1 Hz sampling
  pAMPCfg->FifoThresh = 4;

  pAMPCfg->SensorBias = 500;      // mV
  pAMPCfg->LptiaRtiaSel = LPTIARTIA_4K;
  pAMPCfg->LpTiaRl = LPTIARLOAD_SHORT;
  pAMPCfg->Vzero = 1100;          // mV

  pAMPCfg->ADCRefVolt = 1.82;
}

/* ================= ARDUINO SETUP ================= */
void setup()
{
  Serial.begin(115200);
  while (!Serial);
  delay(200);

  Serial.println("AD5940 Amperometric Test (ESP32)");

  /* MCU resource init (SPI, CS, RESET pins) */
  AD5940_MCUResourceInit(0);

  AD5940PlatformCfg();
  AD5940AMPStructInit();

  
  // 1. Initialize the App - This performs the RTIA calibration
  if (AppAMPInit(AppBuff, APPBUFF_SIZE) != AD5940ERR_OK)
  {
    Serial.println("AMP Init Failed!");
    while (1);
  }

  // 2. Access the configuration structure
  AppAMPCfg_Type *pCfg;
  AppAMPGetCfg(&pCfg);

  // 3. Print the results
  Serial.println("\n--- Calibration Results ---");
  Serial.print("Target RTIA Range: ");
  Serial.println("12 kOhm"); 
  
  Serial.print("Actual Calibrated RTIA: ");
  Serial.print(pCfg->RtiaCalValue.Magnitude); // Magnitude in Ohms
  Serial.println(" Ohms");

  Serial.print("Calibration Phase: ");
  Serial.print(pCfg->RtiaCalValue.Phase);
  Serial.println(" rad");
  Serial.println("---------------------------\n");

  AppAMPCtrl(AMPCTRL_START, 0);
  Serial.println("Amperometric measurement started.");
}

void loop()
{
  if (AD5940_GetMCUIntFlag())
  {
    AD5940_ClrMCUIntFlag();

    uint32_t count = APPBUFF_SIZE;
    AppAMPISR(AppBuff, &count);

    // Create a 'float' pointer to look at the same memory as AppBuff
    float *pResults = (float*)AppBuff;

    for (uint32_t i = 0; i < count; i++)
    {
      float current = pResults[i];
      Serial.print("I = ");
      Serial.print(current);
      Serial.println(" uA");
    }
  }  
}



