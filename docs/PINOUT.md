# Pinout / Wiring (Stand Feb 2026)

## Waveshare ESP32-S3 DevKit-NxR8
### Belegt
- GPIO4, GPIO5, GPIO6: Klingeln (Party 1..3)
- GPIO8, GPIO9: MAX98357A (Audio AMP)  (weitere I2S Pins je nach Verdrahtung)

### UART Link zu XIAO (aktueller Stand)
- RX = GPIO12
- TX = GPIO13

## XIAO ESP32-S3 Sense
### UART Link zu Waveshare
- TX = D6 (GPIO43)
- RX = D7 (GPIO44)

## Kabelverbindung (UART)
- XIAO TX (GPIO43 / D6)  → Waveshare RX (GPIO12)
- XIAO RX (GPIO44 / D7)  ← Waveshare TX (GPIO13)
- GND ↔ GND

## Pegel / Versorgung
- UART Signale: 3.3V
- GPIOs nicht 5V tolerant
- Boards separat versorgen (USB), keine 3V3/5V zwischen den Boards verbinden (nur GND gemeinsam)