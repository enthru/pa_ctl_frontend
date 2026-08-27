#pragma once
#include "globals.h"

// ─── Send commands ────────────────────────────────────────────────────────────
void sendStatusRequest();
void sendSettingsCommand();
void sendSettingsData();
// trackResponse=false sends without arming the ack/retry state machine
// (used when another tracked send follows immediately in the same operation).
void sendStateData(bool trackResponse = true);
// Pushes ONLY the debug flag. Unlike sendStateData() this touches no other key, so it
// is safe to send while keyed — see the comment on the definition.
void sendDebugState();
void sendCalibrationCommand();
void sendCalibrationData();

// ─── Request + blocking wait ──────────────────────────────────────────────────
bool requestAndWaitForSettings(unsigned long timeout = 1000);
bool requestAndWaitForCalibration(unsigned long timeout = 1000);

// ─── Backend-access gating ────────────────────────────────────────────────────
// Entry points that send to the backend must check these before doing so.
bool backendBusy();      // a transaction is outstanding
bool backendDeaf();      // backend is mid EEPROM write (settings/calibration send)
bool isTransmitting();   // PTT is keyed — no configuration traffic allowed

// ─── UART receive loop ────────────────────────────────────────────────────────
void handleUARTData();

// ─── Retry state machine ──────────────────────────────────────────────────────
void handleResponseRetry();

// ─── Test auto-requests (TEST_UART mode) ─────────────────────────────────────
void handleTestRequests();

// ─── Debug helpers ────────────────────────────────────────────────────────────
void debugSettingsData();
void debugCalibrationData();
