
# esp32-open-intercom
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

Kopiere die Vorlage:
cp secrets.h.example secrets.h

Öffne danach secrets.h und trage deine Werte ein:

#define WIFI_SSID       "MeinWLAN"
#define WIFI_PASSWORD   "MeinPasswort"

Wichtig: secrets.h ist in .gitignore und wird nicht ins Repository committed.

### 2. Dokumentation

- ARCHITECTURE.md – Gesamtarchitektur
- PROTOCOL_UART.md – UART-Protokoll (COBS + CRC32)
- PINOUT.md – Pinbelegung beider Boards

## Lizenz

Siehe LICENSE
