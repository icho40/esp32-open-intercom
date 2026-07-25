#include "SystemInit.h"

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ESPmDNS.h>

#include "secrets.h"
#include "WebRoutes.h"

namespace {

constexpr bool USE_MDNS = true;
constexpr const char* MDNS_NAME = "tuer";

void initPsram() {
  if (psramInit()) {
    Serial.printf("[PSRAM] OK: %u\n", ESP.getPsramSize());
  } else {
    Serial.println("[PSRAM] NOT FOUND");
  }
}

void initFilesystem() {
  if (!LittleFS.begin(true)) {
    Serial.println("[FS] LittleFS mount failed");
  } else {
    Serial.println("[FS] LittleFS mounted");
  }
}

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("[WiFi] connecting");

  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }

  Serial.printf(
    "\n[WiFi] IP: %s\n",
    WiFi.localIP().toString().c_str()
  );
}

void startMdns() {
  if (!USE_MDNS) {
    return;
  }

  if (MDNS.begin(MDNS_NAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[mDNS] http://%s.local/\n", MDNS_NAME);
  } else {
    Serial.println("[mDNS] start failed");
  }
}

}  // namespace

void systemInit(AsyncWebServer& server) {
  initPsram();
  initFilesystem();
  connectWifi();
  startMdns();

  webRoutesSetup(server);
  server.begin();

  Serial.println("[HTTP] ready");
}
