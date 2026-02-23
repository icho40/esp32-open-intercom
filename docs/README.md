# Gegensprechanlage Erwin

Zwei-ESP Architektur:

- **XIAO ESP32-S3 Sense**: Kamera + Mikrofon + später Face-Recognition (Sensor/AI-Knoten)
- **Waveshare ESP32-S3 DevKit-NxR8**: Klingel/Parties, Audio-Ausgabe, Webserver/Streaming (Gateway/Panel)

Kommunikation zwischen XIAO und Waveshare: UART (TX/RX + GND), Protokoll: COBS + CRC32 + Typen 'J'/'A'/'E'/'C'.

Dokumentation:
- docs/ARCHITECTURE.md
- docs/PROTOCOL_UART.md
- docs/PINOUT.md