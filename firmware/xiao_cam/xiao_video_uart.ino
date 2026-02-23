/*
  XIAO ESP32S3 Sense → UART JPEG Sender (COBS, adaptives FPS/Quality)
  - Packet:
      struct {
        uint8_t  type;          // 0x01 = PKT_VIDEO_JPEG
        uint16_t seq;           // ++
        uint32_t ts_ms;         // millis()
        uint32_t payload_len;   // JPEG bytes
      } + JPEG + COBS + 0x00
  - UART: 1'000'000 baud
*/


#include <esp_system.h>
#include <Arduino.h>
#include <math.h>
#include "esp_camera.h"
#include "driver/uart.h"
#include "camera_pins.h"    // deine geprüfte Pinbelegung
#include "esp_rom_crc.h"


// ===================== UART / Protokoll =====================
#define UART_PORT     UART_NUM_1
#define PIN_UART_TX   43                 // bei dir vorhanden
#define PIN_UART_RX   UART_PIN_NO_CHANGE
#define UART_BAUD     1000000

enum : uint8_t { PKT_VIDEO_JPEG = 0x01, PKT_AUDIO_MULAW = 0x02 };

typedef struct __attribute__((packed)) {
  uint8_t  type;
  uint16_t seq;
  uint32_t ts_ms;
  uint32_t payload_len;
  uint32_t crc32;
} pkt_hdr_t;

#define JPEG_MAX_BYTES   (120 * 1024)

// ===================== Adaptive Tx Config =====================
#define UART_TARGET_UTIL_MIN   0.70f     // 70 %
#define UART_TARGET_UTIL_MAX   0.85f     // 85 %
#define FPS_MIN                4
#define FPS_MAX                20
#define QUAL_MIN               10        // kleiner = besser Qualität = mehr Bytes
#define QUAL_MAX               28        // größer = schlechtere Qualität = weniger Bytes
#define ADAPT_EVERY_N_FRAMES   12
#define UTIL_SMOOTH_ALPHA      0.25f


static float    g_uartUtilEMA = 0.0f;
static uint16_t g_sinceAdapt  = 0;
static uint8_t  g_curQuality  = 15;      // Startqualität
static uint8_t  g_curFps      = 10;      // Start-FPS
static uint32_t g_nextFrameMs = 0;

static sensor_t* g_sensor = nullptr;     // globales Sensor-Handle

// ===================== COBS Encoder =====================
// out: max len + len/254 + 1  | Rückgabe: Länge ohne 0x00 Terminator
static size_t cobs_encode(const uint8_t* data, size_t len, uint8_t* out)
{
  const uint8_t* end = data + len;
  uint8_t* out_start = out;
  uint8_t* code_ptr  = out++;     // Platz für Code-Byte
  uint8_t  code      = 1;

  while (data < end) {
    if (*data == 0) {
      *code_ptr = code;
      code_ptr  = out++;
      code = 1;
      data++;
    } else {
      *out++ = *data++;
      code++;
      if (code == 0xFF) {
        *code_ptr = 0xFF;
        code_ptr  = out++;
        code = 1;
      }
    }
  }
  *code_ptr = code;
  return (size_t)(out - out_start);
}

