# Erwin Intercom – Gegensprechanlage

Zwei-ESP-Architektur für eine moderne Gegensprechanlage.

## Architektur

| Gerät | Rolle | Aufgaben |
|-------|------|---------|
| **XIAO ESP32-S3 Sense** | Sensor / AI-Knoten | Kamera, Mikrofon, später Face-Recognition |
| **Waveshare ESP32-S3 DevKit-NxR8** | Gateway / Panel | Klingel, Audio-Ausgabe, Webserver, Streaming |

**Kommunikation** zwischen den beiden Boards:  
UART (TX/RX + GND) mit dem Protokoll **COBS + CRC32**  
Nachrichtentypen: `'J'`, `'A'`, `'E'`, `'C'`

## Einrichtung

### 1. Secrets anlegen

Das Projekt benötigt WiFi-Zugangsdaten (und ggf. weitere Secrets).

```bash
cp secrets.h.example secrets.h
