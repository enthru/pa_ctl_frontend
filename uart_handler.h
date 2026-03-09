#pragma once
#include "globals.h"

// ─── Send commands ────────────────────────────────────────────────────────────
void sendStatusRequest();
void sendSettingsCommand();
void sendSettingsData();
void sendStateData();
void sendCalibrationCommand();
void sendCalibrationData();

// ─── Request + blocking wait ──────────────────────────────────────────────────
bool requestAndWaitForSettings(unsigned long timeout = 1000);
bool requestAndWaitForCalibration(unsigned long timeout = 1000);

// ─── UART receive loop ────────────────────────────────────────────────────────
void handleUARTData();

// ─── Retry state machine ──────────────────────────────────────────────────────
void handleResponseRetry();

// ─── Test auto-requests (TEST_UART mode) ─────────────────────────────────────
void handleTestRequests();

// ─── Debug helpers ────────────────────────────────────────────────────────────
void debugStatusData();
void debugSettingsData();
void debugCalibrationData();
