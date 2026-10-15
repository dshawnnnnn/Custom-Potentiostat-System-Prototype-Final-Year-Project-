#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  delay(2000); // Give the serial port time to connect via USB
  
  Serial.println("\n=================================");
  Serial.println("     ESP32-S3 Wi-Fi Scanner      ");
  Serial.println("=================================");

  // Set Wi-Fi to station mode and disconnect from any saved networks
  WiFi.mode(WIFI_STA); 
  WiFi.disconnect();   
  delay(100);
  
  Serial.println("Wi-Fi Hardware Initialized. Starting continuous scan...");
}

void loop() {
  Serial.println("\nScanning for networks...");
  int n = WiFi.scanNetworks();
  
  if (n == 0) {
    Serial.println("No Wi-Fi networks found. (Check antenna connection)");
  } else {
    Serial.print("Success! Found ");
    Serial.print(n);
    Serial.println(" networks.");
    
    // Print the top 5 strongest networks
    for (int i = 0; i < min(n, 5); ++i) {
      Serial.print("  [");
      Serial.print(i + 1);
      Serial.print("] ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (Signal: ");
      Serial.print(WiFi.RSSI(i));
      Serial.println(" dBm)");
      delay(10); // Small delay to keep serial buffer happy
    }
  }
  
  // Wait 5 seconds before scanning again
  delay(5000); 
}