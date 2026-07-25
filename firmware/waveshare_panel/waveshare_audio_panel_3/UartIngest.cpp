#include "UartIngest.h"

#include <Arduino.h>
#include "driver/uart.h"

#include "AppState.h"

namespace {

constexpr uart_port_t UART_PORT = UART_NUM_1;
constexpr int PIN_UART_RX = 18;
constexpr int UART_BAUD = 500000;

constexpr size_t JPEG_MAX = 120 * 1024;
constexpr size_t PACKET_BUFFER_SIZE = JPEG_MAX + 256;

uint8_t* gJpeg = nullptr;
size_t gLen = 0;
portMUX_TYPE frameMux = portMUX_INITIALIZER_UNLOCKED;

uint8_t* encBuf = nullptr;
uint8_t* decBuf = nullptr;
size_t pktLen = 0;

struct __attribute__((packed)) PacketHeader {
  uint8_t type;
  uint16_t seq;
  uint32_t ts_ms;
  uint32_t payload_len;
};

size_t cobsDecode(const uint8_t* input, size_t length, uint8_t* output) {
  const uint8_t* end = input + length;
  size_t outputLength = 0;

  while (input < end) {
    const uint8_t code = *input++;

    if (code == 0) {
      return 0;
    }

    for (uint8_t i = 1; i < code; ++i) {
      if (input >= end) {
        return 0;
      }

      output[outputLength++] = *input++;
    }

    if (code < 0xFF && input < end) {
      output[outputLength++] = 0x00;
    }
  }

  return outputLength;
}

void setFrame(const uint8_t* source, size_t length) {
  uint8_t* newFrame = static_cast<uint8_t*>(ps_malloc(length));

  if (!newFrame) {
    Serial.printf("[UART] JPEG allocation failed: %u bytes\n",
                  static_cast<unsigned>(length));
    return;
  }

  memcpy(newFrame, source, length);

  taskENTER_CRITICAL(&frameMux);

  uint8_t* oldFrame = gJpeg;
  gJpeg = newFrame;
  gLen = length;

  taskEXIT_CRITICAL(&frameMux);

  if (oldFrame) {
    free(oldFrame);
  }
}

void uartTask(void*) {
  uart_config_t config = {
    .baud_rate = UART_BAUD,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .rx_flow_ctrl_thresh = 0,
    .source_clk = UART_SCLK_APB
  };

  uart_param_config(UART_PORT, &config);

  uart_set_pin(
    UART_PORT,
    UART_PIN_NO_CHANGE,
    PIN_UART_RX,
    UART_PIN_NO_CHANGE,
    UART_PIN_NO_CHANGE
  );

  uart_driver_install(UART_PORT, 16384, 0, 0, nullptr, 0);

  uint8_t rx[1024];

  for (;;) {
    const int received = uart_read_bytes(
      UART_PORT,
      rx,
      sizeof(rx),
      20 / portTICK_PERIOD_MS
    );

    if (received <= 0) {
      continue;
    }

    for (int i = 0; i < received; ++i) {
      const uint8_t byteValue = rx[i];

      appStateAddReceivedBytes(1);

      if (byteValue == 0x00) {
        const size_t decodedLength =
          cobsDecode(encBuf, pktLen, decBuf);

        pktLen = 0;

        if (decodedLength < sizeof(PacketHeader)) {
          continue;
        }

        PacketHeader header;
        memcpy(&header, decBuf, sizeof(header));

        const size_t requiredLength =
          sizeof(header) + header.payload_len;

        if (
          header.type == 0x01 &&
          requiredLength == decodedLength &&
          header.payload_len > 0 &&
          header.payload_len <= JPEG_MAX
        ) {
          setFrame(
            decBuf + sizeof(header),
            header.payload_len
          );

          const uint32_t frames = appStateIncrementFrames();

          if ((frames % 30) == 0) {
            Serial.printf(
              "[FRAME] frames=%u jpeg=%u\n",
              static_cast<unsigned>(frames),
              static_cast<unsigned>(uartIngestFrameLength())
            );
          }
        } else {
          Serial.printf(
            "[DROP] dec=%u type=%02X len=%u need=%u\n",
            static_cast<unsigned>(decodedLength),
            static_cast<unsigned>(header.type),
            static_cast<unsigned>(header.payload_len),
            static_cast<unsigned>(requiredLength)
          );
        }
      } else {
        if (pktLen < PACKET_BUFFER_SIZE) {
          encBuf[pktLen++] = byteValue;
        } else {
          pktLen = 0;
        }
      }
    }
  }
}

}  // namespace

void uartIngestBegin() {
  encBuf = static_cast<uint8_t*>(ps_malloc(PACKET_BUFFER_SIZE));
  decBuf = static_cast<uint8_t*>(ps_malloc(PACKET_BUFFER_SIZE));

  if (!encBuf || !decBuf) {
    Serial.println("[UART] buffer alloc failed");

    if (encBuf) {
      free(encBuf);
      encBuf = nullptr;
    }

    if (decBuf) {
      free(decBuf);
      decBuf = nullptr;
    }

    while (true) {
      delay(1000);
    }
  }

  xTaskCreatePinnedToCore(
    uartTask,
    "uart",
    4096,
    nullptr,
    1,
    nullptr,
    1
  );
}

bool uartIngestHasFrame() {
  taskENTER_CRITICAL(&frameMux);
  const bool hasFrame = gJpeg != nullptr && gLen > 0;
  taskEXIT_CRITICAL(&frameMux);

  return hasFrame;
}

size_t uartIngestFrameLength() {
  taskENTER_CRITICAL(&frameMux);
  const size_t length = gLen;
  taskEXIT_CRITICAL(&frameMux);

  return length;
}

bool uartIngestCopyFrame(uint8_t** copy, size_t* length) {
  if (!copy || !length) {
    return false;
  }

  *copy = nullptr;
  *length = 0;

  taskENTER_CRITICAL(&frameMux);

  if (!gJpeg || gLen == 0) {
    taskEXIT_CRITICAL(&frameMux);
    return false;
  }

  const size_t currentLength = gLen;
  uint8_t* buffer = static_cast<uint8_t*>(malloc(currentLength));

  if (buffer) {
    memcpy(buffer, gJpeg, currentLength);
  }

  taskEXIT_CRITICAL(&frameMux);

  if (!buffer) {
    return false;
  }

  *copy = buffer;
  *length = currentLength;
  return true;
}
