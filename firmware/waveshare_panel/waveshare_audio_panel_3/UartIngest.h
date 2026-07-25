#pragma once

#include <Arduino.h>

/*
  UART JPEG ingest

  Die Implementierung wird schrittweise aus
  waveshare_audio_panel_3.ino hierher verschoben.
*/

// UART-Empfang und interne Puffer initialisieren.
void uartIngestBegin();

// True, sobald mindestens ein gültiges JPEG empfangen wurde.
bool uartIngestHasFrame();

// Länge des aktuell gespeicherten JPEG-Frames.
size_t uartIngestFrameLength();

// Aktuellen JPEG-Frame in einen neu reservierten Puffer kopieren.
//
// Bei Erfolg:
//   - Rückgabewert: true
//   - *copy enthält einen mit malloc() reservierten Puffer
//   - *length enthält dessen Länge
//
// Der Aufrufer muss den Puffer anschließend mit free() freigeben.
bool uartIngestCopyFrame(uint8_t** copy, size_t* length);
