#include <WiFi.h>
#include <WebServer.h>

// --- UPDATE YOUR HOTSPOT PASSWORD ---
const char* ssid = "Deshawn";     
const char* password = "12345678"; // Make sure this perfectly matches your iPhone!

const int ledPin = 1; // Your onboard LED
WebServer server(80); 

// The HTML for your phone's control panel website
String htmlPage = "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
                  "<style>body { font-family: Arial; text-align: center; margin-top: 50px; } "
                  ".btn { font-size: 24px; padding: 15px 32px; margin: 10px; border-radius: 8px; border: none; color: white; } "
                  ".on { background-color: #4CAF50; } .off { background-color: #f44336; }</style></head><body>"
                  "<h2>ESP32-S3 Wi-Fi Control</h2>"
                  "<button class=\"btn on\" onclick=\"fetch('/on')\">Turn LED ON</button><br>"
                  "<button class=\"btn off\" onclick=\"fetch('/off')\">Turn LED OFF</button>"
                  "</body></html>";

void setup() {
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  delay(2000);

  Serial.println("\n--- Connecting to iPhone Hotspot (Low Power Mode) ---");
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(); 
  delay(100);

  // Limit the transmission power to prevent brownouts!
  WiFi.setTxPower(WIFI_POWER_8_5dBm); 

  WiFi.begin(ssid, password);

  // Wait for the iPhone to assign an IP address
  Serial.print("Connecting");
  int attempts = 0;
  
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[ SUCCESS! ]");
    Serial.print("Open Safari on your iPhone and type this IP Address: ");
    Serial.println(WiFi.localIP()); 
  } else {
    Serial.println("\n[ FAILED ]");
    Serial.print("Error Code: ");
    Serial.println(WiFi.status());
    Serial.println("Check password and keep the Hotspot screen awake!");
  }

  // Web Server Commands (What happens when you tap the buttons)
  server.on("/", []() { server.send(200, "text/html", htmlPage); });
  
  server.on("/on", []() { 
    digitalWrite(ledPin, HIGH); 
    Serial.println("Action: LED ON!"); 
    server.send(200, "text/plain", "ON"); 
  });
  
  server.on("/off", []() { 
    digitalWrite(ledPin, LOW); 
    Serial.println("Action: LED OFF!"); 
    server.send(200, "text/plain", "OFF"); 
  });

  server.begin();
}

void loop() {
  // Keep listening for your phone tapping the buttons
  server.handleClient(); 
}