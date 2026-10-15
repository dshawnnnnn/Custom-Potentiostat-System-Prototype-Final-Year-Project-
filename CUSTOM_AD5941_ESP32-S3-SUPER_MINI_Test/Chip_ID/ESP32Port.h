#ifndef ESP32PORT_H
#define ESP32PORT_H

#include <Arduino.h>

// --- ESP32-S3 SUPER MINI PINOUT ---
#define AD5940_SCK_PIN   4
#define AD5940_MISO_PIN  5
#define AD5940_MOSI_PIN  6
#define AD5940_CS_PIN    7

#define AD5940_RST_PIN   1
#define AD5940_GP0_PIN   2

// --- C LINKAGE WRAPPER ---
// This forces the compiler to treat the AD5940 driver as C code
#ifdef __cplusplus
extern "C" {
#endif

#include "ad5940.h"

#ifdef __cplusplus
}
#endif

#endif // ESP32PORT_H