#include <Arduino.h>
#include "esp_camera.h"
#include <HardwareSerial.h>

#define CAMERA_MODEL_XIAO_ESP32S3
#include "camera_pins.h"

HardwareSerial VideoUART(1);

#define VIDEO_TX    43
#define VIDEO_BAUD  500000

typedef struct __attribute__((packed)) {
  uint8_t  type;
  uint16_t seq;
  uint32_t ts_ms;
  uint32_t payload_len;
} pkt_hdr_t;

static uint16_t seq = 0;

static size_t cobs_encode(const uint8_t* in, size_t len, uint8_t* out) {
  const uint8_t* end = in + len;
  uint8_t* start = out;
  uint8_t* code_ptr = out++;
  uint8_t code = 1;

  while (in < end) {
    if (*in == 0) {
      *code_ptr = code;
      code_ptr = out++;
      code = 1;
      in++;
    } else {
      *out++ = *in++;
      code++;
      if (code == 0xFF) {
        *code_ptr = code;
        code_ptr = out++;
        code = 1;
      }
    }
  }

  *code_ptr = code;
  return out - start;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("[BOOT] XIAO JPEG UART test");

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;

  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;

  config.pin_xclk  = XCLK_GPIO_NUM;
  config.pin_pclk  = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href  = HREF_GPIO_NUM;

  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;

  config.pin_pwdn  = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size   = FRAMESIZE_QQVGA;
  config.jpeg_quality = 12;
  config.fb_count     = 2;
  config.fb_location  = CAMERA_FB_IN_PSRAM;
  config.grab_mode    = CAMERA_GRAB_LATEST;
  config.xclk_freq_hz = 10000000;

  Serial.println("[CAM] before init");
  esp_err_t err = esp_camera_init(&config);
  Serial.printf("[CAM] init err=0x%x\n", err);

  if (err != ESP_OK) {
    Serial.println("[CAM] init failed, stopped");
    while (true) delay(1000);
  }

  Serial.println("[CAM] init OK");

  VideoUART.begin(VIDEO_BAUD, SERIAL_8N1, -1, VIDEO_TX);
  Serial.println("[UART] VideoUART started");
}

void loop() {
  camera_fb_t *fb = esp_camera_fb_get();

  if (!fb) {
    Serial.println("[CAM] fb failed");
    delay(1000);
    return;
  }

  static uint32_t okCnt = 0;
  if ((okCnt++ % 30) == 0) {
    Serial.printf("[CAM] frame OK len=%u w=%u h=%u format=%u\n",
                  fb->len, fb->width, fb->height, fb->format);
  }

  pkt_hdr_t hdr;
  hdr.type = 0x01;
  hdr.seq = seq++;
  hdr.ts_ms = millis();
  hdr.payload_len = fb->len;

  const size_t rawLen = sizeof(hdr) + fb->len;
  const size_t encMax = rawLen + rawLen / 254 + 2;

  uint8_t* raw = (uint8_t*)ps_malloc(rawLen);
  uint8_t* enc = (uint8_t*)ps_malloc(encMax);

  if (raw && enc) {
    memcpy(raw, &hdr, sizeof(hdr));
    memcpy(raw + sizeof(hdr), fb->buf, fb->len);

    size_t encLen = cobs_encode(raw, rawLen, enc);

    VideoUART.write(enc, encLen);
    VideoUART.write((uint8_t)0x00);
  } else {
    Serial.println("[ERR] alloc failed");
  }

  if (raw) free(raw);
  if (enc) free(enc);

  esp_camera_fb_return(fb);

  delay(300);
}