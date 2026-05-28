#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <LittleFS.h>
#include <ESPmDNS.h>

#include "driver/uart.h"
#include "freertos/ringbuf.h"

#include "secrets.h"

// ================= CONFIG =================
#define UART_PORT UART_NUM_1
#define PIN_UART_RX 18
#define UART_BAUD 500000
#define JPEG_MAX (120 * 1024)

// 👉 FIX: passt zum XIAO!
#define PKT_JPEG 0x01

// ================= GLOBALS =================
AsyncWebServer server(80);

static uint8_t* gJpeg = nullptr;
static size_t gLen = 0;
static uint32_t gFrames = 0;

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

static uint8_t* encBuf;
static uint8_t* decBuf;
static size_t pktLen = 0;

typedef struct __attribute__((packed)) {
  uint8_t  type;
  uint16_t seq;
  uint32_t ts_ms;
  uint32_t payload_len;
} pkt_hdr_t;
// ================= COBS =================
size_t cobs_decode(const uint8_t* in, size_t len, uint8_t* out) {
  const uint8_t* end = in + len;
  size_t o = 0;
  while (in < end) {
    uint8_t code = *in++;
    if (code == 0) break;
    for (uint8_t i = 1; i < code; i++) {
      if (in >= end) return 0;
      out[o++] = *in++;
    }
    if (code < 0xFF && in < end) out[o++] = 0;
  }
  return o;
}

// ================= FRAME =================
void setFrame(uint8_t* data, size_t len) {
  taskENTER_CRITICAL(&mux);
  if (gJpeg) free(gJpeg);
  gJpeg = (uint8_t*)ps_malloc(len);
  if (gJpeg) {
    memcpy(gJpeg, data, len);
    gLen = len;
  }
  taskEXIT_CRITICAL(&mux);
}

// ================= UART =================
void uartTask(void*) {

  uart_config_t cfg = {
    .baud_rate = UART_BAUD,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
  };

  uart_param_config(UART_PORT, &cfg);
  uart_set_pin(UART_PORT, UART_PIN_NO_CHANGE, PIN_UART_RX,
               UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

  uart_driver_install(UART_PORT, 4096, 4096, 0, NULL, 0);

  uint8_t rx[128];

  for (;;) {
    int n = uart_read_bytes(UART_PORT, rx, sizeof(rx), 100 / portTICK_PERIOD_MS);
    if (n > 0) {
      Serial.print("RX: ");
      for (int i = 0; i < n; i++) {
        Serial.write(rx[i]);
      }
      Serial.println();
    }
  }
}
// ================= ROUTES =================
void setupRoutes() {

  server.on("/stats", HTTP_GET, [](AsyncWebServerRequest* r) {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "heap=%u psram=%u jpeg=%u frames=%u",
             ESP.getFreeHeap(),
             ESP.getFreePsram(),
             (unsigned)gLen,
             (unsigned)gFrames);
    r->send(200, "text/plain", buf);
  });

  server.on("/snapshot", HTTP_GET, [](AsyncWebServerRequest* r) {
    taskENTER_CRITICAL(&mux);
    if (gJpeg && gLen) {
      r->send_P(200, "image/jpeg", gJpeg, gLen);
    } else {
      r->send(503, "text/plain", "no frame");
    }
    taskEXIT_CRITICAL(&mux);
  });
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("\n[BOOT] Doorstation Server V3");

  if (psramInit())
    Serial.printf("[PSRAM] OK: %u\n", ESP.getPsramSize());
  else
    Serial.println("[PSRAM] FAIL");

  encBuf = (uint8_t*)ps_malloc(JPEG_MAX);
  decBuf = (uint8_t*)ps_malloc(JPEG_MAX);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }

  Serial.println("\nIP:");
  Serial.println(WiFi.localIP());

  xTaskCreatePinnedToCore(uartTask, "uart", 4096, NULL, 1, NULL, 1);

  setupRoutes();
  server.begin();
}

// ================= LOOP =================
void loop() {
}