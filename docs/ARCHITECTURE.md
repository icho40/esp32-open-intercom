# Architektur (v1)

## Ziel
- **Ein Kameramodul (XIAO)** liefert Video + Audio (+ später KI/Face-Recognition).
- **Ein Gateway/Panel (Waveshare)** übernimmt Klingeln (3 Parties), Webserver/Streaming, Audio-Ausgabe und Zustandslogik.

Video ist **immer dieselbe Kamera**.  
"Party" ist primär für **Klingeln/Audio/Call-State**.

---

## Rollen

### XIAO ESP32-S3 Sense (Sensor/AI-Knoten)
**Verantwortung**
- Kamera erfassen (JPEG)
- Mikrofon erfassen (PCM16)
- später: Face-Recognition / Motion / Person-ID
- Datenpakete per UART senden

**Nicht-Aufgaben**
- kein Webserver
- keine Party-Zustände
- keine Clientverwaltung

**Outputs (UART)**
- 'J' JPEG (ggf. in Chunks)
- 'A' Audio Chunks (PCM16)
- 'E' Events/Meta (JSON)

---

### Waveshare ESP32-S3 DevKit-NxR8 (Gateway/Panel)
**Verantwortung**
- Klingel GPIOs (Party 1..3)
- Call-State (Ringing / Idle / Talk)
- Webserver (HTTP/MJPEG/Audio)
- Audio-Ausgabe (MAX98357A)
- UART Empfang: Video/Audio/Events puffern und an Clients ausliefern
- später: Gegensprechen (Tablet → Waveshare → XIAO)

---

## Ownership / Datenhaltung auf Waveshare

### VideoStore
- Hält "latest JPEG"
- Quelle: XIAO UART 'J'
- Consumer: MJPEG HTTP Responses
- Policy: Video darf droppen, Frame wird ersetzt

### AudioStore
- Ringbuffer PCM16 (oder später komprimiert)
- Quelle: XIAO UART 'A'
- Consumer: Audio Streaming/Playback
- Policy: Audio soll kontinuierlich bleiben, ggf. Video drosseln

### PartyStore
- 3 Parties: State, since, talkActive
- Quelle: Klingel GPIOs + (optional) Events
- Consumer: Web UI / Ringing / Call control

---

## Datenfluss

XIAO:
- Camera -> 'J' -> UART -> Waveshare VideoStore -> MJPEG to Tablets
- Mic    -> 'A' -> UART -> Waveshare AudioStore -> Stream/Playback
- AI     -> 'E' -> UART -> Waveshare UI/State -> Aktionen

---

## Gegensprechen (v2 Reserve)
UART wird vollduplex genutzt.

Zusätzlicher Nachrichtentyp:
- 'C' = Control (Waveshare → XIAO)

Beispiele:
- {"talk":true}
- {"mic":false}
- {"set_fps":8}
- {"jpeg_q":18}

Optional später: Audio-Rückrichtung als 'A' in Gegenrichtung oder eigener Typ (z.B. 'T' talk-audio).

---

## Entwicklungsphasen
1. UART Link stabil (nur 'E' Events) ✅
2. Audio XIAO→Waveshare ('A')
3. Video XIAO→Waveshare ('J' ggf. chunked)
4. Events (Face/Motion) ('E')
5. Gegensprechen: Control + Rückkanal