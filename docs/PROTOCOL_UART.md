# UART Protokoll (v1)

## Physik
- UART TX/RX + GND
- 3.3V Pegel (ESP32-S3 GPIOs sind nicht 5V tolerant)
- Kabel < 10 cm empfohlen

## UART Parameter
- Baud: 3,000,000
- 8N1
- Flow control: none
- Fallback: 2,000,000 wenn instabil

## Framing
UART ist Byte-Stream → wir definieren Nachrichten.

- Payload wird per **COBS** encodiert
- Frame endet mit **0x00** Delimiter
[COBS(payload)] 0x00


## Paketstruktur (vor COBS)

Type (1) | Seq (2 LE) | Len (2 LE) | Payload (Len) | CRC32 (4 LE)


CRC32 berechnet über: `Type + Seq + Len + Payload`

## Nachrichtentypen
- 'J' = JPEG Frame (Video)
- 'A' = Audio Chunk (PCM16 mono)
- 'E' = Event/Meta (UTF-8 JSON)
- 'C' = Control (UTF-8 JSON, Waveshare → XIAO)  [reserviert]

## Audio Format (v1)
- PCM16, mono
- 16 kHz
- Chunking: 20 ms = 320 Samples = 640 Bytes pro Paket
- Audio hat Priorität vor Video (Video darf droppen)

## Video (v1)
- JPEG Frames (z.B. QVGA)
- Bei Frames > UART_MAX_FRAME: "chunked JPEG" einführen:
  - z.B. Typ 'j' + Subheader (frame_id, chunk_idx, total, chunk_len)
  - oder 'J' Payload enthält eigenes Chunk-Header (v2)
  (wird beim Video-Step entschieden)

## Fehlerverhalten
Empfänger:
- COBS decode
- Längencheck
- CRC check
- Bei Fehler: Paket verwerfen (kein Retransmit)

## Priorität/Backpressure
XIAO sendet in Reihenfolge:
1) Audio
2) Events
3) Video (wenn Bandbreite übrig)