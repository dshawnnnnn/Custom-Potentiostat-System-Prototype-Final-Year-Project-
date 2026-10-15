#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "ESP32Port.h"

extern "C" {
  #include "ad5940.h"
  #include "Amperometric.h"
}

/* ================= WI-FI SETTINGS ================= */
const char* ssid = "Deshawn";
const char* password = "12345678";

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

/* ================= HTML DASHBOARD CODE ================= */
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>Live Multi-Step CA</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
  <style>
    body { font-family: Arial; text-align: center; padding: 20px; background-color: #f4f4f9;}
    canvas { max-width: 800px; margin: auto; background: white; box-shadow: 0px 0px 10px rgba(0,0,0,0.1); }
    h2 { color: #333; }
  </style>
</head>
<body>
  <h2>Live Chronoamperometry (Current Decay)</h2>
  <canvas id="caChart"></canvas>
  <script>
    const ctx = document.getElementById('caChart').getContext('2d');
    const caChart = new Chart(ctx, {
      type: 'line', // Line chart is better for time-series decay curves
      data: {
        datasets: [{
          label: 'Current vs Time',
          data: [],
          borderColor: '#007bff',
          backgroundColor: 'rgba(0, 123, 255, 0.1)',
          borderWidth: 2,
          pointRadius: 0, // Hide dots to make the fast line look smoother
          fill: true
        }]
      },
      options: {
        animation: false, 
        scales: {
          x: { type: 'linear', title: { display: true, text: 'Time (ms)' } },
          y: { title: { display: true, text: 'Current (\u00B5A)' } }
        }
      }
    });

    var gateway = `ws://${window.location.hostname}/ws`;
    var websocket = new WebSocket(gateway);
    
    websocket.onmessage = function(event) {
      // Incoming format: Time(ms),Voltage,Current|
      let points = event.data.split('|');
      points.forEach(point => {
        if (point.length > 0) {
          let values = point.split(',');
          let time = parseFloat(values[0]);    // Index 0 is Time
          let current = parseFloat(values[2]); // Index 2 is Current
          caChart.data.datasets[0].data.push({x: time, y: current});
        }
      });
      caChart.update();
    };
  </script>
</body>
</html>
)rawliteral";

/* ================= USER BUFFER ================= */
#define APPBUFF_SIZE 1000
uint32_t AppBuff[APPBUFF_SIZE];
float LFOSCFreq;

/* ================= MULTI-STEP CA VARIABLES ================= */
float voltageSteps[] = {100.0, 250.0, 500.0, 150.0}; 
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

/* ================= AMPEROMETRIC PARAMETERS ================= */
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

  pAMPCfg->SensorBias = voltageSteps[0]; 
  pAMPCfg->Vzero = 1100;                
  
  pAMPCfg->LptiaRtiaSel = LPTIARTIA_4K; 
  pAMPCfg->LpTiaRl = LPTIARLOAD_SHORT;
  pAMPCfg->ADCRefVolt = 1.82;
}

/* ================= ARDUINO SETUP ================= */
void setup()
{
  Serial.begin(115200);
  while (!Serial);
  delay(200);

  // --- CONNECT TO WI-FI ---
  Serial.println("\n==================================");
  Serial.println("  AD5940 Wireless CA Server");
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
  AD5940AMPStructInit();

  if (AppAMPInit(AppBuff, APPBUFF_SIZE) != AD5940ERR_OK)
  {
    Serial.println("Error: Init Failed!");
    while (1);
  }

  // Header for Serial Monitor Backup
  Serial.println("Time(ms),Voltage(mV),Current(uA)"); 
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
  // Keep the WebSocket alive
  ws.cleanupClients();

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

      // --- BATCH BUFFER ---
      char dataBatch[1024] = ""; 
      char singlePoint[64];

      for (uint32_t i = 0; i < count; i++)
      {
        unsigned long elapsedTime = currentMillis - totalTestStartTime;

        // Print to Serial Backup
        Serial.print(elapsedTime); Serial.print(",");
        Serial.print(voltageSteps[currentStepIndex], 1); Serial.print(",");
        Serial.println(pCurrent[i], 4);

        // Add to WebSocket Batch (Format: Time,Voltage,Current|)
        // We drop the 'Index' over Wi-Fi since the graph only needs Time and Current!
        snprintf(singlePoint, sizeof(singlePoint), "%lu,%.1f,%.4f|", elapsedTime, voltageSteps[currentStepIndex], pCurrent[i]);
        strcat(dataBatch, singlePoint);

        currentDataStep++;
      }

      // Blast the batch to the webpage
      if (strlen(dataBatch) > 0) {
        ws.textAll(dataBatch);
      }
    }

    // 2. The Multi-Step Logic
    if (currentMillis - stepStartTime >= stepDuration)
    {
      currentStepIndex++; 

      if (currentStepIndex < totalSteps) 
      {
        AD5940_SetVoltage(voltageSteps[currentStepIndex], 1100.0f);
        stepStartTime = currentMillis; 
      }
      else 
      {
        AppAMPCtrl(AMPCTRL_STOPNOW, 0);
        testActive = false;

        Serial.println("\n=== MULTI-STEP CA TEST COMPLETE ===");
      }
    }
  }  
}