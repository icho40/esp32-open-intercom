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
#include <ESPAsyncWebServer.h>

#include "AppState.h"
#include "UartIngest.h"
#include "SystemInit.h"

// ================= GLOBALS =================
AsyncWebServer server(80);

// ================= SETUP / LOOP =================
void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println("[BOOT] Doorstation Server clean no-audio");

  appStateBegin();
  uartIngestBegin();
  systemInit(server);
}

void loop() {
  appStateHandleButtons();
  appStateHandleTimeouts();
  delay(10);
}
