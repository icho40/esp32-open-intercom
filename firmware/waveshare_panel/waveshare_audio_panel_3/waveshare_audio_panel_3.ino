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

// ================= CONFIG =================
#define USE_MDNS   1
#define MDNS_NAME  "tuer"

// ================= GLOBALS =================
AsyncWebServer server(80);

// ================= MJPEG =================
class MJPEGResponse : public AsyncAbstractResponse {
public:
  MJPEGResponse() : _state(0), _idx(0), _tmp(nullptr), _tmpLen(0) {
    _code = 200;
    _contentType = "multipart/x-mixed-replace; boundary=frame";
    _sendContentLength = false;
  }

  ~MJPEGResponse() {
    if (_tmp) free(_tmp);
  }

  bool _sourceValid() const override {
    return true;
  }

  size_t _fillBuffer(uint8_t* buf, size_t maxLen) override {
    switch (_state) {
      case 0: {
        static const char* s = "--frame\r\n";
        size_t L = strlen(s);
        size_t n = std::min(L - _idx, maxLen);
        memcpy(buf, s + _idx, n);
        _idx += n;
        if (_idx >= L) { _idx = 0; _state = 1; }
        return n;
      }

      case 1: {
        if (_tmp) {
          free(_tmp);
          _tmp = nullptr;
          _tmpLen = 0;
        }

        uartIngestCopyFrame(&_tmp, &_tmpLen);

        _hdr = String("Content-Type: image/jpeg\r\nContent-Length: ") +
               String(_tmpLen) + "\r\n\r\n";

        size_t H = _hdr.length();
        size_t n = std::min(H - _idx, maxLen);
        memcpy(buf, _hdr.c_str() + _idx, n);
        _idx += n;
        if (_idx >= H) { _idx = 0; _state = 2; }
        return n;
      }

      case 2: {
        if (!_tmp || !_tmpLen) {
          _state = 0;
          delay(20);
          return 0;
        }

        size_t n = std::min(_tmpLen - _idx, maxLen);
        memcpy(buf, _tmp + _idx, n);
        _idx += n;

        if (_idx >= _tmpLen) {
          _idx = 0;
          _state = 3;
        }
        return n;
      }

      case 3: {
        static const char* s = "\r\n";
        size_t L = strlen(s);
        size_t n = std::min(L - _idx, maxLen);
        memcpy(buf, s + _idx, n);
        _idx += n;

        if (_idx >= L) {
          _idx = 0;
          _state = 0;
          delay(30);
        }
        return n;
      }
    }

    return 0;
  }

private:
  uint8_t _state;
  size_t _idx;
  String _hdr;
  uint8_t* _tmp;
  size_t _tmpLen;
};

// ================= HELPERS =================
void listDir(File dir, String& out, const String& base = "/") {
  while (true) {
    File f = dir.openNextFile();
    if (!f) break;

    String path = String(base) + String(f.name());

    if (f.isDirectory()) {
      out += "DIR  " + path + "\n";
      File sub = LittleFS.open(path);
      listDir(sub, out, path + "/");
      sub.close();
    } else {
      out += "FILE " + path + " (" + String(f.size()) + " bytes)\n";
    }

    f.close();
  }
}

// ================= ROUTES =================
void setupRoutes() {
  server.on("/stream", HTTP_GET, [](AsyncWebServerRequest* r) {
    auto* res = new MJPEGResponse();
    res->addHeader("Cache-Control", "no-store");
    res->addHeader("Access-Control-Allow-Origin", "*");
    r->send(res);
  });

  server.on("/snapshot", HTTP_GET, [](AsyncWebServerRequest* r) {
    uint8_t* copy = nullptr;
    size_t L = 0;

    uartIngestCopyFrame(&copy, &L);

    if (!copy || !L) {
      if (copy) free(copy);
      r->send(503, "text/plain", "no frame");
      return;
    }

    AsyncWebServerResponse* res =
      r->beginResponse(200, "image/jpeg", copy, L);

    res->addHeader("Cache-Control", "no-store");
    r->send(res);

    free(copy);
  });

  server.on("/stats", HTTP_GET, [](AsyncWebServerRequest* r) {
    char buf[256];
    snprintf(buf, sizeof(buf),
      "heap=%u psram=%u jpeg=%u frames=%u bytes=%u p1=%s p2=%s p3=%s",
      ESP.getFreeHeap(),
      ESP.getFreePsram(),
      (unsigned)uartIngestFrameLength(),
      (unsigned)appStateGetFrames(),
      (unsigned)appStateGetReceivedBytes(),
      appStateGetPartyText(1),
      appStateGetPartyText(2),
      appStateGetPartyText(3)
    );

    r->send(200, "text/plain", buf);
  });

  server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest* r) {
    String json = "{";
    json += "\"p1\":\"" + String(appStateGetPartyText(1)) + "\",";
    json += "\"p2\":\"" + String(appStateGetPartyText(2)) + "\",";
    json += "\"p3\":\"" + String(appStateGetPartyText(3)) + "\"";
    json += "}";

    AsyncWebServerResponse* res =
      r->beginResponse(200, "application/json", json);

    res->addHeader("Cache-Control", "no-store");
    r->send(res);
  });

  server.on("/api/call/ack", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (!r->hasParam("party", true)) {
      r->send(400, "text/plain", "missing party");
      return;
    }

    int p = r->getParam("party", true)->value().toInt();

    if (!appStateAcknowledgeParty(p)) {
      r->send(400, "text/plain", "invalid party");
      return;
    }

    r->send(200, "text/plain", "OK");
  });

  server.on("/fs", HTTP_GET, [](AsyncWebServerRequest* r) {
    String out;
    File root = LittleFS.open("/");
    listDir(root, out, "/");
    root.close();
    r->send(200, "text/plain", out);
  });

  server.serveStatic("/", LittleFS, "/").setDefaultFile("idle.html");
  server.serveStatic("/p1/", LittleFS, "/p1/").setDefaultFile("index.html");
  server.serveStatic("/p2/", LittleFS, "/p2/").setDefaultFile("index.html");
  server.serveStatic("/p3/", LittleFS, "/p3/").setDefaultFile("index.html");
}

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

  setupRoutes();
  server.begin();

  Serial.println("[HTTP] ready");
}

void loop() {
  appStateHandleButtons();
  appStateHandleTimeouts();
  delay(10);
}