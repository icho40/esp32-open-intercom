#include <Arduino.h>

#define UART_PORT UART_NUM_1
#define PIN_UART_RX 18

void setup() {
  Serial.begin(115200);
  Serial1.begin(500000, SERIAL_8N1, PIN_UART_RX, -1);

  Serial.println("WAVESHARE START");
}

void loop() {
  while (Serial1.available()) {
    char c = Serial1.read();
    Serial.print(c);
  }
}