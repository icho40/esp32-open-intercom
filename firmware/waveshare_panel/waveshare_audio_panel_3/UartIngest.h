#ifndef UART_INGEST_H
#define UART_INGEST_H

#include <Arduino.h>
#include "driver/uart.h"
#include "esp_rom_crc.h"
#include "Config.h"

// Zugriff auf globale Variablen und Puffer in der Hauptdatei
extern uint8_t *gEncBuf, *gDecBuf, *gJpeg;
extern size_t gJpegLen;
extern uint32_t gFrameCnt;
extern portMUX_TYPE gMux;
#define Dbg Serial

// COBS Dekodierung: Wandelt den gerahmten UART-Datenstrom zurück in binäre Daten
size_t cobs_decode(const uint8_t* input, size_t length, uint8_t* output) {
  size_t read_index = 0, write_index = 0;
  while (read_index < length) {
    uint8_t code = input[read_index++];
    if (read_index + code - 1 > length) return 0; // Fehler im Stream
    for (uint8_t i = 1; i < code; i++) output[write_index++] = input[read_index++];
    if (code < 0xFF && read_index < length) output[write_index++] = 0;
  }
  return write_index;
}

void uart_ingest_task(void* pv) {
#if ENABLE_UART
  // UART Hardware-Konfiguration
  uart_config_t uart_cfg = {
    .baud_rate = UART_BAUD,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .source_clk = UART_SCLK_DEFAULT
  };
  uart_param_config(UART_PORT, &uart_cfg);
  uart_set_pin(UART_PORT, UART_PIN_NO_CHANGE, PIN_UART_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  uart_driver_install(UART_PORT, 2048, 0, 0, NULL, 0);

  Dbg.println("[UART] Task gestartet");

  size_t encIdx = 0;
  while (true) {
    uint8_t b;
    // Lies Byte für Byte vom UART
    if (uart_read_bytes(UART_PORT, &b, 1, portMAX_DELAY) > 0) {
      if (b == 0x00) { // Ende eines COBS-Pakets erreicht
        if (encIdx > 4) {
          // 1. Dekodieren
          size_t decLen = cobs_decode(gEncBuf, encIdx, gDecBuf);
          if (decLen > 4) {
            // 2. CRC-Prüfsumme validieren (letzte 4 Bytes)
            uint32_t msgCrc;
            memcpy(&msgCrc, gDecBuf + decLen - 4, 4);
            uint32_t calcCrc = esp_rom_crc32_le(0, gDecBuf, decLen - 4);

            if (msgCrc == calcCrc) {
              // 3. Bild in den globalen Jpeg-Speicher kopieren
              portENTER_CRITICAL(&gMux);
              if (!gJpeg) gJpeg = (uint8_t*)ps_malloc(JPEG_MAX_BYTES);
              if (gJpeg) {
                gJpegLen = decLen - 4;
                memcpy(gJpeg, gDecBuf, gJpegLen);
                gFrameCnt++;
              }
              portEXIT_CRITICAL(&gMux);
            }
          }
        }
        encIdx = 0; // Buffer zurücksetzen für nächstes Frame
      } else {
        // Byte im Buffer sammeln
        if (encIdx < JPEG_MAX_BYTES + 250) {
          gEncBuf[encIdx++] = b;
        }
      }
    }
  }
#endif
}

#endif