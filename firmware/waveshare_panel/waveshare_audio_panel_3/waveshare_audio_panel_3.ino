/*
  Waveshare Doorstation Server
  - UART JPEG ingest vom XIAO
  - /stream MJPEG
  - /snapshot
  - LittleFS: /idle.html, /p1/, /p2/, /p3/
  - Taster P1/P2/P3
  - /api/state
  - /api/call/ack
*/

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include <memory>
#include <algorithm>

#include "secrets.h"
#include "AppState.h"
#include "UartIngest.h"
#include "WebRoutes.h"

// ================= CONFIG =================
#define USE_MDNS   1
#define MDNS_NAME  "tuer"

// ================= GLOBALS =================
AsyncWebServer server(80);

// ================= SETUP / LOOP =================
void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println("[BOOT] Doorstation Server clean no-audio");

  if (psramInit()) {
    Serial.printf("[PSRAM] OK: %u\n", ESP.getPsramSize());
  } else {
    Serial.println("[PSRAM] NOT FOUND");
  }

  appStateBegin();

  if (!LittleFS.begin(true)) {
    Serial.println("[FS] LittleFS mount failed");
  } else {
    Serial.println("[FS] LittleFS mounted");
  }

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.printf("[WiFi] connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }

  Serial.printf("\n[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());

#if USE_MDNS
  if (MDNS.begin(MDNS_NAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[mDNS] http://%s.local/\n", MDNS_NAME);
  }
#endif

  uartIngestBegin();

  webRoutesSetup(server);
  server.begin();

  Serial.println("[HTTP] ready");
}

void loop() {
  appStateHandleButtons();
  appStateHandleTimeouts();
  delay(10);
}