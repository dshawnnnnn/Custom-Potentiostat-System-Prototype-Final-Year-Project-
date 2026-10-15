#include <SPI.h>
#include "ESP32Port.h"

extern "C" {
  #include "ad5940.h"
}

// Fallback: If your header file is old, it might miss this definition.
// The Waveform Generator Frequency Control Word is at offset 0x4014
#ifndef REG_AFE_WGFCW
  #define REG_AFE_WGFCW 0x2014 
#endif

void setup() {
  // 1. Safe Mode Startup (Prevents Crash Loop)
  Serial.begin(115200);
  unsigned long start = millis();
  while (!Serial && (millis() - start < 3000)); // Wait max 3 seconds
  delay(1000);

  Serial.println("\n\n==================================");
  Serial.println("    AD5941 Read/Write Test");
  Serial.println("==================================");

  // 2. Initialize Hardware
  // This uses your working ESP32Port.cpp settings
  if (AD5940_MCUResourceInit(0) == 0) {
    Serial.println(">> SPI Hardware Initialized.");
  }

  // 3. Hardware Reset
  AD5940_HWReset();
  Serial.println(">> Hardware Reset Complete.");
  
  // 4. Sanity Check: Read Chip ID first
  // If this fails, writing is pointless.
  uint16_t chipID = AD5940_ReadReg(REG_AFECON_CHIPID);
  Serial.print(">> Chip ID Check: 0x");
  Serial.println(chipID, HEX);

  if (chipID != 0x5502 && chipID != 0x5503) {
    Serial.println("❌ ABORT: Chip ID failed. Check connections.");
    while(1); // Stop here
  }
  Serial.println("✅ Chip ID OK. Starting Memory Test...");
}

void loop() {
  // Use a pattern that fits in 24 bits (0x00 to 0xFFFFFF)
  uint32_t validPattern = 0x00112233; 

  Serial.print(">> Writing: 0x");
  Serial.println(validPattern, HEX);
  
  AD5940_WriteReg(REG_AFE_WGFCW, validPattern);
  delay(10); 
  
  uint32_t result = AD5940_ReadReg(REG_AFE_WGFCW);
  Serial.print(">> Read Back: 0x");
  Serial.println(result, HEX);

  if (result == validPattern) {
    Serial.println("✅ PERFECT MATCH. Hardware is ready.");
  } else {
    Serial.println("⚠️ Mismatch (Did you unplug anything?)");
  }
  
  delay(5000);
}