#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "ESP32Port.h"

extern "C" {
  #include "ad5940.h"
  #include "Impedance.h"
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
  <title>Live EIS Dashboard</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
  <style>
    body { font-family: Arial; text-align: center; padding: 20px; background-color: #f4f4f9;}
    .chart-container { max-width: 800px; margin: 20px auto; background: white; padding: 15px; box-shadow: 0px 0px 10px rgba(0,0,0,0.1); border-radius: 8px;}
    canvas { width: 100% !important; height: auto !important; }
    h2 { color: #333; margin-bottom: 5px; }
    h3 { color: #666; margin-top: 0px; font-size: 16px; }
    .live-data { font-size: 18px; font-weight: bold; color: #007bff; margin-bottom: 20px; }
  </style>
</head>
<body>
  <h2>Electrochemical Impedance Spectroscopy</h2>
  <h3>Live Wireless Telemetry</h3>
  
  <div class="live-data" id="currentValues">Waiting for AD5940 Data...</div>

  <div class="chart-container">
    <canvas id="magChart"></canvas>
  </div>

  <div class="chart-container">
    <canvas id="nyquistChart"></canvas>
  </div>

  <div class="chart-container">
    <canvas id="phaseChart"></canvas>
  </div>

  <script>
    const chartOptions = { animation: false };

    // 1. Magnitude Chart (Actual Impedance Value)
    const magCtx = document.getElementById('magChart').getContext('2d');
    const magChart = new Chart(magCtx, {
      type: 'scatter',
      data: { datasets: [{ label: 'Actual Impedance Magnitude (\u2126)', data: [], borderColor: '#007bff', backgroundColor: '#007bff', showLine: true, borderWidth: 2, pointRadius: 2 }] },
      options: { ...chartOptions, scales: { x: { type: 'logarithmic', title: { display: true, text: 'Frequency (Hz)' } }, y: { type: 'logarithmic', title: { display: true, text: 'Magnitude (\u2126)' } } } }
    });

    // 2. Nyquist Chart
    const nyqCtx = document.getElementById('nyquistChart').getContext('2d');
    const nyquistChart = new Chart(nyqCtx, {
      type: 'scatter',
      data: { datasets: [{ label: "Nyquist Plot (-Z'' vs Z')", data: [], borderColor: '#dc3545', backgroundColor: '#dc3545', showLine: true, borderWidth: 2, pointRadius: 3 }] },
      options: { ...chartOptions, scales: { x: { title: { display: true, text: "Z' (Real) [\u2126]" } }, y: { title: { display: true, text: "-Z'' (Imaginary) [\u2126]" } } } }
    });

    // 3. Phase Chart
    const phaseCtx = document.getElementById('phaseChart').getContext('2d');
    const phaseChart = new Chart(phaseCtx, {
      type: 'scatter',
      data: { datasets: [{ label: 'Phase Shift (Degrees)', data: [], borderColor: '#28a745', backgroundColor: '#28a745', showLine: true, borderWidth: 2, pointRadius: 2 }] },
      options: { ...chartOptions, scales: { x: { type: 'logarithmic', title: { display: true, text: 'Frequency (Hz)' } }, y: { title: { display: true, text: 'Phase (Deg)' } } } }
    });

    // WebSocket Parser
    var gateway = `ws://${window.location.hostname}/ws`;
    var websocket = new WebSocket(gateway);
    
    websocket.onmessage = function(event) {
      let points = event.data.split('|');
      points.forEach(point => {
        if (point.length > 0) {
          let values = point.split(',');
          let freq = parseFloat(values[0]);
          let mag = parseFloat(values[1]);
          let phaseDeg = parseFloat(values[2]);

          // Math for Nyquist
          let phaseRad = phaseDeg * (Math.PI / 180.0);
          let zReal = mag * Math.cos(phaseRad);
          let zImag = mag * Math.sin(phaseRad);
          let nyquistY = -zImag; 

          // Update Live Text Banner
          document.getElementById('currentValues').innerText = 
            `Freq: ${freq.toFixed(1)} Hz | Mag: ${mag.toFixed(1)} \u2126 | Phase: ${phaseDeg.toFixed(1)}\u00B0`;

          // Push to the 3 separate charts
          magChart.data.datasets[0].data.push({x: freq, y: mag});
          nyquistChart.data.datasets[0].data.push({x: zReal, y: nyquistY});
          phaseChart.data.datasets[0].data.push({x: freq, y: phaseDeg});
        }
      });
      
      magChart.update();
      nyquistChart.update();
      phaseChart.update();
    };
  </script>
</body>
</html>
)rawliteral";

/* ================= USER BUFFER & VARIABLES ================= */
#define APPBUFF_SIZE 512
uint32_t AppBuff[APPBUFF_SIZE];
float LFOSCFreq;

int totalSweepPoints = 1001; 
int pointsCollected = 0;
bool testActive = false;

/* ================= PLATFORM CONFIG ================= */
static int32_t AD5940PlatformCfg(void)
{
  CLKCfg_Type clk_cfg;
  FIFOCfg_Type fifo_cfg;
  AGPIOCfg_Type gpio_cfg;
  LFOSCMeasure_Type LfoscMeasure;

  AD5940_HWReset();
  AD5940_Initialize();

  clk_cfg.ADCClkDiv = ADCCLKDIV_1;
  clk_cfg.ADCCLkSrc = ADCCLKSRC_HFOSC;
  clk_cfg.SysClkDiv = SYSCLKDIV_1;
  clk_cfg.SysClkSrc = SYSCLKSRC_HFOSC;
  clk_cfg.HfOSC32MHzMode = bFALSE;
  clk_cfg.HFOSCEn = bTRUE;
  clk_cfg.HFXTALEn = bFALSE;
  clk_cfg.LFOSCEn = bTRUE;
  AD5940_CLKCfg(&clk_cfg);

  fifo_cfg.FIFOEn = bFALSE;
  fifo_cfg.FIFOMode = FIFOMODE_FIFO;
  fifo_cfg.FIFOSize = FIFOSIZE_4KB;                       
  fifo_cfg.FIFOSrc = FIFOSRC_DFT;
  fifo_cfg.FIFOThresh = 4;        
  AD5940_FIFOCfg(&fifo_cfg);
  
  fifo_cfg.FIFOEn = bTRUE;
  AD5940_FIFOCfg(&fifo_cfg);
  
  AD5940_INTCCfg(AFEINTC_1, AFEINTSRC_ALLINT, bTRUE);   
  AD5940_INTCClrFlag(AFEINTSRC_ALLINT);
  AD5940_INTCCfg(AFEINTC_0, AFEINTSRC_DATAFIFOTHRESH | AFEINTSRC_ENDSEQ | AFEINTSRC_CUSTOMINT0, bTRUE); 
  AD5940_INTCClrFlag(AFEINTSRC_ALLINT);

  gpio_cfg.FuncSet = GP0_INT | GP1_SLEEP | GP2_SYNC;
  gpio_cfg.InputEnSet = 0;
  gpio_cfg.OutputEnSet = AGPIO_Pin0 | AGPIO_Pin1 | AGPIO_Pin2;
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

/* ================= EIS PARAMETERS ================= */
void AD5940ImpedanceStructInit(void)
{
  AppIMPCfg_Type *pImpedanceCfg;
  AppIMPGetCfg(&pImpedanceCfg);

  pImpedanceCfg->WuptClkFreq = LFOSCFreq;
  pImpedanceCfg->SeqStartAddr = 0;
  pImpedanceCfg->MaxSeqLen = 512; 

  pImpedanceCfg->RcalVal = 10000.0;     
  pImpedanceCfg->SinFreq = 60000.0;
  pImpedanceCfg->FifoThresh = 4;
    
  pImpedanceCfg->DswitchSel = SWD_CE0;
  pImpedanceCfg->PswitchSel = SWP_RE0;
  pImpedanceCfg->NswitchSel = SWN_SE0;
  pImpedanceCfg->TswitchSel = SWT_SE0LOAD;
  
  pImpedanceCfg->HstiaRtiaSel = HSTIARTIA_10K;  
    
  pImpedanceCfg->SweepCfg.SweepEn = bTRUE;
  pImpedanceCfg->SweepCfg.SweepStart = 30.0f;    
  pImpedanceCfg->SweepCfg.SweepStop = 150e3f;     
  pImpedanceCfg->SweepCfg.SweepPoints = totalSweepPoints;      
  pImpedanceCfg->SweepCfg.SweepLog = bTRUE;
  
  pImpedanceCfg->PwrMod = AFEPWR_HP;
  pImpedanceCfg->ADCSinc3Osr = ADCSINC3OSR_2;     
  pImpedanceCfg->DftNum = DFTNUM_16384;
  pImpedanceCfg->DftSrc = DFTSRC_SINC3;
}

/* ================= ARDUINO SETUP ================= */
void setup()
{
  Serial.begin(115200);
  while (!Serial);
  delay(200);

  // --- CONNECT TO WI-FI ---
  Serial.println("\n==================================");
  Serial.println("  AD5940 Wireless EIS Server");
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
    Serial.println("❌ SPI FAILURE: Cannot see AD5940.");
    while(1); 
  }
  
  AD5940PlatformCfg();
  AD5940ImpedanceStructInit();

  if (AppIMPInit(AppBuff, APPBUFF_SIZE) != AD5940ERR_OK)
  {
    Serial.println("❌ Init Failed!");
    while (1);
  }

  // Backup Serial Header
  Serial.println("Frequency(Hz),Magnitude(Ohm),Phase(Deg)"); 
  
  AppIMPCtrl(IMPCTRL_START, 0);
  testActive = true;
}

/* ================= ARDUINO LOOP ================= */
void loop()
{
  // Keep WebSocket alive
  ws.cleanupClients();

  if (testActive) 
  {
    if (AD5940_GetMCUIntFlag() || digitalRead(AD5940_INTR_PIN) == LOW)
    {
      AD5940_ClrMCUIntFlag();

      uint32_t count = APPBUFF_SIZE;
      AppIMPISR(AppBuff, &count);

      fImpPol_Type *pImp = (fImpPol_Type*)AppBuff;
      
      float freq;
      AppIMPCtrl(IMPCTRL_GETFREQ, &freq);

      // --- BATCH BUFFER ---
      char dataBatch[1024] = ""; 
      char singlePoint[64];

      for (uint32_t i = 0; i < count; i++)
      {
        float phaseDeg = pImp[i].Phase * 180.0 / PI;

        // Print to Serial Backup
        Serial.print(freq, 2); Serial.print(",");
        Serial.print(pImp[i].Magnitude, 4); Serial.print(",");
        Serial.println(phaseDeg, 4); 

        // Add to WebSocket Batch (Format: Freq,Mag,PhaseDeg|)
        snprintf(singlePoint, sizeof(singlePoint), "%.2f,%.4f,%.4f|", freq, pImp[i].Magnitude, phaseDeg);
        strcat(dataBatch, singlePoint);

        pointsCollected++;
      }
      
      // Blast the batch to the webpage
      if (strlen(dataBatch) > 0) {
        ws.textAll(dataBatch);
      }
      
      if (pointsCollected >= totalSweepPoints)
      {
        AppIMPCtrl(IMPCTRL_STOPNOW, 0);
        testActive = false;
        Serial.println("\n=== EIS SWEEP COMPLETE ===");
      }
    }
  }  
}