#pragma once
#include <Arduino.h>

// ─── Leveled logger ───────────────────────────────────────────────────────────
// LOG_LEVEL is a compile-time constant, so the compiler strips disabled
// branches entirely — zero runtime cost and nothing to drown the UART hot path.
// Levels:
//   0 = OFF     — silent
//   1 = WARN    — anomalies only: parse failures, retries, timeouts, give-ups
//   2 = INFO    — WARN + lifecycle: sends, band/PTT/amp changes, WiFi/OTA (default)
//   3 = TRACE   — INFO + firehose: raw UART lines, per-object parse, full dumps
// IMPORTANT: the first argument is a printf format string. Never pass an
// untrusted buffer (e.g. receivedData) as the format — use LOG_x("%s", buf).
#define LOG_LEVEL   2

#define LOG_WARN(fmt, ...)  do { if (LOG_LEVEL >= 1) Serial.printf("[WARN] "  fmt "\n", ##__VA_ARGS__); } while (0)
#define LOG_INFO(fmt, ...)  do { if (LOG_LEVEL >= 2) Serial.printf("[INFO] "  fmt "\n", ##__VA_ARGS__); } while (0)
#define LOG_TRACE(fmt, ...) do { if (LOG_LEVEL >= 3) Serial.printf("[TRACE] " fmt "\n", ##__VA_ARGS__); } while (0)
