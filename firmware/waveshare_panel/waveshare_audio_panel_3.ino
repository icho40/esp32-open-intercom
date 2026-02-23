#include <Arduino.h>
#include "USB.h"
 
#include "UartProto.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <FS.h>
#include <LittleFS.h>
#include <ESPmDNS.h>

// WICHTIG: Die Reihenfolge der Tabs
#include "Config.h"      // 1. Enums und Pins
#include "Responses.h"   // 2. Die Streaming-Klassen (MJPEGStreamResponse etc.)
#include "AudioCore.h"   // 3. Audio Logik
#include "UartIngest.h"  // 4. UART Logik
#include "WebRoutes.h"   // 5. Server Routen
#include "secrets.h"     // Deine WiFi Daten

// --- Globale Variablen Definitionen ---
AsyncWebServer server(80);
RingbufHandle_t g_micRb = nullptr;

volatile CallState g_state[4] = { CS_IDLE, CS_IDLE, CS_IDLE, CS_IDLE };
volatile uint32_t g_stateSinceMs[4] = { 0, 0, 0, 0 };
volatile bool g_talkActive[4] = { false, false, false, false };

uint8_t* gJpeg = nullptr;
size_t gJpegLen = 0;
uint8_t* gEncBuf = nullptr;
uint8_t* gDecBuf = nullptr;
uint32_t gFrameCnt = 0;
portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;

// Statistiken
uint32_t gMicSendOk = 0, gMicSendFail = 0, gMicBytesIn = 0, gMicRmsN = 0;
int32_t gMicPeak16 = 0;
uint64_t gMicRmsAcc = 0;

void setState(Party p, CallState s) {
  g_state[p] = s;
  g_stateSinceMs[p] = millis();
  if (s == CS_IDLE) g_talkActive[p] = false;
}

// --- Hilfsfunktionen für Hardware ---
void mountFS() {
  if (!LittleFS.begin(true)) Dbg.println("[FS] Mount failed");
}

void connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Dbg.print("."); }
  Dbg.println("\n[WiFi] OK");
}

void setupMDNS() {
  if (MDNS.begin(MDNS_NAME)) MDNS.addService("http", "tcp", 80);
}

void setupButtons() {
  pinMode(PIN_BTN_P1, INPUT_PULLUP);
  pinMode(PIN_BTN_P2, INPUT_PULLUP);
  pinMode(PIN_BTN_P3, INPUT_PULLUP);
}

void setup() {
  Dbg.begin(115200);
  delay(2000);
  if (psramInit()) Dbg.printf("[PSRAM] OK %u\n", ESP.getPsramSize());

  mountFS();
  connectWiFi();
  setupMDNS();
  setupButtons();

#if ENABLE_UART
  gEncBuf = (uint8_t*)ps_malloc(JPEG_MAX_BYTES + 256);
  gDecBuf = (uint8_t*)ps_malloc(JPEG_MAX_BYTES + 64);
  xTaskCreatePinnedToCore(uart_ingest_task, "uart", 4096, nullptr, 1, nullptr, 1);
#endif

#if ENABLE_AUDIO
  micInit();
  xTaskCreatePinnedToCore(micTask, "mic", 4096, nullptr, 1, nullptr, 0);
#endif

  setupRoutes();
  server.begin();
  Dbg.println("[READY]");
}

void loop() {
  static uint32_t lastMs[4] = { 0, 0, 0, 0 };
  static uint8_t lastLvl[4] = { 1, 1, 1, 1 };
  uint32_t now = millis();

  auto pollBtn = [&](int p, int pin) {
    uint8_t lvl = digitalRead(pin);
    if (lvl != lastLvl[p]) {
      lastLvl[p] = lvl;
      if (lvl == 0 && (now - lastMs[p] > BTN_DEBOUNCE_MS)) {
        lastMs[p] = now;
        if (g_state[p] == CS_IDLE) setState((Party)p, CS_RING);
      }
    }
  };

  for(int i=1; i<=3; i++) {
    pollBtn(i, (i==1?PIN_BTN_P1:(i==2?PIN_BTN_P2:PIN_BTN_P3)));
    
    uint32_t age = now - g_stateSinceMs[i];
    if (g_state[i] == CS_RING && age > RING_TIMEOUT_MS) setState((Party)i, CS_IDLE);
    if (g_state[i] == CS_CALL && age > CALL_TIMEOUT_MS) setState((Party)i, CS_IDLE);
  }
  delay(50);
}