#include "globals.h"

// ─── NVS keys ─────────────────────────────────────────────────────────────────
const char* PREF_NAMESPACE = "wifi_config";
const char* SSID_KEY       = "ssid";
const char* PASSWORD_KEY   = "password";

// ─── Wi-Fi ────────────────────────────────────────────────────────────────────
Preferences preferences;
WebServer   server(80);
String      ssid     = "";
String      password = "";

// ─── OTA ──────────────────────────────────────────────────────────────────────
const char* otaHostname = "admin";
const char* otaPassword = "1234";

// ─── Timing ───────────────────────────────────────────────────────────────────
unsigned long lastWifiStatusPrint      = 0;
unsigned long lastWifiReconnectAttempt = 0;
const unsigned long WIFI_STATUS_PRINT_INTERVAL = 30000;
const unsigned long WIFI_RECONNECT_INTERVAL    = 10000;

// ─── Connection state ─────────────────────────────────────────────────────────
bool          isConnecting       = false;
bool          serverStarted      = false;
bool          otaStarted         = false;
unsigned long connectionStartTime = 0;
const unsigned long CONNECTION_TIMEOUT = 10000;

// ─── Main data structs ────────────────────────────────────────────────────────
StatusData      status;
SettingsData    settings;
StateData       state;
CalibrationData calibration;

// ─── Touch controller ─────────────────────────────────────────────────────────
TAMC_GT911 touchController = TAMC_GT911(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, TOUCH_WIDTH, TOUCH_HEIGHT);

// ─── Display ──────────────────────────────────────────────────────────────────
uint32_t     screenWidth  = 0;
uint32_t     screenHeight = 0;
uint32_t     bufSize      = 0;
lv_display_t *disp        = nullptr;
lv_color_t   *disp_draw_buf = nullptr;

// ─── UART buffer ──────────────────────────────────────────────────────────────
char receivedData[512];
int  dataIndex = 0;

// ─── Test timer ───────────────────────────────────────────────────────────────
unsigned long      lastTestRequest       = 0;
const unsigned long TEST_REQUEST_INTERVAL = 3000;

// ─── Web error flags ──────────────────────────────────────────────────────────
String webErrorMessage = "";
bool   hasWebError     = false;

// ─── Response state machine ───────────────────────────────────────────────────
String       ResponseTypeString;
ResponseType waitingForResponse  = RESPONSE_NONE;
unsigned long responseRequestTime = 0;
int          responseRetryCount   = 0;
const int    MAX_RETRIES          = 5;
const unsigned long RETRY_INTERVAL = 100;

SettingsData pendingSettings;
StateData    pendingState;
