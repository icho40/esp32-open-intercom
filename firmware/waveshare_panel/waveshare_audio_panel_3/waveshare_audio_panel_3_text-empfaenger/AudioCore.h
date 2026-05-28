#ifndef AUDIO_CORE_H
#define AUDIO_CORE_H

#include <Arduino.h>
#include "driver/i2s.h"
#include "freertos/ringbuf.h"
#include "Config.h"

// Zugriff auf globale Variablen in der Hauptdatei
extern RingbufHandle_t g_micRb;
extern uint32_t gMicBytesIn, gMicSendOk, gMicSendFail, gMicRmsN;
extern int32_t gMicPeak16;
extern uint64_t gMicRmsAcc;

#define Dbg Serial

// Initialisierung des I2S Mikrofons (INMP441)
void micInit() {
#if ENABLE_AUDIO
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = MIC_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 512,
    .use_apll = false
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = PIN_I2S_MIC_BCLK,
    .ws_io_num = PIN_I2S_MIC_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = PIN_I2S_MIC_DATA
  };

  i2s_driver_install(I2S_MIC_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_MIC_PORT, &pin_config);

  g_micRb = xRingbufferCreate(MIC_RING_BYTES, RINGBUF_TYPE_BYTEBUF);
  if (!g_micRb) Dbg.println("[MIC] Ringbuf Error!");
  else Dbg.printf("[MIC] OK, Ringbuf %d KB\n", MIC_RING_BYTES / 1024);
#endif
}

// Task, der ständig Audiodaten liest und in den Puffer schiebt
void micTask(void* pv) {
#if ENABLE_AUDIO
  static int32_t samples32[MIC_CHUNK_SAMPLES];
  static int16_t samples16[MIC_CHUNK_SAMPLES];

  while (true) {
    size_t bytesRead = 0;
    i2s_read(I2S_MIC_PORT, samples32, sizeof(samples32), &bytesRead, portMAX_DELAY);
    int n = bytesRead / 4;
    if (n <= 0) continue;

    int32_t peak = 0;
    uint64_t sumSq = 0;

    for (int i = 0; i < n; i++) {
      // Wandlung von 32-bit (I2S) zu 16-bit PCM
      int32_t val = samples32[i] >> 14; 
      if (val > 32767) val = 32767;
      if (val < -32768) val = -32768;
      samples16[i] = (int16_t)val;

      // Statistik für Lautstärke-Anzeige
      int32_t av = abs(val);
      if (av > peak) peak = av;
      sumSq += (int64_t)val * val;
    }

    gMicPeak16 = peak;
    gMicRmsAcc += sumSq;
    gMicRmsN += n;
    gMicBytesIn += (n * 2);

    if (g_micRb) {
      // Daten in den Ringbuffer für den Webserver schieben
      if (xRingbufferSend(g_micRb, samples16, n * 2, 0) == pdTRUE) {
        gMicSendOk += (n * 2);
      } else {
        // Falls Puffer voll, Platz schaffen
        void* dummy;
        size_t dummySz;
        dummy = xRingbufferReceiveUpTo(g_micRb, &dummySz, 0, n * 2);
        if (dummy) vRingbufferReturnItem(g_micRb, dummy);
        gMicSendFail += (n * 2);
      }
    }
  }
#endif
}

#endif