#include <Arduino.h>
#include <SPI.h>
#include "ESP32Port.h"

extern "C" {
  #include "ad5940.h"
  #include "RampTest.h"
}

/* ================= USER BUFFER ================= */
#define APPBUFF_SIZE 1000
uint32_t AppBuff[APPBUFF_SIZE];
float LFOSCFreq;

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
  AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH | AFEINTSRC_ENDSEQ | AFEINTSRC_CUSTOMINT0, bTRUE);
  AD5940_INTCClrFlag(AFEINTSRC_ALLINT);

  /* GPIO */
  gpio_cfg.FuncSet = GP1_SLEEP | GP0_INT;
  gpio_cfg.InputEnSet = 0;
  gpio_cfg.OutputEnSet = AGPIO_Pin0 | AGPIO_Pin1;
  gpio_cfg.OutVal = 0;
  gpio_cfg.PullEnSet = 0;
  AD5940_AGPIOCfg(&gpio_cfg);

  AD5940_SleepKeyCtrlS(SLPKEY_UNLOCK);

  /* Measure LFOSC */
  LfoscMeasure.CalDuration = 1000.0;
  LfoscMeasure.CalSeqAddr = 0;
  LfoscMeasure.SystemClkFreq = 16000000.0f;
  AD5940_LFOSCMeasure(&LfoscMeasure, &LFOSCFreq);

  return 0;
}

/* ================= CYCLIC VOLTAMMETRY PARAMETERS ================= */
void AD5940RampStructInit(void)
{
  AppRAMPCfg_Type *pRAMPCfg;
  AppRAMPGetCfg(&pRAMPCfg);

  /* System Clocks */
  pRAMPCfg->LFOSCClkFreq = LFOSCFreq;
  pRAMPCfg->SysClkFreq = 16000000.0f;
  pRAMPCfg->AdcClkFreq = 16000000.0f;
  pRAMPCfg->SeqStartAddr = 0;
  pRAMPCfg->MaxSeqLen = 512;
  
  /* CV Waveform Settings */
  pRAMPCfg->RampStartVolt = -400.0f;     // Start at -500mV
  pRAMPCfg->RampPeakVolt = +400.0f;      // Peak at +500mV
  pRAMPCfg->StepNumber = 160;            // 100 voltage steps
  pRAMPCfg->RampDuration = 16 * 1000;    // Total sweep takes 10 seconds (10,000ms)
  pRAMPCfg->bRampOneDir = bFALSE;        // bFALSE = Triangle Wave (CV). bTRUE = Linear (LSV).
  
  /* Internal AD5940 Hardware Setup */
  pRAMPCfg->VzeroStart = 1100.0f; //2200.0f;        // 2.2V reference
  pRAMPCfg->VzeroPeak = 1100.0f;  //400.0f;          // 0.4V reference
  pRAMPCfg->SampleDelay = 26.0f;          // Wait 1ms after changing voltage before sampling
  pRAMPCfg->LPTIARtiaSel = LPTIARTIA_12K; // Resistor size (Lower this if your current overloads!)
  pRAMPCfg->AdcPgaGain = ADCPGA_1;
  pRAMPCfg->ADCSinc3Osr = ADCSINC3OSR_4;
  pRAMPCfg->FifoThresh = 4;
}

// /* ================= ARDUINO SETUP ================= */
// void setup()
// {
//   Serial.begin(115200);
//   while (!Serial);
//   delay(200);

//   Serial.println("\nAD5940 Cyclic Voltammetry Test");

//   AD5940_MCUResourceInit(0);

//   // --- THE SANITY CHECK ---
//   AD5940_HWReset(); 
//   delay(10);
//   uint16_t chipID = AD5940_ReadReg(REG_AFECON_CHIPID);
  
//   if (chipID != 0x5502 && chipID != 0x5503) {
//     Serial.println("❌ SPI FAILURE: Cannot see AD5940.");
//     while(1); 
//   }
//   Serial.println("✅ SPI SUCCESS: AD5940 Connected!");
//   // ------------------------

//   AD5940PlatformCfg();
//   AD5940RampStructInit();

//   if (AppRAMPInit(AppBuff, APPBUFF_SIZE) != AD5940ERR_OK)
//   {
//     Serial.println("❌ Ramp Init Failed!");
//     while (1);
//   }

//   Serial.println("✅ Ramp Initialized. Starting sweep...");
//   AppRAMPCtrl(APPCTRL_START, 0);
// }

// /* ================= ARDUINO LOOP ================= */
// void loop()
// {
//   // When the AD5940 pulls the interrupt pin LOW, we collect data
//   if (AD5940_GetMCUIntFlag())
//   {
//     AD5940_ClrMCUIntFlag();

