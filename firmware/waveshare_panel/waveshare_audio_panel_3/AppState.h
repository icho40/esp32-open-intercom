#ifndef APP_STATE_H
#define APP_STATE_H

#include <Arduino.h>

enum CallState : uint8_t {
  CS_IDLE = 0,
  CS_RING = 1
};

void appStateBegin();
void appStateHandleButtons();
void appStateHandleTimeouts();

void appStateRingParty(uint8_t party);
bool appStateAcknowledgeParty(uint8_t party);
CallState appStateGetParty(uint8_t party);
const char* appStateGetPartyText(uint8_t party);

void appStateAddReceivedBytes(uint32_t count);
uint32_t appStateGetReceivedBytes();

uint32_t appStateIncrementFrames();
uint32_t appStateGetFrames();

#endif
