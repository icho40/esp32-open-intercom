#ifndef CONFIG_H
#define CONFIG_H
#include <Arduino.h>
#include "USB.h"
#include "USBCDC.h"

// --- Neue Typ-Definitionen hierher verschoben ---
enum Party : uint8_t { P1 = 1, P2 = 2, P3 = 3 };
enum CallState : uint8_t { CS_IDLE = 0, CS_RING = 1, CS_CALL = 2 };

// Deine Dbg-Definition für alle Tabs
#define Dbg Serial
// ---- Hardware & Features ---- [cite: 4]
#define USE_MDNS 1
#define MDNS_NAME "tuer"
#define ENABLE_UART 1
#define ENABLE_AUDIO 1

// ---- UART (XIAO -> ESP32-S3) ---- [cite: 4]
#define UART_PORT UART_NUM_1
#define PIN_UART_RX 18
#define PIN_UART_TX UART_PIN_NO_CHANGE
#define UART_BAUD 1000000

// ---- I2S MIC (INMP441) ---- [cite: 4]
#define I2S_MIC_PORT I2S_NUM_0
#define PIN_I2S_MIC_BCLK 11
#define PIN_I2S_MIC_WS 12
#define PIN_I2S_MIC_DATA 13
#define MIC_SAMPLE_RATE 16000

// ---- Buttons (nach GND) ---- [cite: 4]
#define PIN_BTN_P1 4
#define PIN_BTN_P2 5
#define PIN_BTN_P3 6
static const uint32_t BTN_DEBOUNCE_MS = 120;

// ---- JPEG & Audio Buffers ---- [cite: 5]
#define JPEG_MAX_BYTES (120 * 1024)
#define MIC_CHUNK_SAMPLES 256
#define MIC_RING_BYTES (64 * 1024)
#define WAV_HEADER_BYTES 44

// ---- Call Timeouts ---- [cite: 5, 6]
static const uint32_t RING_TIMEOUT_MS = 30 * 1000;
static const uint32_t CALL_TIMEOUT_MS = 120 * 1000;

// ---- Logging ---- [cite: 7]
static constexpr size_t LOG_BUF_SZ = 16 * 1024;

#endif