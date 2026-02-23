#pragma once
#include <Arduino.h>
#include "UartProto.h"

struct UartPacketView {
  uint8_t  type;
  uint16_t seq;
  uint16_t len;
  const uint8_t* payload;
};

class UartReceiver {
public:
  explicit UartReceiver(HardwareSerial& s) : ser(s) {}

  void begin(uint32_t baud, int rxPin, int txPin) {
    ser.begin(baud, SERIAL_8N1, rxPin, txPin);
  }

  // Call frequently from loop() or a task.
  // onPacket gets called with a view into an internal buffer (valid until next packet).
  template<typename Fn>
  void poll(Fn&& onPacket) {
    while (ser.available()) {
      const uint8_t b = (uint8_t)ser.read();

      if (b == UART_FRAME_DELIM) {
        if (rx_len == 0) continue; // ignore empty frames

        handleFrame(onPacket);
        rx_len = 0;
      } else {
        if (rx_len < sizeof(rx_buf)) {
          rx_buf[rx_len++] = b;
        } else {
          // overflow -> drop frame until delimiter
          overflow_drops++;
        }
      }
    }
  }

  uint32_t getCrcFails() const { return crc_fails; }
  uint32_t getDecodeFails() const { return decode_fails; }
  uint32_t getFramesOk() const { return frames_ok; }
  uint32_t getOverflows() const { return overflow_drops; }

private:
  template<typename Fn>
  void handleFrame(Fn&& onPacket) {
    // decode into dec_buf
    const size_t dec_len = cobs_decode(rx_buf, rx_len, dec_buf, sizeof(dec_buf));
    if (dec_len == 0 || dec_len < (PKT_HDR_LEN + PKT_CRC_LEN)) {
      decode_fails++;
      return;
    }

    const uint8_t* raw = dec_buf;
    const uint8_t type = raw[0];
    const uint16_t seq = u16le_read(raw + 1);
    const uint16_t len = u16le_read(raw + 3);

    const size_t expect = PKT_HDR_LEN + (size_t)len + PKT_CRC_LEN;
    if (expect != dec_len) {
      decode_fails++;
      return;
    }

    const uint32_t crc_rx = u32le_read(raw + PKT_HDR_LEN + (size_t)len);
    const uint32_t crc_ok = crc32_ieee(raw, PKT_HDR_LEN + (size_t)len);
    if (crc_rx != crc_ok) {
      crc_fails++;
      return;
    }

    UartPacketView v{ type, seq, len, raw + PKT_HDR_LEN };
    frames_ok++;
    onPacket(v);
  }

  HardwareSerial& ser;

  // Incoming COBS frame bytes (without delimiter)
  uint8_t rx_buf[UART_MAX_FRAME];
  size_t  rx_len = 0;

  // Decoded raw packet
  uint8_t dec_buf[UART_MAX_FRAME];

  uint32_t crc_fails = 0;
  uint32_t decode_fails = 0;
  uint32_t frames_ok = 0;
  uint32_t overflow_drops = 0;
};