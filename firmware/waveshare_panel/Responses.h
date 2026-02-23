#ifndef RESPONSES_H
#define RESPONSES_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "freertos/ringbuf.h"
#include "Config.h"

// Zugriff auf globale Variablen der Hauptdatei
extern uint8_t* gJpeg;
extern size_t gJpegLen;
extern portMUX_TYPE gMux;
extern RingbufHandle_t g_micRb;

// ================= MJPEG STREAM RESPONSE =================
class MJPEGStreamResponse : public AsyncAbstractResponse {
private:
    uint8_t _party;
    uint32_t _interval;
    uint32_t _lastMs = 0;

public:
    MJPEGStreamResponse(uint8_t party, uint32_t intervalMs) {
        _party = party;
        _interval = intervalMs;
        _code = 200;
        _contentType = "multipart/x-mixed-replace;boundary=frame";
    }

    size_t _fillBuffer(uint8_t *data, size_t len) override {
        uint32_t now = millis();
        if (_interval > 0 && (now - _lastMs < _interval)) return 0;
        _lastMs = now;

        size_t jlen = 0;
        portENTER_CRITICAL(&gMux);
        jlen = gJpegLen;
        portEXIT_CRITICAL(&gMux);

        if (jlen == 0 || !gJpeg) return 0;

        // HTTP Multipart Header für das Einzelbild
        size_t headLen = snprintf((char*)data, len,
            "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %d\r\n\r\n", jlen);
        
        if (headLen + jlen + 2 > len) return 0; // Puffer zu klein

        portENTER_CRITICAL(&gMux);
        memcpy(data + headLen, gJpeg, jlen);
        portEXIT_CRITICAL(&gMux);

        memcpy(data + headLen + jlen, "\r\n", 2);
        return headLen + jlen + 2;
    }
};

// ================= WAV AUDIO STREAM RESPONSE =================
// Hilfsfunktion für den WAV-Header
static void writeWavHeaderUnknown(uint8_t* h, uint32_t sampleRate) {
    memcpy(h, "RIFF", 4);
    uint32_t fileSize = 0x7FFFFFFF; // Unbekannte Länge (Live-Stream)
    memcpy(h + 4, &fileSize, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    uint32_t fmtLen = 16;
    memcpy(h + 16, &fmtLen, 4);
    uint16_t fmttag = 1; // PCM
    memcpy(h + 20, &fmttag, 2);
    uint16_t channels = 1; // Mono
    memcpy(h + 22, &channels, 2);
    memcpy(h + 24, &sampleRate, 4);
    uint32_t byteRate = sampleRate * 2;
    memcpy(h + 28, &byteRate, 4);
    uint16_t blockAlign = 2;
    memcpy(h + 32, &blockAlign, 2);
    uint16_t bps = 16;
    memcpy(h + 34, &bps, 2);
    memcpy(h + 36, "data", 4);
    uint32_t dataSize = 0x7FFFFFFF;
    memcpy(h + 40, &dataSize, 4);
}

class WAVStreamResponse : public AsyncAbstractResponse {
private:
    bool _headerSent = false;
public:
    WAVStreamResponse() {
        _code = 200;
        _contentType = "audio/wav";
    }

    size_t _fillBuffer(uint8_t *data, size_t len) override {
        size_t total = 0;
        if (!_headerSent) {
            writeWavHeaderUnknown(data, MIC_SAMPLE_RATE);
            total += 44;
            _headerSent = true;
        }

        if (g_micRb) {
            size_t available = 0;
            // Daten aus dem Ringbuffer holen
            void* rbData = xRingbufferReceiveUpTo(g_micRb, &available, 0, len - total);
            if (rbData) {
                memcpy(data + total, rbData, available);
                vRingbufferReturnItem(g_micRb, rbData);
                total += available;
            }
        }
        return total;
    }
};

#endif