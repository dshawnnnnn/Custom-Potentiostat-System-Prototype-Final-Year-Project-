#include <SPI.h>
#include "ESP32Port.h"

extern "C" {
  #include "ad5940.h"
}

// Global flag from ESP32Port.cpp
extern volatile uint32_t ucInterrupted;

// --- DEFINITIONS FOR YOUR DRIVER VERSION ---
#ifndef GP0_OUTPUT
  #define GP0_OUTPUT (1ul << 3) 
#endif
#ifndef GP0_GPIO
  #define GP0_GPIO   (0ul)
#endif

void setup() {
  Serial.begin(115200);
  delay(3000); // Safe wait for USB

  Serial.println("\n\n========================================");
  Serial.println("   AD5940 Interrupt & Reset Test");
  Serial.println("========================================");

  // 1. Initialize SPI
  if (AD5940_MCUResourceInit(0) == 0) {
    Serial.println(">> SPI Initialized.");
  }

  // ================================================
  // TEST 1: HARDWARE RESET
  // ================================================
  Serial.println("\n[TEST 1] Testing Hardware Reset Pin...");
  
  // A. Write a "Secret Number" to a register
  uint32_t secretCode = 0x00A5A5A5;
  AD5940_WriteReg(REG_AFE_WGFCW, secretCode);
  
  // B. Verify it was written
  uint32_t check = AD5940_ReadReg(REG_AFE_WGFCW);
  Serial.print("   >> Written: 0x"); Serial.println(check, HEX);
  
  if (check != secretCode) {
    Serial.println("   ❌ ABORT: SPI Write failed. Fix wiring first.");
    while(1);
  }

  // C. Pull the RESET Pin (Toggle Hardware Line)
  Serial.println("   >> Pulling RESET Pin Low...");
  AD5940_HWReset(); // This toggles GPIO 1
  delay(100);       // Wait for chip to reboot

  // D. Read the register again. It SHOULD be 0x000000 now.
  check = AD5940_ReadReg(REG_AFE_WGFCW);
  Serial.print("   >> After Reset: 0x"); Serial.println(check, HEX);

  if (check == 0x000000) {
    Serial.println("   ✅ PASS: Hardware Reset works!");
  } else {
    Serial.println("   ❌ FAIL: Chip did not reset (Value is still there).");
    Serial.println("      Check connection to RST (GPIO 1).");
  }

  // ================================================
  // TEST 2: INTERRUPT (GP0)
  // ================================================
  Serial.println("\n[TEST 2] Testing Interrupt Pin (GP0)...");
  
  // A. Reset the ESP32 Flag
  AD5940_ClrMCUIntFlag();

  // B. Configure AD5940 GP0 as a manual GPIO Output
  AGPIOCfg_Type gpio_cfg;
  
  gpio_cfg.FuncSet = GP0_GPIO | GP0_OUTPUT;  
  gpio_cfg.OutVal = 1;        // Start HIGH (Idle)
  gpio_cfg.PullEnSet = 0;     // FIXED: Changed PullEn to PullEnSet
  
  AD5940_AGPIOCfg(&gpio_cfg);
  delay(10);

  Serial.println("   >> Forcing GP0 Low (Triggering Interrupt)...");
  
  // C. Force Pin LOW
  gpio_cfg.OutVal = 0;        // Pull LOW
  AD5940_AGPIOCfg(&gpio_cfg);
  delay(50); // Give ESP32 time to react

  // D. Check if ESP32 saw it
  if (AD5940_GetMCUIntFlag()) {
    Serial.println("   ✅ PASS: ESP32 detected the Interrupt!");
  } else {
    Serial.println("   ❌ FAIL: ESP32 did not see the pin go LOW.");
    Serial.println("      Check connection to GP0 (GPIO 2).");
  }

  // E. Clean up: Set GP0 back to High
  gpio_cfg.OutVal = 1;
  AD5940_AGPIOCfg(&gpio_cfg);
}

void loop() {
  // Nothing to do
}