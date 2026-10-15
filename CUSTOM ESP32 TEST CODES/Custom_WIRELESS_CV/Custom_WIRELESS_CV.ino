#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "ESP32Port.h"

extern "C" {
  #include "ad5940.h"
  #include "RampTest.h"
}

/* ================= WI-FI SETTINGS ================= */
const char* ssid = "iPhone";
const char* password = "0164029650";

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

/* ================= HTML DASHBOARD CODE ================= */
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>AD5940 Live CV Dashboard</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
  <style>
    body { font-family: Arial; text-align: center; padding: 20px; background-color: #f4f4f9;}
    canvas { max-width: 800px; margin: auto; background: white; box-shadow: 0px 0px 10px rgba(0,0,0,0.1); }
    h2 { color: #333; }
  </style>
</head>
<body>
  <h2>Live Cyclic Voltammetry</h2>
  <canvas id="cvChart"></canvas>
  <script>
    const ctx = document.getElementById('cvChart').getContext('2d');
    const cvChart = new Chart(ctx, {
      type: 'scatter',
      data: {
        datasets: [{
          label: 'Live CV Scan',
          data: [],
          showLine: true,
          borderColor: '#28a745',
          backgroundColor: '#28a745',
          borderWidth: 2,
          pointRadius: 1
        }]
      },
      options: {
        animation: false, 
        scales: {
          x: { title: { display: true, text: 'Voltage (mV)' } },
          y: { title: { display: true, text: 'Current (\u00B5A)' } }
        }
      }
    });

    var gateway = `ws://${window.location.hostname}/ws`;
    var websocket = new WebSocket(gateway);
    
    websocket.onmessage = function(event) {
      // Incoming format: Cycle,Step,Voltage,Current|
      let points = event.data.split('|');
      points.forEach(point => {
        if (point.length > 0) {
          let values = point.split(',');
          let voltage = parseFloat(values[2]); // Index 2 is Voltage
          let current = parseFloat(values[3]); // Index 3 is Current
          cvChart.data.datasets[0].data.push({x: voltage, y: current});
        }
      });
      cvChart.update();
    };
  </script>
</body>
</html>
)rawliteral";

/* ================= USER BUFFER ================= */
#define APPBUFF_SIZE 1000
uint32_t AppBuff[APPBUFF_SIZE];
float LFOSCFreq;

/* ================= CONTINUOUS CV TRACKERS ================= */
int maxCycles = 2;          // <--- SET YOUR DESIRED NUMBER OF CYCLES HERE!
int currentCycle = 1;
uint32_t currentStep = 0;

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

/* ================= CYCLIC VOLTAMMETRY PARAMETERS ================= */
void AD5940RampStructInit(void)
{
  AppRAMPCfg_Type *pRAMPCfg;
  AppRAMPGetCfg(&pRAMPCfg);

  pRAMPCfg->LFOSCClkFreq = LFOSCFreq;
  pRAMPCfg->SysClkFreq = 16000000.0f;
  pRAMPCfg->AdcClkFreq = 16000000.0f;
  pRAMPCfg->SeqStartAddr = 0;
  pRAMPCfg->MaxSeqLen = 512;
  
  pRAMPCfg->RampStartVolt = -300.0f;     
  pRAMPCfg->RampPeakVolt = +300.0f;      
  pRAMPCfg->StepNumber = 500;            
  pRAMPCfg->RampDuration = 10 * 1000;    
  pRAMPCfg->bRampOneDir = bFALSE;        
  
  pRAMPCfg->VzeroStart = 1100.0f;        
  pRAMPCfg->VzeroPeak = 1100.0f;          
  pRAMPCfg->SampleDelay = 1.0f;          
  pRAMPCfg->LPTIARtiaSel = LPTIARTIA_4K; 
  pRAMPCfg->AdcPgaGain = ADCPGA_1;
  pRAMPCfg->ADCSinc3Osr = ADCSINC3OSR_4;
  pRAMPCfg->FifoThresh = 4;
}

/* ================= VOLTAGE CALCULATOR ================= */
float calculateVoltage(uint32_t stepIndex, float startV, float peakV, uint32_t totalSteps) {
  uint32_t halfSteps = totalSteps / 2;
  
  if (stepIndex <= halfSteps) {
    return startV + ((peakV - startV) * stepIndex) / halfSteps;
  } else {
    return peakV - ((peakV - startV) * (stepIndex - halfSteps)) / halfSteps;
  }
}

/* ================= ARDUINO SETUP ================= */
void setup()
{
  Serial.begin(115200);
  while (!Serial);
  delay(200);

  // --- CONNECT TO WI-FI ---
  Serial.println("\n==================================");
  Serial.println("  AD5940 Wireless CV Server");
  Serial.println("==================================");
  Serial.print(">> Connecting to Wi-Fi: ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\n✅ Wi-Fi Connected!");
  Serial.print("🌐 Open Browser and go to: http://");
  Serial.println(WiFi.localIP());

  // --- START WEB SERVER ---
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });
  server.addHandler(&ws);
  server.begin();

  // --- INIT AD5940 ---
  AD5940_MCUResourceInit(0);
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

  Serial.println("Cycle,Step,Voltage(mV),Current(uA)"); 
  AppRAMPCtrl(APPCTRL_START, 0);
}

/* ================= ARDUINO LOOP ================= */
void loop()
{
  // Keep the WebSocket alive
  ws.cleanupClients();

  if (AD5940_GetMCUIntFlag() || digitalRead(AD5940_INTR_PIN) == LOW)
  {
    AD5940_ClrMCUIntFlag();

    uint32_t count = APPBUFF_SIZE;
    AppRAMPISR(AppBuff, &count);

    float *pCurrentuA = (float*)AppBuff;
    AppRAMPCfg_Type *pCfg;
    AppRAMPGetCfg(&pCfg);

    // --- BATCH BUFFER ---
    char dataBatch[1024] = ""; 
    char singlePoint[64];

    for (uint32_t i = 0; i < count; i++)
    {
      float vBias = calculateVoltage(currentStep, pCfg->RampStartVolt, pCfg->RampPeakVolt, pCfg->StepNumber);
      
      // Print to Serial
      Serial.print(currentCycle); Serial.print(",");
      Serial.print(currentStep); Serial.print(",");
      Serial.print(vBias, 2); Serial.print(",");
      Serial.println(pCurrentuA[i], 4);

      // Add to WebSocket Batch (Format: Cycle,Step,Voltage,Current|)
      snprintf(singlePoint, sizeof(singlePoint), "%d,%d,%.2f,%.4f|", currentCycle, currentStep, vBias, pCurrentuA[i]);
      strcat(dataBatch, singlePoint);

      currentStep++;
    }

    // Blast the batch to the webpage
    if (strlen(dataBatch) > 0) {
      ws.textAll(dataBatch);
    }

    // --- FINITE RESTART LOGIC ---
    if (pCfg->bTestFinished == bTRUE) 
    {
      if (currentCycle < maxCycles) 
      {
        currentCycle++;
        currentStep = 0;
        pCfg->bTestFinished = bFALSE; 
        
        AD5940_SEQCtrlS(bFALSE);             
        AD5940_WriteReg(REG_AFE_SEQCNT, 0);  
        AD5940_SEQCtrlS(bTRUE);              
        AppRAMPCtrl(APPCTRL_START, 0);
      }
      else
      {
        Serial.println("\n=== SWEEP COMPLETE ===");
        while(1); 
      }
    }
  }  
}