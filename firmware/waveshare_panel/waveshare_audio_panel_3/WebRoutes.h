#ifndef WEB_ROUTES_H
#define WEB_ROUTES_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include "Config.h"
#include "Responses.h"

// Zugriff auf globale Objekte und Variablen
extern AsyncWebServer server;
extern volatile CallState g_state[4];
extern volatile bool g_talkActive[4];
extern void setState(Party p, CallState s);
extern uint32_t gMicSendOk, gMicSendFail, gMicBytesIn;
extern int32_t gMicPeak16;

// Hilfsfunktion für Logs (falls du die LOGF-Logik beibehalten willst)
#define LOGF(fmt, ...) Dbg.printf("[" fmt "]\n", ##__VA_ARGS__)
#define Dbg Serial

void setupRoutes() {
  
  // --- MJPEG & Snapshot Streams ---
  auto handleStream = [](AsyncWebServerRequest* r) {
    uint8_t p = 1;
    if (r->url().startsWith("/p1/")) p = 1;
    else if (r->url().startsWith("/p2/")) p = 2;
    else if (r->url().startsWith("/p3/")) p = 3;

    uint32_t iv = 0;
    if (r->hasParam("fps")) {
      int fps = r->getParam("fps")->value().toInt();
      if (fps > 0) iv = 1000u / (uint32_t)fps;
    }
    
    auto* res = new MJPEGStreamResponse(p, iv);
    res->addHeader("Access-Control-Allow-Origin", "*");
    r->send(res);
  };

  server.on("/p1/stream", HTTP_GET, handleStream);
  server.on("/p2/stream", HTTP_GET, handleStream);
  server.on("/p3/stream", HTTP_GET, handleStream);

  // --- Audio WAV Stream ---
  auto handleAudio = [](AsyncWebServerRequest* r) {
    auto* res = new WAVStreamResponse();
    res->addHeader("Access-Control-Allow-Origin", "*");
    r->send(res);
  };

  server.on("/p1/audio", HTTP_GET, handleAudio);
  server.on("/p2/audio", HTTP_GET, handleAudio);
  server.on("/p3/audio", HTTP_GET, handleAudio);

  // --- API: Call Acknowledge (Tablet nimmt an) ---
  server.on("/api/call/ack", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (!r->hasParam("party", true)) { r->send(400); return; }
    int p = r->getParam("party", true)->value().toInt();
    if (p < 1 || p > 3) { r->send(400); return; }
    
    setState((Party)p, CS_CALL);
    LOGF("API ACK P%d", p);
    r->send(200, "text/plain", "OK");
  });

  // --- API: Push-To-Talk ---
  server.on("/api/talk/start", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (!r->hasParam("party", true)) { r->send(400); return; }
    int p = r->getParam("party", true)->value().toInt();
    g_talkActive[p] = true;
    r->send(200, "text/plain", "TALK START");
  });

  server.on("/api/talk/stop", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (!r->hasParam("party", true)) { r->send(400); return; }
    int p = r->getParam("party", true)->value().toInt();
    g_talkActive[p] = false;
    r->send(200, "text/plain", "TALK STOP");
  });

  // --- Status & Debug ---
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* r) {
    String json = "{";
    for(int i=1; i<=3; i++) {
      json += "\"p" + String(i) + "\":{\"state\":" + String((int)g_state[i]) + 
              ",\"talk\":" + String(g_talkActive[i]?"true":"false") + "}";
      if(i<3) json += ",";
    }
    json += "}";
    r->send(200, "application/json", json);
  });

  // --- Statische Dateien (UI) ---
  server.serveStatic("/p1/", LittleFS, "/p1/").setDefaultFile("index.html");
  server.serveStatic("/p2/", LittleFS, "/p2/").setDefaultFile("index.html");
  server.serveStatic("/p3/", LittleFS, "/p3/").setDefaultFile("index.html");

  // Fallback
  server.onNotFound([](AsyncWebServerRequest* r) {
    r->send(404, "text/plain", "Nicht gefunden");
  });
}

#endif