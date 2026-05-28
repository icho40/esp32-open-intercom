#pragma once
#include <Arduino.h>

// ===== Protocol constants =====
static constexpr uint8_t UART_FRAME_DELIM = 0x00;
static constexpr size_t  UART_MAX_PAYLOAD = 65535;    // len is uint16
static constexpr size_t  UART_MAX_FRAME   = 8192;     // adjust (must fit your biggest COBS frame chunk)

// Message types
static constexpr uint8_t PKT_JPEG  = 'J';
static constexpr uint8_t PKT_AUDIO = 'A';
static constexpr uint8_t PKT_EVENT = 'E';

// Header is: type(1), seq(2 LE), len(2 LE)
static constexpr size_t PKT_HDR_LEN = 1 + 2 + 2;
static constexpr size_t PKT_CRC_LEN = 4;

// -------- CRC32 (IEEE 802.3) --------
static inline uint32_t crc32_ieee(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      uint32_t mask = -(crc & 1u);
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return ~crc;
}

static inline void u16le_write(uint8_t* p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
}
static inline uint16_t u16le_read(const uint8_t* p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static inline void u32le_write(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)((v >> 24) & 0xFF);
}
static inline uint32_t u32le_read(const uint8_t* p) {
  return (uint32_t)p[0]
       | ((uint32_t)p[1] << 8)
       | ((uint32_t)p[2] << 16)
       | ((uint32_t)p[3] << 24);
}

// -------- COBS encode/decode --------
// Returns encoded length (no delimiter added).
// out must have enough space: worst case len + len/254 + 1
static inline size_t cobs_encode(const uint8_t* in, size_t len, uint8_t* out, size_t out_cap) {
  if (out_cap == 0) return 0;

  size_t read_index = 0;
  size_t write_index = 1;     // reserve code position
  size_t code_index = 0;
  uint8_t code = 1;

  while (read_index < len) {
    if (in[read_index] == 0) {
      if (code_index >= out_cap) return 0;
      out[code_index] = code;
      code = 1;
      code_index = write_index++;
      if (write_index > out_cap) return 0;
      read_index++;
    } else {
      if (write_index >= out_cap) return 0;
      out[write_index++] = in[read_index++];
      code++;
      if (code == 0xFF) {
        if (code_index >= out_cap) return 0;
        out[code_index] = code;
        code = 1;
        code_index = write_index++;
        if (write_index > out_cap) return 0;
      }
    }
  }

  if (code_index >= out_cap) return 0;
  out[code_index] = code;
  return write_index;
}

// Returns decoded length; 0 on failure.
static inline size_t cobs_decode(const uint8_t* in, size_t len, uint8_t* out, size_t out_cap) {
  size_t read_index = 0;
  size_t write_index = 0;

  while (read_index < len) {
    uint8_t code = in[read_index];
    if (code == 0) return 0;
    read_index++;

    for (uint8_t i = 1; i < code; i++) {
      if (read_index >= len) return 0;
      if (write_index >= out_cap) return 0;
      out[write_index++] = in[read_index++];
    }

    if (code != 0xFF && read_index < len) {
      if (write_index >= out_cap) return 0;
      out[write_index++] = 0;
    }
  }

  return write_index;
}