#include "ESP32Port.h"
#include <SPI.h>

// Standard ESP32 SPI Settings
SPISettings spiSettings(1000000, MSBFIRST, SPI_MODE0);

// --- INTERRUPT HANDLER ---
volatile static uint32_t ucInterrupted = 0;

void IRAM_ATTR AD5940_ISR(void) {
  ucInterrupted = 1;
}

// --- C LINKAGE BLOCK ---
extern "C" {

  void AD5940_CsSet(void) {
    digitalWrite(AD5940_CS_PIN, HIGH);
  }

  void AD5940_CsClr(void) {
    digitalWrite(AD5940_CS_PIN, LOW);
  }

  void AD5940_RstSet(void) {
    digitalWrite(AD5940_RST_PIN, HIGH);
  }

  void AD5940_RstClr(void) {
    digitalWrite(AD5940_RST_PIN, LOW);
  }

  void AD5940_Delay10us(uint32_t time) {
    if (time > 0) delayMicroseconds(time * 10);
  }

  uint32_t AD5940_GetMCUIntFlag(void) {
    return ucInterrupted;
  }

  uint32_t AD5940_ClrMCUIntFlag(void) {
    ucInterrupted = 0;
    return 1;
  }

  // NOTE: Return type fixed to uint32_t (was void)
  uint32_t AD5940_MCUResourceInit(void *pCfg) {
    // 1. Setup Pins
    pinMode(AD5940_CS_PIN, OUTPUT);
    pinMode(AD5940_RST_PIN, OUTPUT);
    digitalWrite(AD5940_CS_PIN, HIGH);
    digitalWrite(AD5940_RST_PIN, HIGH);

    pinMode(AD5940_GP0_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(AD5940_GP0_PIN), AD5940_ISR, FALLING);

    // 2. Start SPI with Custom Super Mini Pins
    SPI.begin(AD5940_SCK_PIN, AD5940_MISO_PIN, AD5940_MOSI_PIN, AD5940_CS_PIN);
    
    return 0; // Success
  }
  
  // NOTE: This function signature must match ad5940.h exactly
  void AD5940_ReadWriteNBytes(unsigned char *pSendBuffer, unsigned char *pRecvBuff, unsigned long length) {
    SPI.beginTransaction(spiSettings);
    for (unsigned long i = 0; i < length; i++) {
      uint8_t data = (pSendBuffer != NULL) ? pSendBuffer[i] : 0xFF;
      uint8_t rx = SPI.transfer(data);
      if (pRecvBuff != NULL) {
        pRecvBuff[i] = rx;
      }
    }
    SPI.endTransaction();
  }

} // End extern "C"