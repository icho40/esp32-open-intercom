#include "WebRoutes.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#include <algorithm>

#include "AppState.h"
#include "UartIngest.h"

namespace {

class MJPEGResponse : public AsyncAbstractResponse {
public:
  MJPEGResponse()
    : _state(0),
      _idx(0),
      _tmp(nullptr),
      _tmpLen(0) {
    _code = 200;
    _contentType = "multipart/x-mixed-replace; boundary=frame";
    _sendContentLength = false;
  }

  ~MJPEGResponse() override {
    if (_tmp) {
      free(_tmp);
    }
  }

  bool _sourceValid() const override {
    return true;
  }

  size_t _fillBuffer(uint8_t* buffer, size_t maxLength) override {
    switch (_state) {
      case 0: {
        static const char* separator = "--frame\r\n";

        const size_t length = strlen(separator);
        const size_t count =
          std::min(length - _idx, maxLength);

        memcpy(buffer, separator + _idx, count);
        _idx += count;

        if (_idx >= length) {
          _idx = 0;
          _state = 1;
        }

        return count;
      }

      case 1: {
        if (_tmp) {
          free(_tmp);
          _tmp = nullptr;
          _tmpLen = 0;
        }

        uartIngestCopyFrame(&_tmp, &_tmpLen);

        _header =
          String("Content-Type: image/jpeg\r\nContent-Length: ") +
          String(_tmpLen) +
          "\r\n\r\n";

        const size_t length = _header.length();
        const size_t count =
          std::min(length - _idx, maxLength);

        memcpy(buffer, _header.c_str() + _idx, count);
        _idx += count;

        if (_idx >= length) {
          _idx = 0;
          _state = 2;
        }

        return count;
      }

      case 2: {
        if (!_tmp || !_tmpLen) {
          _state = 0;
          delay(20);
          return 0;
        }

        const size_t count =
          std::min(_tmpLen - _idx, maxLength);

        memcpy(buffer, _tmp + _idx, count);
        _idx += count;

        if (_idx >= _tmpLen) {
          _idx = 0;
          _state = 3;
        }

        return count;
      }

      case 3: {
        static const char* ending = "\r\n";

        const size_t length = strlen(ending);
        const size_t count =
          std::min(length - _idx, maxLength);

        memcpy(buffer, ending + _idx, count);
        _idx += count;

        if (_idx >= length) {
          _idx = 0;
          _state = 0;
          delay(30);
        }

        return count;
      }
    }

    return 0;
  }

private:
  uint8_t _state;
  size_t _idx;
  String _header;
  uint8_t* _tmp;
  size_t _tmpLen;
};

class JPEGSnapshotResponse : public AsyncAbstractResponse {
public:
  JPEGSnapshotResponse(uint8_t* data, size_t length)
    : _data(data),
      _length(length),
      _index(0) {
    _code = 200;
    _contentType = "image/jpeg";
    _contentLength = length;
    _sendContentLength = true;
  }

  ~JPEGSnapshotResponse() override {
    if (_data) {
      free(_data);
    }
  }

  bool _sourceValid() const override {
    return _data != nullptr && _length > 0;
  }

  size_t _fillBuffer(uint8_t* buffer, size_t maxLength) override {
    if (!_data || _index >= _length) {
      return 0;
    }

    const size_t count =
      std::min(_length - _index, maxLength);

    memcpy(buffer, _data + _index, count);
    _index += count;

    return count;
  }

private:
  uint8_t* _data;
  size_t _length;
  size_t _index;
};

void listDirectory(File directory, String& output, const String& base = "/") {
  while (true) {
    File file = directory.openNextFile();

    if (!file) {
      break;
    }

    const String path =
      String(base) + String(file.name());

    if (file.isDirectory()) {
      output += "DIR  " + path + "\n";

      File subdirectory = LittleFS.open(path);
      listDirectory(subdirectory, output, path + "/");
      subdirectory.close();
    } else {
      output +=
        "FILE " +
        path +
        " (" +
        String(file.size()) +
        " bytes)\n";
    }

    file.close();
  }
}

}  // namespace

void webRoutesSetup(AsyncWebServer& server) {
  server.on("/stream", HTTP_GET, [](AsyncWebServerRequest* request) {
    auto* response = new MJPEGResponse();

    response->addHeader("Cache-Control", "no-store");
    response->addHeader("Access-Control-Allow-Origin", "*");

    request->send(response);
  });

  server.on("/snapshot", HTTP_GET, [](AsyncWebServerRequest* request) {
    uint8_t* copy = nullptr;
    size_t length = 0;

    uartIngestCopyFrame(&copy, &length);

    if (!copy || !length) {
      if (copy) {
        free(copy);
      }

      request->send(503, "text/plain", "no frame");
      return;
    }

    auto* response =
      new JPEGSnapshotResponse(copy, length);

    response->addHeader("Cache-Control", "no-store");
    request->send(response);
  });

  server.on("/stats", HTTP_GET, [](AsyncWebServerRequest* request) {
    char buffer[256];

    snprintf(
      buffer,
      sizeof(buffer),
      "heap=%u psram=%u jpeg=%u frames=%u bytes=%u p1=%s p2=%s p3=%s",
      ESP.getFreeHeap(),
      ESP.getFreePsram(),
      static_cast<unsigned>(uartIngestFrameLength()),
      static_cast<unsigned>(appStateGetFrames()),
      static_cast<unsigned>(appStateGetReceivedBytes()),
      appStateGetPartyText(1),
      appStateGetPartyText(2),
      appStateGetPartyText(3)
    );

    request->send(200, "text/plain", buffer);
  });

  server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest* request) {
    String json = "{";

    json +=
      "\"p1\":\"" +
      String(appStateGetPartyText(1)) +
      "\",";

    json +=
      "\"p2\":\"" +
      String(appStateGetPartyText(2)) +
      "\",";

    json +=
      "\"p3\":\"" +
      String(appStateGetPartyText(3)) +
      "\"";

    json += "}";

    AsyncWebServerResponse* response =
      request->beginResponse(
        200,
        "application/json",
        json
      );

    response->addHeader("Cache-Control", "no-store");
    request->send(response);
  });

  server.on("/api/call/ack", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (!request->hasParam("party", true)) {
      request->send(
        400,
        "text/plain",
        "missing party"
      );
      return;
    }

    const int party =
      request->getParam("party", true)->value().toInt();

    if (!appStateAcknowledgeParty(party)) {
      request->send(
        400,
        "text/plain",
        "invalid party"
      );
      return;
    }

    request->send(200, "text/plain", "OK");
  });

  server.on("/fs", HTTP_GET, [](AsyncWebServerRequest* request) {
    String output;

    File root = LittleFS.open("/");
    listDirectory(root, output, "/");
    root.close();

    request->send(
      200,
      "text/plain",
      output
    );
  });

  server
    .serveStatic("/", LittleFS, "/")
    .setDefaultFile("idle.html");

  server
    .serveStatic("/p1/", LittleFS, "/p1/")
    .setDefaultFile("index.html");

  server
    .serveStatic("/p2/", LittleFS, "/p2/")
    .setDefaultFile("index.html");

  server
    .serveStatic("/p3/", LittleFS, "/p3/")
    .setDefaultFile("index.html");
}