// ===================== UART Helpers =====================
static void uart_init()
{
  uart_config_t cfg = {
    .baud_rate = UART_BAUD,
    .data_bits = UART_DATA_8_BITS,
    .parity    = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .rx_flow_ctrl_thresh = 0,
    .source_clk = UART_SCLK_APB
  };

  esp_err_t e;

  e = uart_param_config(UART_PORT, &cfg);
  Serial.printf("[UART] param_config=%d\n", (int)e);

  e = uart_set_pin(UART_PORT, PIN_UART_TX, PIN_UART_RX,
                   UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  Serial.printf("[UART] set_pin=%d\n", (int)e);

  e = uart_driver_install(UART_PORT, 2048, 0, 0, NULL, 0);
  Serial.printf("[UART] driver_install=%d\n", (int)e);
}


// ===================== Adaptive helpers =====================
static inline float bytes_to_uart_ms(size_t bytes, uint32_t baud)
{
  const float bits = (float)bytes * 10.0f;   // 10 Bit/Byte (Start+Stop)
  return (bits / (float)baud) * 1000.0f;
}

static void adapt_tx_params(size_t encodedBytes)
{
  const size_t onwire = encodedBytes + 1;  // + Terminator
  const float  tx_ms  = bytes_to_uart_ms(onwire, UART_BAUD);
  const float  period_ms = 1000.0f / (float)g_curFps;

  float util = tx_ms / period_ms;
  g_uartUtilEMA = (1.0f - UTIL_SMOOTH_ALPHA) * g_uartUtilEMA + UTIL_SMOOTH_ALPHA * util;

  if (++g_sinceAdapt < ADAPT_EVERY_N_FRAMES) return;
  g_sinceAdapt = 0;

  bool changed = false;

  if (g_uartUtilEMA > UART_TARGET_UTIL_MAX) {
  if (g_curFps > FPS_MIN) {
    int nf = (int)roundf((float)g_curFps * 0.85f);
    g_curFps = (uint8_t)constrain(nf, (int)FPS_MIN, (int)FPS_MAX);
    changed = true;
    Serial.printf("[ADAPT] util=%.2f -> FPS=%u\n", g_uartUtilEMA, g_curFps);
  } else if (g_sensor && g_curQuality < QUAL_MAX) {
    int nq = (int)g_curQuality + 1;
    g_curQuality = (uint8_t)constrain(nq, (int)QUAL_MIN, (int)QUAL_MAX);
    g_sensor->set_quality(g_sensor, g_curQuality);
    changed = true;
    Serial.printf("[ADAPT] util=%.2f -> quality=%u (smaller files)\n", g_uartUtilEMA, g_curQuality);
  }
} else if (g_uartUtilEMA < UART_TARGET_UTIL_MIN) {
  if (g_sensor && g_curQuality > QUAL_MIN) {
    int nq = (int)g_curQuality - 1;
    g_curQuality = (uint8_t)constrain(nq, (int)QUAL_MIN, (int)QUAL_MAX);
    g_sensor->set_quality(g_sensor, g_curQuality);
    changed = true;
    Serial.printf("[ADAPT] util=%.2f -> quality=%u (better)\n", g_uartUtilEMA, g_curQuality);
  } else if (g_curFps < FPS_MAX) {
    int nf = (int)roundf((float)g_curFps * 1.12f);
    g_curFps = (uint8_t)constrain(nf, (int)FPS_MIN, (int)FPS_MAX);
    changed = true;
    Serial.printf("[ADAPT] util=%.2f -> FPS=%u\n", g_uartUtilEMA, g_curFps);
  }
}


  if (changed) {
    // Die neue Periode setzt loop() unten nach jedem Frame.
  }
}

// ===================== Kamera Init =====================
static bool camera_init()
{
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;

  // Pins aus camera_pins.h
  config.pin_pwdn  = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.pin_xclk  = XCLK_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href  = HREF_GPIO_NUM;
  config.pin_pclk  = PCLK_GPIO_NUM;

  config.xclk_freq_hz = 20000000;        // 20 MHz für OV2640
  config.pixel_format = PIXFORMAT_JPEG;

  config.fb_location  = CAMERA_FB_IN_DRAM;
  config.grab_mode    = CAMERA_GRAB_LATEST;
  config.fb_count     = 1;

  // Startwerte – werden gleich via g_sensor final gesetzt
  config.frame_size   = FRAMESIZE_QVGA;
  config.jpeg_quality = g_curQuality;

Serial.printf("[CAM] XCLK=%d, SIOD=%d, SIOC=%d\n", XCLK_GPIO_NUM, SIOD_GPIO_NUM, SIOC_GPIO_NUM);
Serial.printf("[CAM] PWDN=%d, RESET=%d\n", PWDN_GPIO_NUM, RESET_GPIO_NUM);
Serial.printf("[CAM] D0..D7=%d,%d,%d,%d,%d,%d,%d,%d\n",
              Y2_GPIO_NUM,Y3_GPIO_NUM,Y4_GPIO_NUM,Y5_GPIO_NUM,
              Y6_GPIO_NUM,Y7_GPIO_NUM,Y8_GPIO_NUM,Y9_GPIO_NUM);
Serial.printf("[CAM] VSYNC=%d HREF=%d PCLK=%d\n", VSYNC_GPIO_NUM, HREF_GPIO_NUM, PCLK_GPIO_NUM);


  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAM] init failed 0x%X\n", (unsigned)err);
    return false;
  }
  if (psramFound()) {
  Serial.println("[PSRAM] OK");
} else {
  Serial.println("[PSRAM] NOT FOUND !!!");
}


  g_sensor = esp_camera_sensor_get();
  if (g_sensor) {
    g_sensor->set_framesize(g_sensor, FRAMESIZE_QVGA);   // Start: QVGA
    g_sensor->set_quality  (g_sensor, g_curQuality);
    g_sensor->set_brightness(g_sensor, 0);
    g_sensor->set_saturation(g_sensor, 0);
    g_sensor->set_sharpness(g_sensor, 0);
    g_sensor->set_contrast (g_sensor, 0);
    g_sensor->set_whitebal (g_sensor, 1);
    g_sensor->set_exposure_ctrl(g_sensor, 1);
    g_sensor->set_gain_ctrl    (g_sensor, 1);
  }

  Serial.printf("[CAM] ready (QVGA, quality=%u)\n", g_curQuality);
  return true;
}

