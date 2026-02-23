#pragma once
#include <Arduino.h>
#include "UartProto.h"

class UartSender {
public:
  explicit UartSender(HardwareSerial& s) : ser(s) {}

  void begin(uint32_t baud, int rxPin, int txPin) {
    ser.begin(baud, SERIAL_8N1, rxPin, txPin);
  }

  bool sendPacket(uint8_t type, const uint8_t* payload, uint16_t len) {
    if (len > UART_MAX_PAYLOAD) return false;

    // Build raw packet: hdr + payload + crc
    // raw length = PKT_HDR_LEN + len + 4
    const size_t raw_len = PKT_HDR_LEN + (size_t)len + PKT_CRC_LEN;

    // We allocate on heap only if needed (JPEG can be big).
    uint8_t* raw = (uint8_t*)malloc(raw_len);
    if (!raw) return false;

    raw[0] = type;
    u16le_write(raw + 1, seq++);
    u16le_write(raw + 3, len);
    if (len) memcpy(raw + PKT_HDR_LEN, payload, len);

    const uint32_t crc = crc32_ieee(raw, PKT_HDR_LEN + (size_t)len);
    u32le_write(raw + PKT_HDR_LEN + (size_t)len, crc);

    // COBS encode
    // worst-case out = raw_len + raw_len/254 + 1
    const size_t out_cap = raw_len + raw_len / 254 + 2;
    uint8_t* enc = (uint8_t*)malloc(out_cap);
    if (!enc) { free(raw); return false; }

    const size_t enc_len = cobs_encode(raw, raw_len, enc, out_cap);
    free(raw);
    if (enc_len == 0) { free(enc); return false; }

    // Write + delimiter
    ser.write(enc, enc_len);
    ser.write(UART_FRAME_DELIM);
    free(enc);
    return true;
  }

  bool sendEventJson(const char* json) {
    const size_t n = strlen(json);
    if (n > 60000) return false;
    return sendPacket(PKT_EVENT, (const uint8_t*)json, (uint16_t)n);
  }

private:
  HardwareSerial& ser;
  uint16_t seq = 0;
};