//     uint32_t count = APPBUFF_SIZE;
    
//     // The ISR automatically processes the bits into floats (uA)
//     AppRAMPISR(AppBuff, &count);

//     // Cast the raw buffer to a float pointer
//     float *pCurrentuA = (float*)AppBuff;

//     // Print the collected data points
//     for (uint32_t i = 0; i < count; i++)
//     {
//       Serial.print("Current: ");
//       // NOTE: The RampTest.c file ALREADY multiplies by 1e3 to output in microAmps!
//       Serial.print(pCurrentuA[i], 4); 
//       Serial.println(" uA");
//     }
//   }  
// }

/* ================= CONTINUOUS CV TRACKERS ================= */
int maxCycles = 2;          // <--- SET YOUR DESIRED NUMBER OF CYCLES HERE!
int currentCycle = 1;
uint32_t currentStep = 0;

/* ================= VOLTAGE CALCULATOR ================= */
// Calculates the exact applied voltage based on the current step
float calculateVoltage(uint32_t stepIndex, float startV, float peakV, uint32_t totalSteps) {
  uint32_t halfSteps = totalSteps / 2;
  
  if (stepIndex <= halfSteps) {
    // Sweeping UP
    return startV + ((peakV - startV) * stepIndex) / halfSteps;
  } else {
    // Sweeping DOWN
    return peakV - ((peakV - startV) * (stepIndex - halfSteps)) / halfSteps;
  }
}

/* ================= ARDUINO SETUP ================= */
void setup()
{
  Serial.begin(115200);
  while (!Serial);
  delay(200);

  // MCU resource init
  AD5940_MCUResourceInit(0);

  // --- THE SANITY CHECK ---
  AD5940_HWReset(); 
  delay(10);
  uint16_t chipID = AD5940_ReadReg(REG_AFECON_CHIPID);
  
  if (chipID != 0x5502 && chipID != 0x5503) {
    Serial.println("Error: Cannot see AD5940.");
    while(1); 
  }

  AD5940PlatformCfg();
  AD5940RampStructInit();

  if (AppRAMPInit(AppBuff, APPBUFF_SIZE) != AD5940ERR_OK)
  {
    Serial.println("Error: Ramp Init Failed!");
    while (1);
  }

  // --- PRINT CSV HEADER ---
  // This is the very first line of your output!
  Serial.println("Cycle,Step,Voltage(mV),Current(uA)"); 
  
  AppRAMPCtrl(APPCTRL_START, 0);
}

/* ================= ARDUINO LOOP ================= */
void loop()
{
  if (AD5940_GetMCUIntFlag() || digitalRead(AD5940_INTR_PIN) == LOW)
  {
    AD5940_ClrMCUIntFlag();

    uint32_t count = APPBUFF_SIZE;
    
    // The ISR automatically processes the bits into floats (uA)
    AppRAMPISR(AppBuff, &count);

    // Cast the raw buffer to a float pointer
    float *pCurrentuA = (float*)AppBuff;

    // Grab the active configuration to calculate the voltage mathematically
    AppRAMPCfg_Type *pCfg;
    AppRAMPGetCfg(&pCfg);

    // Print the collected data points in CSV format
    for (uint32_t i = 0; i < count; i++)
    {
      // Calculate the theoretical voltage for this step
      float vBias = calculateVoltage(currentStep, pCfg->RampStartVolt, pCfg->RampPeakVolt, pCfg->StepNumber);
      
      // Print row: Cycle, Step, Voltage, Current
      Serial.print(currentCycle); Serial.print(",");
      Serial.print(currentStep); Serial.print(",");
      Serial.print(vBias, 2); Serial.print(",");
      Serial.println(pCurrentuA[i], 4);

      currentStep++;
    }

    // --- FINITE RESTART LOGIC ---
    // Check if the AD5940 has finished the current cycle
    if (pCfg->bTestFinished == bTRUE) 
    {
      if (currentCycle < maxCycles) 
      {
        // Not done yet! Move to the next cycle
        currentCycle++;
        currentStep = 0;
        pCfg->bTestFinished = bFALSE; 
        
        // Wake up the sequencer and restart
        AD5940_SEQCtrlS(bFALSE);             
        AD5940_WriteReg(REG_AFE_SEQCNT, 0);  
        AD5940_SEQCtrlS(bTRUE);              
        AppRAMPCtrl(APPCTRL_START, 0);
      }
      else
      {
        // All cycles are complete!
        Serial.println("\n=== SWEEP COMPLETE ===");
        while(1); // Freeze the ESP32 here so it doesn't loop forever
      }
    }
  }  
}