// ===================== Arbeitspuffer =====================
static uint8_t* g_in  = nullptr;   // Header + JPEG
static uint8_t* g_out = nullptr;   // COBS-enc

static bool buffers_init()
{
  size_t in_max  = sizeof(pkt_hdr_t) + JPEG_MAX_BYTES;
  size_t out_max = in_max + (in_max / 254) + 8;

  g_in  = (uint8_t*)ps_malloc(in_max);
  g_out = (uint8_t*)ps_malloc(out_max);
  if (!g_in || !g_out) {
    Serial.println("[BUF] ps_malloc failed");
    return false;
  }
  return true;
}

// ===================== Globals =====================
static uint16_t g_seq = 0;

#ifndef LED_BUILTIN
#define LED_BUILTIN 21
#endif

// ===================== Setup / Loop =====================
void setup()
{
  Serial.begin(115200);
  delay(1500);                 // <-- wichtig: Monitor Zeit geben
  Serial.println("\n[BOOT] setup start");
  Serial.printf("[CHIP] psramFound=%d\n", psramFound());
  delay(200);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  Serial.println("[BOOT] XIAO UART JPEG TX");

Serial.printf("[DEBUG] Testing GPIO TX Pin %d...\n", PIN_UART_TX);
  
  pinMode(PIN_UART_TX, OUTPUT);
  
  // Sende 10 kurze Impulse (Rechteckwelle)
  for (int i = 0; i < 10; i++) {
    digitalWrite(PIN_UART_TX, HIGH); // Signal auf HIGH setzen
    delay(1);                        // Kurz warten
    digitalWrite(PIN_UART_TX, LOW);  // Signal auf LOW setzen
    delay(1);                        // Kurz warten
  }
  
  Serial.println("[DEBUG] GPIO Test finished. Check with scope.");

/*
  if (!camera_init()) {
    Serial.println("[BOOT] camera init failed, reboot in 5s");
    delay(5000);
    esp_restart();
  }

  if (!buffers_init()) {
    Serial.println("[BOOT] buffers init failed, reboot in 5s");
    delay(5000);
    esp_restart();
  }
  */
  if (!camera_init()) {
  Serial.println("[BOOT] camera init failed - STOP");
  while (true) { delay(1000); }
}

if (!buffers_init()) {
  Serial.println("[BOOT] buffers init failed - STOP");
  while (true) { delay(1000); }
}

  uart_init();
  Serial.printf("[UART] TX pin=%d, %u baud\n", PIN_UART_TX, (unsigned)UART_BAUD);

  g_nextFrameMs = millis();    // Startdeadline
}
static inline void uart_write_all(const uint8_t* p, size_t n)
{
  while (n > 0) {
    int wr = uart_write_bytes(UART_PORT, (const char*)p, n);
    if (wr <= 0) { delay(1); continue; }
    p += wr;
    n -= (size_t)wr;
  }
}

void loop()
{
  // Adaptive FPS-Gate (nur g_curFps maßgeblich)
  if (g_curFps > 0) {
    uint32_t now = millis();
    if (now < g_nextFrameMs) { delay(1); return; }
  }

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) { delay(5); return; }

  if (fb->format != PIXFORMAT_JPEG || fb->len == 0 || fb->len > JPEG_MAX_BYTES) {
    esp_camera_fb_return(fb);
    return;
  }

  size_t jpg_len = fb->len;
  const uint8_t* jpg_ptr = fb->buf;

  pkt_hdr_t hdr;
  hdr.type        = PKT_VIDEO_JPEG;
  hdr.seq         = ++g_seq;
  hdr.ts_ms       = millis();
  hdr.payload_len = (uint32_t)jpg_len;
  hdr.crc32 = esp_rom_crc32_le(0, jpg_ptr, jpg_len);

  size_t in_len = 0;
  memcpy(g_in + in_len, &hdr, sizeof(hdr)); in_len += sizeof(hdr);
  memcpy(g_in + in_len, jpg_ptr, jpg_len);  in_len += jpg_len;

  esp_camera_fb_return(fb);  // so früh wie möglich freigeben

  size_t enc_len = cobs_encode(g_in, in_len, g_out);

  uart_write_all(g_out, enc_len);
  const uint8_t zero = 0x00;
  uart_write_all(&zero, 1);

  // kurze LED-Flanke
  digitalWrite(LED_BUILTIN, HIGH); delay(1); digitalWrite(LED_BUILTIN, LOW);

  // Adaptiver Regler (passt g_curFps / g_curQuality an)
  adapt_tx_params(enc_len);

  // Nächste Deadline aus aktuellem g_curFps
  if (g_curFps > 0) {
    const uint32_t period = (uint32_t)roundf(1000.0f / (float)g_curFps);
   g_nextFrameMs += period;
if ((int32_t)(millis() - g_nextFrameMs) > 0) g_nextFrameMs = millis() + period;
  }
}
