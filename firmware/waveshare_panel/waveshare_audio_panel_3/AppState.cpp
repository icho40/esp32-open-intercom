#include "AppState.h"

namespace {
constexpr uint8_t PIN_BTN_P1 = 4;
constexpr uint8_t PIN_BTN_P2 = 5;
constexpr uint8_t PIN_BTN_P3 = 6;

constexpr uint32_t RING_TIMEOUT_MS = 30U * 1000U;
constexpr uint32_t BTN_DEBOUNCE_MS = 120U;

volatile CallState stateP1 = CS_IDLE;
volatile CallState stateP2 = CS_IDLE;
volatile CallState stateP3 = CS_IDLE;

uint32_t ringSinceP1 = 0;
uint32_t ringSinceP2 = 0;
uint32_t ringSinceP3 = 0;

uint32_t receivedBytes = 0;
uint32_t receivedFrames = 0;

volatile CallState* stateForParty(uint8_t party) {
  switch (party) {
    case 1: return &stateP1;
    case 2: return &stateP2;
    case 3: return &stateP3;
    default: return nullptr;
  }
}

uint32_t* ringSinceForParty(uint8_t party) {
  switch (party) {
    case 1: return &ringSinceP1;
    case 2: return &ringSinceP2;
    case 3: return &ringSinceP3;
    default: return nullptr;
  }
}
}  // namespace

void appStateBegin() {
  pinMode(PIN_BTN_P1, INPUT_PULLUP);
  pinMode(PIN_BTN_P2, INPUT_PULLUP);
  pinMode(PIN_BTN_P3, INPUT_PULLUP);
}

void appStateRingParty(uint8_t party) {
  volatile CallState* state = stateForParty(party);
  uint32_t* ringSince = ringSinceForParty(party);

  if (!state || !ringSince) return;

  *state = CS_RING;
  *ringSince = millis();
  Serial.printf("[RING] P%u\n", party);
}

bool appStateAcknowledgeParty(uint8_t party) {
  volatile CallState* state = stateForParty(party);
  if (!state) return false;

  *state = CS_IDLE;
  return true;
}

CallState appStateGetParty(uint8_t party) {
  volatile CallState* state = stateForParty(party);
  return state ? *state : CS_IDLE;
}

const char* appStateGetPartyText(uint8_t party) {
  return appStateGetParty(party) == CS_RING ? "ring" : "idle";
}

void appStateHandleButtons() {
  static bool lastP1 = HIGH;
  static bool lastP2 = HIGH;
  static bool lastP3 = HIGH;
  static uint32_t lastMs = 0;

  const uint32_t nowMs = millis();
  if (nowMs - lastMs < BTN_DEBOUNCE_MS) return;
  lastMs = nowMs;

  const bool nowP1 = digitalRead(PIN_BTN_P1);
  const bool nowP2 = digitalRead(PIN_BTN_P2);
  const bool nowP3 = digitalRead(PIN_BTN_P3);

  if (lastP1 == HIGH && nowP1 == LOW) appStateRingParty(1);
  if (lastP2 == HIGH && nowP2 == LOW) appStateRingParty(2);
  if (lastP3 == HIGH && nowP3 == LOW) appStateRingParty(3);

  lastP1 = nowP1;
  lastP2 = nowP2;
  lastP3 = nowP3;
}

void appStateHandleTimeouts() {
  const uint32_t now = millis();

  if (stateP1 == CS_RING && now - ringSinceP1 > RING_TIMEOUT_MS) stateP1 = CS_IDLE;
  if (stateP2 == CS_RING && now - ringSinceP2 > RING_TIMEOUT_MS) stateP2 = CS_IDLE;
  if (stateP3 == CS_RING && now - ringSinceP3 > RING_TIMEOUT_MS) stateP3 = CS_IDLE;
}

void appStateAddReceivedBytes(uint32_t count) {
  receivedBytes += count;
}

uint32_t appStateGetReceivedBytes() {
  return receivedBytes;
}

uint32_t appStateIncrementFrames() {
  return ++receivedFrames;
}

uint32_t appStateGetFrames() {
  return receivedFrames;
}
