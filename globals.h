#pragma once
// IMPORTANT: Do NOT include PINS_JC4827W543.h here.
// It defines gfx/bus/GFX_BL and must be included only in main.cpp.
#include <lvgl.h>
#include <ui.h>
#include "TAMC_GT911.h"
#include "JSONParser.h"
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <WebServer.h>

// ─── Debug flags ──────────────────────────────────────────────────────────────
#define DEBUG       0
#define TEST_UART   0

// ─── NVS keys ─────────────────────────────────────────────────────────────────
extern const char* PREF_NAMESPACE;
extern const char* SSID_KEY;
extern const char* PASSWORD_KEY;

// ─── Wi-Fi ────────────────────────────────────────────────────────────────────
extern Preferences preferences;
extern WebServer   server;
extern String      ssid;
extern String      password;

// ─── OTA ──────────────────────────────────────────────────────────────────────
extern const char* otaHostname;
extern const char* otaPassword;

// ─── Timing ───────────────────────────────────────────────────────────────────
extern unsigned long lastWifiStatusPrint;
extern unsigned long lastWifiReconnectAttempt;
extern const unsigned long WIFI_STATUS_PRINT_INTERVAL;
extern const unsigned long WIFI_RECONNECT_INTERVAL;

// ─── Connection state ─────────────────────────────────────────────────────────
extern bool          isConnecting;
extern bool          serverStarted;
extern bool          otaStarted;
extern unsigned long connectionStartTime;
extern const unsigned long CONNECTION_TIMEOUT;

// ─── Main data structs ────────────────────────────────────────────────────────
extern StatusData      status;
extern SettingsData    settings;
extern StateData       state;
extern CalibrationData calibration;

// ─── Touch controller ─────────────────────────────────────────────────────────
#define TOUCH_SDA    8
#define TOUCH_SCL    4
#define TOUCH_INT    3
#define TOUCH_RST   38
#define TOUCH_WIDTH  480
#define TOUCH_HEIGHT 272
extern TAMC_GT911 touchController;

// ─── Display ──────────────────────────────────────────────────────────────────
extern uint32_t     screenWidth;
extern uint32_t     screenHeight;
extern uint32_t     bufSize;
extern lv_display_t *disp;
extern lv_color_t   *disp_draw_buf;

// ─── UART buffer ──────────────────────────────────────────────────────────────
extern char receivedData[512];
extern int  dataIndex;

// ─── Test timer ───────────────────────────────────────────────────────────────
extern unsigned long lastTestRequest;
extern const unsigned long TEST_REQUEST_INTERVAL;

// ─── Web error flags ──────────────────────────────────────────────────────────
extern String webErrorMessage;
extern bool   hasWebError;

// ─── Response state machine ───────────────────────────────────────────────────
enum ResponseType {
    RESPONSE_NONE,
    RESPONSE_SETTINGS_REQUEST,
    RESPONSE_SETTINGS_SEND,
    RESPONSE_STATE_SEND,
    RESPONSE_CALIBRATION_REQUEST,
    RESPONSE_CALIBRATION_SEND
};

extern String       ResponseTypeString;
extern ResponseType waitingForResponse;
extern unsigned long responseRequestTime;
extern int          responseRetryCount;
extern const int    MAX_RETRIES;
extern const unsigned long RETRY_INTERVAL;

extern SettingsData pendingSettings;
extern StateData    pendingState;

// ─── LVGL screen references ───────────────────────────────────────────────────
extern lv_obj_t* ui_main;
extern lv_obj_t* ui_menu;
extern lv_obj_t* ui_protection;
extern lv_obj_t* ui_wifi;
extern lv_obj_t* ui_mainLeft;
extern lv_obj_t* ui_bands;
extern lv_obj_t* ui_mainRight;
extern lv_obj_t* ui_warning;
extern lv_obj_t* ui_protection2;
