#include <SPI.h>
#include "ESP32Port.h"

void setup() {
  // 1. Start Serial
  Serial.begin(115200);
  
  // Wait for Serial Monitor
  unsigned long startTime = millis();
  while (!Serial && millis() - startTime < 3000); 
  delay(1000); 

  Serial.println("\n\n================================");
  Serial.println("   AD5941 Breakout Chip ID Test   ");
  Serial.println("   (ESP32-S3 Super Mini Config)   ");
  Serial.println("================================");

  // 2. Initialize Hardware
  AD5940_MCUResourceInit(0);
  Serial.println(">> Hardware Layer Initialized.");

  // 3. Hardware Reset
  Serial.print(">> Resetting AD5941 Hardware... ");
  AD5940_HWReset();
  Serial.println("Done.");
}

void loop() {
  // 4. Read the Chip ID Register (Address 0x404)
  uint16_t chipID = AD5940_ReadReg(REG_AFECON_CHIPID);

  // 5. Check the Result
  Serial.print(">> Read Register 0x404: 0x");
  Serial.println(chipID, HEX);

  if (chipID == 0x5502) {
    Serial.println("✅ SUCCESS: AD5941 Detected (Rev 2)");
  } 
  else if (chipID == 0x5503) {
    Serial.println("✅ SUCCESS: AD5940 Detected (Rev 3)");
  } 
  else if (chipID == 0x0000) {
    Serial.println("❌ FAILURE: Read 0x0000.");
    Serial.println("   - Check MISO (Pin 5) connection.");
    Serial.println("   - Check if 3.3V power is ON.");
  } 
  else if (chipID == 0xFFFF) {
    Serial.println("❌ FAILURE: Read 0xFFFF.");
    Serial.println("   - Check CS (Pin 7) connection.");
  } 
  else {
    Serial.println("⚠️ WARNING: Unknown ID received.");
  }
  
  Serial.println("--------------------------------");
  delay(2000); 
}