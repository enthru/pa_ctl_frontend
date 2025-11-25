#include <lvgl.h>
#include <PINS_JC4827W543.h>
#include <ui.h>
#include "TAMC_GT911.h"
#include "JSONParser.h"
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <WebServer.h>

Preferences preferences;
WebServer server(80);

// Constants for NVS keys
const char* PREF_NAMESPACE = "wifi_config";
const char* SSID_KEY = "ssid";
const char* PASSWORD_KEY = "password";

// Wi-Fi credentials
String ssid = "";
String password = "";

// OTA
const char* otaHostname = "admin";
const char* otaPassword = "1234";

// Timing variables
unsigned long lastWifiStatusPrint = 0;
unsigned long lastWifiReconnectAttempt = 0;
const unsigned long WIFI_STATUS_PRINT_INTERVAL = 30000;
const unsigned long WIFI_RECONNECT_INTERVAL = 10000;

// Connection state
bool isConnecting = false;
// handle to not start web before wi-fi connected
bool serverStarted = false;
unsigned long connectionStartTime = 0;
const unsigned long CONNECTION_TIMEOUT = 10000;

// main data structs
StatusData status;
SettingsData settings; 
StateData state;

#define DEBUG 0
#define TEST_UART 0

// Touch Controller
#define TOUCH_SDA 8
#define TOUCH_SCL 4
#define TOUCH_INT 3
#define TOUCH_RST 38
#define TOUCH_WIDTH 480
#define TOUCH_HEIGHT 272
TAMC_GT911 touchController = TAMC_GT911(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, TOUCH_WIDTH, TOUCH_HEIGHT);

// Display global variables
uint32_t screenWidth;
uint32_t screenHeight;
uint32_t bufSize;
lv_display_t *disp;
lv_color_t *disp_draw_buf;

char receivedData[512];
int dataIndex = 0;

unsigned long lastTestRequest = 0;
const unsigned long TEST_REQUEST_INTERVAL = 3000;

enum ResponseType {
  RESPONSE_NONE,
  RESPONSE_SETTINGS_REQUEST,
  RESPONSE_SETTINGS_SEND,
  RESPONSE_STATE_SEND
};

ResponseType waitingForResponse = RESPONSE_NONE;
unsigned long responseRequestTime = 0;
int responseRetryCount = 0;
const int MAX_RETRIES = 5;
const unsigned long RETRY_INTERVAL = 100;

SettingsData pendingSettings;
StateData pendingState;

// prototypes
void debugStatusData();
void debugSettingsData();
bool parseAckResponse(const char* json, ResponseType expectedResponse);
void sendSettingsCommand();
void sendStatusRequest();
void sendSettingsData();
void sendStateData();
void handleResponseRetry();
void handleTestRequests();
void set_switch_state(lv_obj_t *sw, bool state);
void processParsedData();
void processSettingsData();
void processAckResponse(ResponseType expectedResponse);
void handleUARTData();
void set_band(lv_event_t * e);
void setupLVGLButtonHandler();
static void saveWiFiSettingsEventHandler(lv_event_t* e);
void loadWiFiCredentials();
void saveWiFiCredentials(const char* newSSID, const char* newPassword);
void connectToWiFi();
void printWiFiStatus();
void updateLVGLTextAreasWithSavedCredentials();
void mainRightLoaded(lv_event_t * e);
void protectionOpened(lv_event_t * e);
void resetAlert(lv_event_t * e);
bool requestAndWaitForSettings(unsigned long timeout = 1000);
void toggleAutoBand(lv_event_t * e);
void saveSettings(lv_event_t * e);

//////////////////////////////////////////// WEB ////////////////////////////////////////////////////////

int webWaitingForResponse = 0;
const int WEB_RESPONSE_NONE = 0;

// main page
void handleRoot() {
    String html = R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
        <title>Status</title>
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <link rel="stylesheet" href="/style.css">
    </head>
    <body>
        <div class="container">
            <h1>Amplifier Status</h1>
            <div class="status-grid">
                <div class="status-item">
                    <label>Power:</label>
                    <span id="pwr">0W</span>
                    <div class="bar"><div id="pwrBar" class="bar-fill"></div></div>
                </div>
                <div class="status-item">
                    <label>SWR:</label>
                    <span id="swr">0</span>
                </div>
                <div class="status-item">
                    <label>Reflected:</label>
                    <span id="ref">0W</span>
                </div>
                <div class="status-item">
                    <label>Voltage:</label>
                    <span id="vol">0V</span>
                </div>
                <div class="status-item">
                    <label>Current:</label>
                    <span id="cur">0A</span>
                </div>
                <div class="status-item">
                    <label>Water Temp:</label>
                    <span id="waterTmp">0C</span>
                </div>
                <div class="status-item">
                    <label>Plate Temp:</label>
                    <span id="plateTmp">0C</span>
                </div>
                <div class="status-item">
                    <label>State:</label>
                    <div class="toggle-switch">
                        <input type="checkbox" id="stateToggle" class="toggle-input">
                        <label for="stateToggle" class="toggle-label"></label>
                    </div>
                </div>
                <div class="status-item">
                    <label>Band:</label>
                    <span id="band">-</span>
                </div>
                <div class="status-item">
                    <label>PTT:</label>
                    <span id="ptt">OFF</span>
                </div>
            </div>
            <div class="navigation">
                <button onclick="location.href='/band'">Band Selection</button>
                <button onclick="location.href='/settings'">Settings</button>
            </div>
        </div>
        <script>
function updateStatus() {
    fetch('/status')
        .then(response => response.json())
        .then(data => {
            // progress bar
            const powerValue = data.fwd || 0;
            document.getElementById('pwr').textContent = powerValue + 'W';
            
            // progress bar maximum
            const powerPercent = Math.min(powerValue / 12, 100); // 1200W = 100%
            document.getElementById('pwrBar').style.width = powerPercent + '%';
            
            // updates
            document.getElementById('swr').textContent = data.swr || 0;
            document.getElementById('ref').textContent = (data.ref || 0) + 'W';
            document.getElementById('vol').textContent = (data.voltage || 0).toFixed(1) + 'V';
            document.getElementById('cur').textContent = (data.current || 0).toFixed(1) + 'A';
            document.getElementById('waterTmp').textContent = (data.water_temp || 0).toFixed(1) + 'C';
            document.getElementById('plateTmp').textContent = (data.plate_temp || 0).toFixed(1) + 'C';
            document.getElementById('band').textContent = data.band || '-';
            document.getElementById('ptt').textContent = data.ptt ? 'ON' : 'OFF';
            
            const stateToggle = document.getElementById('stateToggle');
            stateToggle.checked = data.state || false;
            
            // red color while PTT
            const pwrBar = document.getElementById('pwrBar');
            if(data.ptt) {
                pwrBar.style.backgroundColor = '#FF0000';
            } else {
                pwrBar.style.backgroundColor = '#007AFF';
            }
        });
}
            
            document.getElementById('stateToggle').addEventListener('change', function() {
                const newState = this.checked;
                fetch('/setstate', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/x-www-form-urlencoded',
                    },
                    body: 'state=' + newState
                })
                .then(response => response.text())
                .then(data => {
                    console.log('State updated to: ' + newState);
                })
                .catch(error => {
                    console.error('Error updating state:', error);
                });
            });
            
            setInterval(updateStatus, 500);
            updateStatus();
        </script>
    </body>
    </html>
    )rawliteral";
    
    server.send(200, "text/html", html);
}

void handleStatus() {
    if (waitingForResponse != RESPONSE_NONE) {
        server.send(503, "application/json", "{\"error\":\"Busy\"}");
        return;
    }
    
    String response = "{";
    response += "\"fwd\":" + String(status.fwd) + ",";
    response += "\"ref\":" + String(status.ref) + ",";
    response += "\"swr\":" + String(status.swr) + ",";
    response += "\"voltage\":" + String(status.voltage, 1) + ",";
    response += "\"current\":" + String(status.current, 1) + ",";
    response += "\"water_temp\":" + String(status.water_temp, 1) + ",";
    response += "\"plate_temp\":" + String(status.plate_temp, 1) + ",";
    response += "\"ptt\":" + String(status.ptt ? "true" : "false") + ",";
    response += "\"state\":" + String(status.state ? "true" : "false") + ",";
    response += "\"band\":\"" + String(status.band) + "\"";
    response += "}";
    
    server.send(200, "application/json", response);
}

// band page
void handleBandPage() {
    String html = R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
        <title>Band Selection</title>
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <link rel="stylesheet" href="/style.css">
    </head>
    <body>
        <div class="container">
            <h1>Band Selection</h1>
            <div class="band-grid">
                <button class="band-btn" onclick="setBand('160m')">160m</button>
                <button class="band-btn" onclick="setBand('80m')">80m</button>
                <button class="band-btn" onclick="setBand('60m')">60m</button>
                <button class="band-btn" onclick="setBand('40m')">40m</button>
                <button class="band-btn" onclick="setBand('30m')">30m</button>
                <button class="band-btn" onclick="setBand('20m')">20m</button>
                <button class="band-btn" onclick="setBand('17m')">17m</button>
                <button class="band-btn" onclick="setBand('15m')">15m</button>
                <button class="band-btn" onclick="setBand('12m')">12m</button>
                <button class="band-btn" onclick="setBand('10m')">10m</button>
                <button class="band-btn" onclick="setBand('6m')">6m</button>
            </div>
            <div class="navigation">
                <button onclick="location.href='/'">Back to Status</button>
                <span id="message" class="message"></span>
            </div>
        </div>
        <script>
            function setBand(band) {
                fetch('/setband', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/x-www-form-urlencoded',
                    },
                    body: 'band=' + band
                })
                .then(response => response.text())
                .then(data => {

                    document.getElementById('message').textContent = 'Band set to: ' + band + '. Redirecting...';
                    
                    setTimeout(() => {
                        window.location.href = '/';
                    }, 50);
                })
                .catch(error => {
                    document.getElementById('message').textContent = 'Error setting band';
                });
            }
        </script>
    </body>
    </html>
    )rawliteral";
    
    server.send(200, "text/html", html);
}

void handleSettingsPage() {
    String html = R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
        <title>Settings</title>
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <link rel="stylesheet" href="/style.css">
    </head>
    <body>
        <div class="container">
            <h1>Amplifier Settings</h1>
            <form id="settingsForm" action="/savesettings" method="POST">
                <div class="settings-grid">
                    <div class="settings-item">
                        <label>Max SWR:</label>
                        <input type="number" id="max_swr" name="max_swr" step="0.1" min="1" max="10" value="0">
                    </div>
                    <div class="settings-item">
                        <label>Max Current (A):</label>
                        <input type="number" id="max_current" name="max_current" step="0.1" min="1" max="50" value="0">
                    </div>
                    <div class="settings-item">
                        <label>Max Voltage (V):</label>
                        <input type="number" id="max_voltage" name="max_voltage" step="0.1" min="1" max="50" value="0">
                    </div>
                    <div class="settings-item">
                        <label>Max Plate Temp (°C):</label>
                        <input type="number" id="max_plate_temp" name="max_plate_temp" step="0.1" min="20" max="100" value="0">
                    </div>
                    <div class="settings-item">
                        <label>Max Water Temp (°C):</label>
                        <input type="number" id="max_water_temp" name="max_water_temp" step="0.1" min="20" max="80" value="0">
                    </div>
                    <div class="settings-item">
                        <label>Max Pump Speed Temp (°C):</label>
                        <input type="number" id="max_pump_speed_temp" name="max_pump_speed_temp" step="0.1" min="20" max="80" value="0">
                    </div>
                    <div class="settings-item">
                        <label>Min Pump Speed Temp (°C):</label>
                        <input type="number" id="min_pump_speed_temp" name="min_pump_speed_temp" step="0.1" min="10" max="60" value="0">
                    </div>
                    <div class="settings-item">
                        <label>Max Fan Speed Temp (°C):</label>
                        <input type="number" id="max_fan_speed_temp" name="max_fan_speed_temp" step="0.1" min="20" max="80" value="0">
                    </div>
                    <div class="settings-item">
                        <label>Min Fan Speed Temp (°C):</label>
                        <input type="number" id="min_fan_speed_temp" name="min_fan_speed_temp" step="0.1" min="10" max="60" value="0">
                    </div>
                    <div class="settings-item">
                        <label>Protection Enabled:</label>
                        <div class="toggle-switch">
                            <input type="checkbox" id="protection_enabled" name="protection_enabled" class="toggle-input">
                            <label for="protection_enabled" class="toggle-label"></label>
                        </div>
                    </div>
                    <div class="settings-item">
                        <label>Default Band:</label>
                        <select id="default_band" name="default_band">
                            <option value="160m">160m</option>
                            <option value="80m">80m</option>
                            <option value="60m">60m</option>
                            <option value="40m">40m</option>
                            <option value="30m">30m</option>
                            <option value="20m">20m</option>
                            <option value="17m">17m</option>
                            <option value="15m">15m</option>
                            <option value="12m">12m</option>
                            <option value="10m">10m</option>
                            <option value="6m">6m</option>
                        </select>
                    </div>
                    <div class="settings-item">
                        <label>Auto Band:</label>
                        <div class="toggle-switch">
                            <input type="checkbox" id="autoband" name="autoband" class="toggle-input">
                            <label for="autoband" class="toggle-label"></label>
                        </div>
                    </div>
                </div>
                <div class="navigation">
                    <button type="button" onclick="location.href='/'">Back to Status</button>
                    <button type="submit">Save Settings</button>
                    <span id="message" class="message"></span>
                </div>
            </form>
        </div>
        <script>
            function loadSettings() {
                fetch('/getsettings')
                    .then(response => {
                        if (!response.ok) {
                            throw new Error('Network response was not ok');
                        }
                        return response.json();
                    })
                    .then(data => {
                        document.getElementById('max_swr').value = data.max_swr;
                        document.getElementById('max_current').value = data.max_current;
                        document.getElementById('max_voltage').value = data.max_voltage;
                        document.getElementById('max_plate_temp').value = data.max_plate_temp;
                        document.getElementById('max_water_temp').value = data.max_water_temp;
                        document.getElementById('max_pump_speed_temp').value = data.max_pump_speed_temp;
                        document.getElementById('min_pump_speed_temp').value = data.min_pump_speed_temp;
                        document.getElementById('max_fan_speed_temp').value = data.max_fan_speed_temp;
                        document.getElementById('min_fan_speed_temp').value = data.min_fan_speed_temp;

                        document.getElementById('protection_enabled').checked = data.protection_enabled;
                        document.getElementById('autoband').checked = data.autoband;

                        const defaultBandSelect = document.getElementById('default_band');
                        for (let i = 0; i < defaultBandSelect.options.length; i++) {
                            if (defaultBandSelect.options[i].value === data.default_band) {
                                defaultBandSelect.selectedIndex = i;
                                break;
                            }
                        }
                        
                        console.log('Settings loaded successfully');
                    })
                    .catch(error => {
                        console.error('Error loading settings:', error);
                        document.getElementById('message').textContent = 'Error loading settings: ' + error.message;
                    });
            }

            document.addEventListener('DOMContentLoaded', loadSettings);

            document.getElementById('settingsForm').addEventListener('submit', function(e) {
                document.getElementById('message').textContent = 'Saving settings...';
            });

            setInterval(loadSettings, 30000);
        </script>
    </body>
    </html>
    )rawliteral";
    
    server.send(200, "text/html", html);
}

void handleGetSettings() {

    requestAndWaitForSettings(300);

    if (waitingForResponse != RESPONSE_NONE) {
        server.send(503, "application/json", "{\"error\":\"Busy\"}");
        return;
    }
    
    String response = "{";
    response += "\"max_swr\":" + String(settings.max_swr, 2) + ",";
    response += "\"max_current\":" + String(settings.max_current, 2) + ",";
    response += "\"max_voltage\":" + String(settings.max_voltage, 2) + ",";
    response += "\"max_plate_temp\":" + String(settings.max_plate_temp, 2) + ",";
    response += "\"max_water_temp\":" + String(settings.max_water_temp, 2) + ",";
    response += "\"max_pump_speed_temp\":" + String(settings.max_pump_speed_temp, 2) + ",";
    response += "\"min_pump_speed_temp\":" + String(settings.min_pump_speed_temp, 2) + ",";
    response += "\"max_fan_speed_temp\":" + String(settings.max_fan_speed_temp, 2) + ",";
    response += "\"min_fan_speed_temp\":" + String(settings.min_fan_speed_temp, 2) + ",";
    response += "\"protection_enabled\":" + String(status.protection_enabled ? "true" : "false") + ",";
    response += "\"autoband\":" + String(settings.autoband ? "true" : "false") + ",";
    response += "\"default_band\":\"" + String(settings.default_band) + "\"";
    response += "}";
    
    server.send(200, "application/json", response);
}

void handleSaveSettings() {
    if (server.method() != HTTP_POST) {
        server.send(405, "text/plain", "Method Not Allowed");
        return;
    }

    if (server.hasArg("max_swr")) settings.max_swr = server.arg("max_swr").toFloat();
    if (server.hasArg("max_current")) settings.max_current = server.arg("max_current").toFloat();
    if (server.hasArg("max_voltage")) settings.max_voltage = server.arg("max_voltage").toFloat();
    if (server.hasArg("max_plate_temp")) settings.max_plate_temp = server.arg("max_plate_temp").toFloat();
    if (server.hasArg("max_water_temp")) settings.max_water_temp = server.arg("max_water_temp").toFloat();
    if (server.hasArg("max_pump_speed_temp")) settings.max_pump_speed_temp = server.arg("max_pump_speed_temp").toFloat();
    if (server.hasArg("min_pump_speed_temp")) settings.min_pump_speed_temp = server.arg("min_pump_speed_temp").toFloat();
    if (server.hasArg("max_fan_speed_temp")) settings.max_fan_speed_temp = server.arg("max_fan_speed_temp").toFloat();
    if (server.hasArg("min_fan_speed_temp")) settings.min_fan_speed_temp = server.arg("min_fan_speed_temp").toFloat();
    
    status.protection_enabled = server.hasArg("protection_enabled");
    settings.autoband = server.hasArg("autoband");

    if (server.hasArg("default_band")) {
        String band = server.arg("default_band");
        strncpy(settings.default_band, band.c_str(), sizeof(settings.default_band) - 1);
        settings.default_band[sizeof(settings.default_band) - 1] = '\0';
    }

    sendStateData();
    sendSettingsData();
    
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleSetBand() {
    if (server.hasArg("band")) {
        String band = server.arg("band");

        memset(state.band, 0, sizeof(state.band));
        strncpy(state.band, band.c_str(), sizeof(state.band) - 1);
        state.band[sizeof(state.band) - 1] = '\0';

        strncpy(status.band, band.c_str(), sizeof(status.band) - 1);
        status.band[sizeof(status.band) - 1] = '\0';
        
        Serial.print("Band set to: ");
        Serial.println(state.band);
        sendStateData();
        
        server.send(200, "text/plain", "OK");
    } else {
        server.send(400, "text/plain", "Missing band parameter");
    }
}

void handleCSS() {
    String css = R"rawliteral(
    body {
        font-family: Arial, sans-serif;
        margin: 0;
        padding: 20px;
        background-color: #f0f0f0;
    }
    .container {
        max-width: 800px;
        margin: 0 auto;
        background: white;
        padding: 20px;
        border-radius: 10px;
        box-shadow: 0 2px 10px rgba(0,0,0,0.1);
    }
    h1 {
        text-align: center;
        color: #333;
        margin-bottom: 20px;
    }
    .status-grid {
        display: grid;
        grid-template-columns: 1fr 1fr;
        gap: 15px;
        margin: 20px 0;
    }
    .settings-grid {
        display: grid;
        grid-template-columns: 1fr 1fr;
        gap: 15px;
        margin: 20px 0;
    }
    .status-item, .settings-item {
        display: flex;
        flex-direction: column;
        padding: 15px;
        background: #f8f9fa;
        border-radius: 8px;
        border: 1px solid #e9ecef;
    }
    .status-item label, .settings-item label {
        font-weight: bold;
        color: #495057;
        font-size: 0.9em;
        margin-bottom: 8px;
    }
    .settings-item input, .settings-item select {
        padding: 8px 12px;
        border: 1px solid #ced4da;
        border-radius: 4px;
        font-size: 1em;
        background: white;
    }
    .settings-item input:focus, .settings-item select:focus {
        outline: none;
        border-color: #007AFF;
        box-shadow: 0 0 0 2px rgba(0,122,255,0.2);
    }
    .bar {
        width: 100%;
        height: 20px;
        background: #e0e0e0;
        border-radius: 10px;
        margin-top: 5px;
        overflow: hidden;
    }
    .bar-fill {
        height: 100%;
        background: #007AFF;
        border-radius: 10px;
        transition: all 0.3s ease;
        width: 0%;
    }
    /* Стили для переключателя */
    .toggle-switch {
        display: flex;
        align-items: center;
        margin-top: 5px;
    }
    .toggle-input {
        display: none;
    }
    .toggle-label {
        position: relative;
        display: inline-block;
        width: 50px;
        height: 25px;
        background-color: #ccc;
        border-radius: 25px;
        cursor: pointer;
        transition: background-color 0.3s;
    }
    .toggle-label:before {
        content: "";
        position: absolute;
        width: 21px;
        height: 21px;
        border-radius: 50%;
        background-color: white;
        top: 2px;
        left: 2px;
        transition: transform 0.3s;
    }
    .toggle-input:checked + .toggle-label {
        background-color: #28a745;
    }
    .toggle-input:checked + .toggle-label:before {
        transform: translateX(25px);
    }
    .band-grid {
        display: grid;
        grid-template-columns: repeat(3, 1fr);
        gap: 10px;
        margin: 20px 0;
    }
    .band-btn, .navigation button {
        padding: 12px 20px;
        border: none;
        background: #007AFF;
        color: white;
        border-radius: 6px;
        cursor: pointer;
        font-size: 1em;
        transition: all 0.3s ease;
        text-decoration: none;
        display: inline-block;
        text-align: center;
    }
    .band-btn:hover, .navigation button:hover {
        background: #0056cc;
        transform: translateY(-1px);
        box-shadow: 0 2px 5px rgba(0,0,0,0.2);
    }
    .navigation {
        display: flex;
        justify-content: space-between;
        align-items: center;
        margin-top: 25px;
        padding-top: 20px;
        border-top: 1px solid #e9ecef;
    }
    .navigation button {
        margin: 0 5px;
    }
    .navigation button:first-child {
        background: #6c757d;
    }
    .navigation button:first-child:hover {
        background: #545b62;
    }
    .navigation button:last-child {
        background: #28a745;
    }
    .navigation button:last-child:hover {
        background: #218838;
    }
    .message {
        color: #28a745;
        font-weight: bold;
        text-align: center;
        margin: 10px 0;
        min-height: 20px;
    }
    @media (max-width: 768px) {
        .status-grid, .settings-grid {
            grid-template-columns: 1fr;
        }
        .band-grid {
            grid-template-columns: repeat(2, 1fr);
        }
        .navigation {
            flex-direction: column;
            gap: 10px;
        }
        .navigation button {
            width: 100%;
            margin: 5px 0;
        }
    }
    @media (max-width: 480px) {
        body {
            padding: 10px;
        }
        .container {
            padding: 15px;
        }
        .band-grid {
            grid-template-columns: 1fr;
        }
    }
    )rawliteral";
    
    server.send(200, "text/css", css);
}

void handleSetState() {
    if (server.hasArg("state")) {
        String stateStr = server.arg("state");
        bool newState = (stateStr == "true");

        state.state = newState;
        
        Serial.print("State set to: ");
        Serial.println(newState ? "ON" : "OFF");

        sendStateData();
        
        server.send(200, "text/plain", "OK");
    } else {
        server.send(400, "text/plain", "Missing state parameter");
    }
}
//////////////////////////////////////////// OTA ////////////////////////////////////////////////////////

void setupOTA() {
  ArduinoOTA.setHostname(otaHostname);
  ArduinoOTA.setPassword(otaPassword);
  // ArduinoOTA.setPort(3232);
  ArduinoOTA.setRebootOnSuccess(true);

  ArduinoOTA.onStart([]() {
    String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
    Serial.println("[OTA] Start updating " + type);
  });
  
  ArduinoOTA.onEnd([]() {
    Serial.println("\n[OTA] End");
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("[OTA] Progress: %u%%\r", (progress / (total / 100)));
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[OTA] Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  
  ArduinoOTA.begin();
  Serial.println("[OTA] Ready");
  Serial.print("[OTA] IP address: ");
  Serial.println(WiFi.localIP());
}

//////////////////////////////////////////// DATA PROCESSING ////////////////////////////////////////////

bool requestAndWaitForSettings(unsigned long timeout) {
#if DEBUG
    Serial.println("[DEBUG] Starting settings request with waiting");
#endif

    waitingForResponse = RESPONSE_NONE;
    responseRetryCount = 0;

    sendSettingsCommand();
    
    unsigned long startTime = millis();

    while (millis() - startTime < timeout) {
        handleUARTData();

        if (waitingForResponse == RESPONSE_NONE) {
            if (settings.max_swr > 0 || settings.max_current > 0) {
#if DEBUG
                Serial.println("[DEBUG] Settings received successfully");
                debugSettingsData();
#endif
                return true;
            }
        }
        
        handleResponseRetry();
    }
    
#if DEBUG
    Serial.println("[DEBUG] Settings request timeout");
#endif
    return false;
}

// feel da beat
void toggleAutoBand(lv_event_t * e) {
    settings.autoband = lv_obj_get_state(ui_protectionSwitch) & LV_STATE_CHECKED;
    sendSettingsData();
}

void settingsNext(lv_event_t * e) {
    settings.max_swr = lv_slider_get_value(ui_swrSlider);
    settings.max_current = lv_slider_get_value(ui_currentSlider);
    settings.max_voltage = lv_slider_get_value(ui_voltageSlider);
    settings.max_plate_temp = lv_slider_get_value(ui_plateTmpSlider);
    settings.max_water_temp = lv_slider_get_value(ui_waterTmpSlider);
}
void saveSettings(lv_event_t * e) {
    settings.max_pump_speed_temp = lv_slider_get_value(ui_maxPumpSpdTmpSlider);
    settings.min_pump_speed_temp = lv_slider_get_value(ui_minPumpSpdTmpSlider);
    settings.max_fan_speed_temp = lv_slider_get_value(ui_maxFanSpdTmpSlider);
    settings.min_fan_speed_temp = lv_slider_get_value(ui_minFanSpdTmpSlider);
    status.protection_enabled = lv_obj_get_state(ui_autoSelectSwitch) & LV_STATE_CHECKED;
    lv_dropdown_get_selected_str(ui_defaultBandDropdown, settings.default_band, sizeof(settings.default_band));

#if DEBUG
    Serial.println("[DEBUG] Settings saved from UI to variables:");
    Serial.println("Max SWR: " + String(settings.max_swr));
    Serial.println("Max Current: " + String(settings.max_current));
    Serial.println("Max Voltage: " + String(settings.max_voltage));
    Serial.println("Max Plate Temp: " + String(settings.max_plate_temp));
    Serial.println("Max Water Temp: " + String(settings.max_water_temp));
    Serial.println("Max Pump Speed Temp: " + String(settings.max_pump_speed_temp));
    Serial.println("Min Pump Speed Temp: " + String(settings.min_pump_speed_temp));
    Serial.println("Max Fan Speed Temp: " + String(settings.max_fan_speed_temp));
    Serial.println("Min Fan Speed Temp: " + String(settings.min_fan_speed_temp));
    Serial.println("Protection Enabled: " + String(status.protection_enabled));
#endif
    sendStateData();
    sendSettingsData();
}

void resetAlert(lv_event_t * e) {}
void mainRightLoaded(lv_event_t * e) {}

void dropdown_set_by_text(lv_obj_t *dropdown, const char *text) {
    const char *options = lv_dropdown_get_options(dropdown);
    uint16_t option_count = lv_dropdown_get_option_count(dropdown);
    
    for(uint16_t i = 0; i < option_count; i++) {
        const char *option_start = options;
        while(*options != '\n' && *options != '\0') options++;
        
        size_t len = options - option_start;
        if(strlen(text) == len && strncmp(option_start, text, len) == 0) {
            lv_dropdown_set_selected(dropdown, i);
            return;
        }
        
        if(*options == '\n') options++;
    }
}

void protectionOpened(lv_event_t * e) {
#if DEBUG
    Serial.println("[DEBUG] Sending settings request command");
#endif
    if (requestAndWaitForSettings(300)) {
    lv_label_set_text(ui_maxSWR, String(settings.max_swr).c_str());
    lv_slider_set_value(ui_swrSlider, settings.max_swr, LV_ANIM_OFF);
    
    lv_label_set_text(ui_maxCurr, String(settings.max_current).c_str());
    lv_slider_set_value(ui_currentSlider, settings.max_current, LV_ANIM_OFF);

    lv_label_set_text(ui_maxVoltage, String(settings.max_voltage).c_str());
    lv_slider_set_value(ui_voltageSlider, settings.max_voltage, LV_ANIM_OFF);

    lv_label_set_text(ui_maxPlateTemp, String(settings.max_plate_temp).c_str());
    lv_slider_set_value(ui_plateTmpSlider, settings.max_plate_temp, LV_ANIM_OFF);

    lv_label_set_text(ui_maxWaterTemp, String(settings.max_water_temp).c_str());
    lv_slider_set_value(ui_waterTmpSlider, settings.max_water_temp, LV_ANIM_OFF);

    lv_label_set_text(ui_maxPumpSpeedTmp, String(settings.max_pump_speed_temp).c_str());
    lv_slider_set_value(ui_maxPumpSpdTmpSlider, settings.max_pump_speed_temp, LV_ANIM_OFF);

    lv_label_set_text(ui_minPumpSpeedTmp, String(settings.min_pump_speed_temp).c_str());
    lv_slider_set_value(ui_minPumpSpdTmpSlider, settings.min_pump_speed_temp, LV_ANIM_OFF);

    lv_label_set_text(ui_maxFanSpeedTmp, String(settings.max_fan_speed_temp).c_str());
    lv_slider_set_value(ui_maxFanSpdTmpSlider, settings.max_fan_speed_temp, LV_ANIM_OFF);

    lv_label_set_text(ui_minFanSpeedTmp, String(settings.min_fan_speed_temp).c_str());
    lv_slider_set_value(ui_minFanSpdTmpSlider, settings.min_fan_speed_temp, LV_ANIM_OFF);

    set_switch_state(ui_protectionSwitch, status.protection_enabled);

    dropdown_set_by_text(ui_defaultBandDropdown, settings.default_band);
    }
}

void debugStatusData() {
    if (waitingForResponse != RESPONSE_NONE) return;
    
    Serial.println("=== STATUS DATA ===");
    Serial.print("FWD: "); Serial.println(status.fwd);
    Serial.print("REF: "); Serial.println(status.ref);
    Serial.print("TRX FWD: "); Serial.println(status.trxfwd);
    Serial.print("SWR: "); Serial.println(status.swr);
    Serial.print("Current: "); Serial.println(status.current);
    Serial.print("Voltage: "); Serial.println(status.voltage);
    Serial.print("Water temp: "); Serial.println(status.water_temp);
    Serial.print("Plate temp: "); Serial.println(status.plate_temp);
    Serial.print("Alarm: "); Serial.println(status.alarm ? "true" : "false");
    Serial.print("Alert reason: "); Serial.println(status.alert_reason);
    Serial.print("State: "); Serial.println(status.state ? "true" : "false");
    Serial.print("PTT: "); Serial.println(status.ptt ? "true" : "false");
    Serial.print("Band: "); Serial.println(status.band);
    Serial.print("PWM Pump: "); Serial.println(status.pwm_pump);
    Serial.print("PWM Cooler: "); Serial.println(status.pwm_cooler);
    Serial.print("Auto PWM Pump: "); Serial.println(status.auto_pwm_pump ? "true" : "false");
    Serial.print("Auto PWM Fan: "); Serial.println(status.auto_pwm_fan ? "true" : "false");
    Serial.println("===================");
}

void debugSettingsData() {
    Serial.println("=== SETTINGS DATA ===");
    Serial.print("Max SWR: "); Serial.println(settings.max_swr);
    Serial.print("Max Current: "); Serial.println(settings.max_current);
    Serial.print("Max Voltage: "); Serial.println(settings.max_voltage);
    Serial.print("Max Water Temp: "); Serial.println(settings.max_water_temp);
    Serial.print("Max Plate Temp: "); Serial.println(settings.max_plate_temp);
    Serial.print("Max Pump Speed Temp: "); Serial.println(settings.max_pump_speed_temp);
    Serial.print("Min Pump Speed Temp: "); Serial.println(settings.min_pump_speed_temp);
    Serial.print("Max Fan Speed Temp: "); Serial.println(settings.max_fan_speed_temp);
    Serial.print("Min Fan Speed Temp: "); Serial.println(settings.min_fan_speed_temp);
    Serial.print("Autoband: "); Serial.println(settings.autoband ? "true" : "false");
    Serial.print("Default Band: "); Serial.println(settings.default_band);
    Serial.println("====================");
}

bool parseAckResponse(const char* json, ResponseType expectedResponse) {
    if (strstr(json, "\"response\":\"settings updated\"") != NULL) {
#if DEBUG
    Serial.println("Settings update RESPONSOR!");
#endif
        return expectedResponse == RESPONSE_SETTINGS_SEND;
    }
    if (strstr(json, "\"response\":\"state updated\"") != NULL) {
#if DEBUG
    Serial.println("Status update RESPONSOR!");
#endif
        return expectedResponse == RESPONSE_STATE_SEND;
    }
    return false;
}

void sendSettingsCommand() {
#if DEBUG
    Serial.print("[DEBUG] Sending settings request command");
    if (responseRetryCount > 0) {
        Serial.print(" (retry ");
        Serial.print(responseRetryCount);
        Serial.print("/");
        Serial.print(MAX_RETRIES);
        Serial.print(")");
    }
    Serial.println();
#endif
    Serial1.println("{\"command\":{\"value\":\"settings\"}}");
    
    waitingForResponse = RESPONSE_SETTINGS_REQUEST;
    responseRequestTime = millis();
}

void sendStatusRequest() {
#if DEBUG
    if (waitingForResponse == RESPONSE_NONE) {
        Serial.println("[DEBUG] Sending status request command");
    }
#endif
    Serial1.println("{\"command\":{\"value\":\"status\"}}");
}

void sendSettingsData() {
#if DEBUG
    Serial.print("[DEBUG] Sending settings data");
    if (responseRetryCount > 0) {
        Serial.print(" (retry ");
        Serial.print(responseRetryCount);
        Serial.print("/");
        Serial.print(MAX_RETRIES);
        Serial.print(")");
    }
    Serial.println();
#endif

    if (&settings == NULL || &pendingSettings == NULL) {
        Serial.println("ERROR: Settings pointers are null");
        return;
    }
    
    memcpy(&pendingSettings, &settings, sizeof(SettingsData));

    char json[768];
    snprintf(json, sizeof(json),
        "{\"settings\":{"
        "\"max_swr\":%.2f,"
        "\"max_current\":%.2f,"
        "\"max_voltage\":%.2f,"
        "\"max_water_temp\":%.2f,"
        "\"max_plate_temp\":%.2f,"
        "\"max_pump_speed_temp\":%.2f,"
        "\"min_pump_speed_temp\":%.2f,"
        "\"max_fan_speed_temp\":%.2f,"
        "\"min_fan_speed_temp\":%.2f,"
        "\"autoband\":%s,"
        "\"default_band\":\"%s\"}}",
        settings.max_swr,
        settings.max_current,
        settings.max_voltage,
        settings.max_water_temp,
        settings.max_plate_temp,
        settings.max_pump_speed_temp,
        settings.min_pump_speed_temp,
        settings.max_fan_speed_temp,
        settings.min_fan_speed_temp,
        settings.autoband ? "true" : "false",
        settings.default_band
    );

#if DEBUG
    Serial.print("[DEBUG] JSON: ");
    Serial.println(json);
    Serial0.println(json);
#endif
    Serial1.println(json);
    
    waitingForResponse = RESPONSE_SETTINGS_SEND;
    delay(1000);
    responseRequestTime = millis();
}

void sendStateData() {
#if DEBUG
    Serial.print("[DEBUG] Sending state data");
    if (responseRetryCount > 0) {
        Serial.print(" (retry ");
        Serial.print(responseRetryCount);
        Serial.print("/");
        Serial.print(MAX_RETRIES);
        Serial.print(")");
    }
    Serial.println();
#endif
    
    // saving values for retry if failed
    memcpy(&pendingState, &state, sizeof(StateData));
    
    char json[256];
    strcpy(json, "{\"state\":{");
    
    strcat(json, "\"enabled\":");
    strcat(json, state.state ? "true" : "false");
    strcat(json, ",");

    strcat(json, "\"protection_enabled\":");
    strcat(json, status.protection_enabled ? "true" : "false");
    strcat(json, ",");

    strcat(json, "\"ptt\":");
    strcat(json, state.ptt ? "true" : "false");
    strcat(json, ",");
    
    strcat(json, "\"pwm_pump\":");
    char temp[10];
    itoa(state.pwm_pump, temp, 10);
    strcat(json, temp);
    strcat(json, ",");
    
    strcat(json, "\"pwm_cooler\":");
    itoa(state.pwm_cooler, temp, 10);
    strcat(json, temp);
    strcat(json, ",");
    
    strcat(json, "\"band\":\"");
    strcat(json, state.band);
    strcat(json, "\",");
    
    strcat(json, "\"auto_pwm_pump\":");
    strcat(json, state.auto_pwm_pump ? "true" : "false");
    strcat(json, ",");
    
    strcat(json, "\"auto_pwm_fan\":");
    strcat(json, state.auto_pwm_fan ? "true" : "false");

    strcat(json, "}}");
    
    Serial1.println(json);
    Serial.println(json);
    waitingForResponse = RESPONSE_STATE_SEND;
    responseRequestTime = millis();
}

void handleResponseRetry() {
    if (waitingForResponse == RESPONSE_NONE) return;
    
    unsigned long currentTime = millis();
    
    if (currentTime - responseRequestTime >= RETRY_INTERVAL) {
        if (responseRetryCount < MAX_RETRIES) {
            responseRetryCount++;
            
            switch (waitingForResponse) {
                case RESPONSE_SETTINGS_REQUEST:
                    sendSettingsCommand();
                    break;
                case RESPONSE_SETTINGS_SEND:
#if DEBUG
                    Serial.println("[DEBUG] Retrying settings send");
#endif
                    sendSettingsData();
                    break;
                case RESPONSE_STATE_SEND:
#if DEBUG
                    Serial.println("[DEBUG] Retrying state send");
#endif
                    sendStateData();
                    break;
                default:
                    waitingForResponse = RESPONSE_NONE;
                    responseRetryCount = 0;
                    break;
            }
        } else {
#if DEBUG
            Serial.println("[DEBUG] Max retries reached, giving up");
#endif
            waitingForResponse = RESPONSE_NONE;
            responseRetryCount = 0;
        }
    }
}

void handleTestRequests() {
#if TEST_UART
    if (waitingForResponse != RESPONSE_NONE) return;
    
    unsigned long currentTime = millis();
    if (currentTime - lastTestRequest >= TEST_REQUEST_INTERVAL) {
        lastTestRequest = currentTime;
        
#if DEBUG
        Serial.println("[TEST_UART] Sending automatic settings request");
#endif
        responseRetryCount = 0;
        sendSettingsCommand();
    }
#endif
}

void set_switch_state(lv_obj_t *sw, bool state) {
    if (state)
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    else
        lv_obj_clear_state(sw, LV_STATE_CHECKED);
}

void processParsedData() {
    if (waitingForResponse != RESPONSE_NONE) return;
    
#if DEBUG
    Serial.println("[DEBUG] Processing status data");
    debugStatusData();
#endif
    if (getCurrentScreenName() == "main" ) {
        lv_label_set_text(ui_pwrTxt, (String(status.fwd) + "W").c_str());
        lv_bar_set_value(ui_pwrBar, int(status.fwd), LV_ANIM_ON);
        if (status.ptt) {
        lv_obj_set_style_bg_color(ui_pwrBar, lv_color_hex(0xFF0000), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(ui_menuBtn, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(ui_Button1, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(ui_Button2, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
        } else {
        lv_obj_set_style_bg_color(ui_pwrBar, lv_color_hex(0x007AFF), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(ui_menuBtn, lv_color_hex(0x007AFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(ui_Button1, lv_color_hex(0x007AFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(ui_Button2, lv_color_hex(0x007AFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        lv_label_set_text(ui_swrValue, String(status.swr).c_str());
        lv_label_set_text(ui_refTxt, (String(status.ref) + "W").c_str());
        lv_label_set_text(ui_volTxt, (String(status.voltage, 1) + "V").c_str());
        lv_label_set_text(ui_current, (String(status.current, 1) + "A").c_str());
        lv_label_set_text(ui_waterTmp, (String(status.water_temp, 1) + "C").c_str());
        lv_label_set_text(ui_plateTmp, (String(status.plate_temp, 1) + "C").c_str());
        set_switch_state(ui_mainSwitch, status.state);
        lv_label_set_text(ui_Label2, String(status.band).c_str());
        strncpy(state.band, status.band, sizeof(state.band) - 1);
        lv_label_set_text(ui_iPWRTxt, (String(status.trxfwd) + "W").c_str());
    }
}

void processSettingsData() {
#if DEBUG
    Serial.println("[DEBUG] Processing settings data");
    debugSettingsData();
#endif
    if (waitingForResponse == RESPONSE_SETTINGS_REQUEST) {
        waitingForResponse = RESPONSE_NONE;
        responseRetryCount = 0;
    }

}

void processAckResponse(ResponseType expectedResponse) {
#if DEBUG
    Serial.println("[DEBUG] Received ACK confirmation");
#endif
    
    waitingForResponse = RESPONSE_NONE;
    responseRetryCount = 0;
}

/////////////////////////////////////////////////// LVGL ///////////////////////////////////////////////////
extern lv_obj_t* ui_main;
extern lv_obj_t* ui_menu;
extern lv_obj_t* ui_protection;
extern lv_obj_t* ui_wifi;
extern lv_obj_t* ui_mainLeft;
extern lv_obj_t* ui_bands;
extern lv_obj_t* ui_mainRight;
extern lv_obj_t* ui_warning;
extern lv_obj_t* ui_protection2;
extern lv_obj_t* ui_message;

const char* getCurrentScreenName() {
    lv_obj_t* active = lv_scr_act();
    
    if (active == ui_main) return "main";
    if (active == ui_menu) return "menu";
    if (active == ui_protection) return "protection";
    if (active == ui_wifi) return "wifi";
    if (active == ui_mainLeft) return "mainLeft";
    if (active == ui_bands) return "bands";
    if (active == ui_mainRight) return "mainRight";
    if (active == ui_warning) return "warning";
    if (active == ui_protection2) return "protection2";
    if (active == ui_message) return "message";
    
    return "none";
}


void my_print(lv_log_level_t level, const char *buf) {
  LV_UNUSED(level);
  if (waitingForResponse == RESPONSE_NONE) {
    Serial.println(buf);
    Serial.flush();
  }
}

uint32_t millis_cb(void) {
  return millis();
}

void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  uint32_t w = lv_area_get_width(area);
  uint32_t h = lv_area_get_height(area);
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px_map, w, h);
  lv_disp_flush_ready(disp);
}

void my_touchpad_read(lv_indev_t *indev, lv_indev_data_t *data) {
  touchController.read();
  if (touchController.isTouched && touchController.touches > 0) {
    data->point.x = touchController.points[0].x;
    data->point.y = touchController.points[0].y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

///////////////////////////////////////////// PROCESS DATA 2 ///////////////////////////////////////////////////////

void handleUARTData() {
    while (Serial1.available()) {
        char c = Serial1.read();
        
        if (c == '\n') {
            if (dataIndex > 0) {
                receivedData[dataIndex] = 0;
#if DEBUG
    Serial.println(receivedData);
#endif                      
                // Checking data for ACKs
                if (waitingForResponse != RESPONSE_NONE) {
                    if (parseAckResponse(receivedData, waitingForResponse)) {
                        processAckResponse(waitingForResponse);
                        dataIndex = 0;
                        receivedData[0] = 0;
                        continue;
                    }
                }
                if (parseSettingsJson(receivedData)) {
                    processSettingsData();
                } else {
                    if (parseStatusJson(receivedData)) {
                        processParsedData();
                    }
                }
                
                dataIndex = 0;
                receivedData[0] = 0;
            }
        } else if (c != '\r' && dataIndex < 511) {
            receivedData[dataIndex++] = c;
        }
    }
}

void set_band(lv_event_t * e) {
    lv_obj_t * target = lv_event_get_target_obj(e);
    
    memset(state.band, 0, sizeof(state.band));
    
    if (target == ui_Button160m) {
        strncpy(state.band, "160m", sizeof(state.band) - 1);
    } 
    else if (target == ui_Button80m) {
        strncpy(state.band, "80m", sizeof(state.band) - 1);
    } 
    else if (target == ui_Button60m) {
        strncpy(state.band, "60m", sizeof(state.band) - 1);
    } 
    else if (target == ui_Button40m) {
        strncpy(state.band, "40m", sizeof(state.band) - 1);
    }
    else if (target == ui_Button30m) {
        strncpy(state.band, "30m", sizeof(state.band) - 1);
    }
    else if (target == ui_Button20m) {
        strncpy(state.band, "20m", sizeof(state.band) - 1);
    }
    else if (target == ui_Button17m) {
        strncpy(state.band, "17m", sizeof(state.band) - 1);
    }
    else if (target == ui_Button15m) {
        strncpy(state.band, "15m", sizeof(state.band) - 1);
    }
    else if (target == ui_Button12m) {
        strncpy(state.band, "12m", sizeof(state.band) - 1);
    }
    else if (target == ui_Button10m) {
        strncpy(state.band, "10m", sizeof(state.band) - 1);
    }
    else if (target == ui_Button6m) {
        strncpy(state.band, "6m", sizeof(state.band) - 1);
    }
    else {
        Serial.println("Неизвестная кнопка!");
        return;
    }
    
    state.band[sizeof(state.band) - 1] = '\0';
    
    Serial.print("Установлена полоса: ");
    Serial.println(state.band);
    sendStateData();
}

void enableAmp(lv_event_t * e) {
    lv_obj_t * switch_obj = (lv_obj_t *)lv_event_get_target(e);
    
    if(lv_obj_has_state(switch_obj, LV_STATE_CHECKED)) {
        Serial.println("Amplifier: ON");
        state.state = true;
    } else {
        Serial.println("Amplifier: OFF");
        state.state = false;
    }
    sendStateData();
}

void togglePTT(lv_event_t * e) {
    lv_obj_t * switch_obj = (lv_obj_t *)lv_event_get_target(e);
    
    lv_color_t current_color = lv_obj_get_style_bg_color(switch_obj, LV_PART_INDICATOR);
    Serial.printf("Current color - R:%d, G:%d, B:%d\n", current_color.red, current_color.green, current_color.blue);
    if(current_color.red == 0xFF) {
        Serial.println("PTT: OFF");
        state.ptt = false;
        lv_obj_set_style_bg_color(switch_obj, lv_color_hex(0x2196F3), LV_PART_INDICATOR);
    } else {
        if (state.state) {
            Serial.println("PTT: ON");
            state.ptt = true;
            lv_obj_set_style_bg_color(switch_obj, lv_color_hex(0xFF0000), LV_PART_INDICATOR);
        }
    }
    
    sendStateData();
}

///////////////////////////////////////////////////////////// WIFI /////////////////////////////////////////////////////////////
void setupLVGLButtonHandler() {
  lv_obj_add_event_cb(ui_Button10, saveWiFiSettingsEventHandler, LV_EVENT_CLICKED, NULL);
}

static void saveWiFiSettingsEventHandler(lv_event_t* e) {
  lv_event_code_t event_code = lv_event_get_code(e);
  
  if (event_code == LV_EVENT_CLICKED) {
    const char* ssid_text = lv_textarea_get_text(ui_TextArea1);
    const char* password_text = lv_textarea_get_text(ui_TextArea3);
    
    saveWiFiCredentials(ssid_text, password_text);
    connectToWiFi();
    
    Serial.println("WiFi settings saved via LVGL button!");
  }
}

void loadWiFiCredentials() {
  ssid = preferences.getString(SSID_KEY, "");
  password = preferences.getString(PASSWORD_KEY, "");
  
  Serial.println("Loaded WiFi settings:");
  Serial.print("SSID: ");
  Serial.println(ssid.length() > 0 ? ssid : "not set");
  Serial.print("Password: ");
  Serial.println(password.length() > 0 ? password : "not set");
  Serial.println();
}

void saveWiFiCredentials(const char* newSSID, const char* newPassword) {
  if (newSSID == nullptr || strlen(newSSID) == 0) {
    Serial.println("Error: SSID cannot be empty!");
    return;
  }
  
  preferences.putString(SSID_KEY, newSSID);
  preferences.putString(PASSWORD_KEY, newPassword);
  
  ssid = newSSID;
  password = newPassword;
  
  Serial.println("WiFi settings saved!");
}

void connectToWiFi() {
  if (ssid.length() == 0) {
    Serial.println("Error: SSID not set!");
    return;
  }
  
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  
  WiFi.disconnect();
  delay(10);
  WiFi.begin(ssid.c_str(), password.c_str());
  
  isConnecting = true;
  connectionStartTime = millis();
}

void printWiFiStatus() {
  Serial.println("\n=== WiFi Status ===");
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());
  Serial.print("Status: ");
  
  switch (WiFi.status()) {
    case WL_CONNECTED:
      Serial.println("Connected");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      Serial.print("RSSI: ");
      Serial.print(WiFi.RSSI());
      Serial.println(" dBm");
      break;
    case WL_NO_SSID_AVAIL:
      Serial.println("Network not found");
      break;
    case WL_CONNECT_FAILED:
      Serial.println("Connection failed");
      break;
    case WL_IDLE_STATUS:
      Serial.println("Idle");
      break;
    case WL_DISCONNECTED:
      Serial.println("Disconnected");
      break;
    default:
      Serial.println("Unknown status");
      break;
  }
  Serial.println("===================\n");
}

void updateLVGLTextAreasWithSavedCredentials() {
  if (ssid.length() > 0) {
    lv_textarea_set_text(ui_TextArea1, ssid.c_str());
    Serial.println("SSID field populated with saved value");
  } else {
    lv_textarea_set_text(ui_TextArea1, "");
    Serial.println("SSID field cleared - no saved value");
  }
  
  if (password.length() > 0) {
    lv_textarea_set_text(ui_TextArea3, password.c_str());
    Serial.println("Password field populated with saved value");
  } else {
    lv_textarea_set_text(ui_TextArea3, "");
    Serial.println("Password field cleared - no saved value");
  }
}
/////////////////////////////////////////////////////////// CORE ///////////////////////////////////////////////////////////

void setup() {
  Serial.begin(115200);
  Serial1.begin(115200, SERIAL_8N1, 18, 17);
  preferences.begin(PREF_NAMESPACE, false);
  loadWiFiCredentials();

#if DEBUG
  Serial.println("====================================");
  Serial.println("Power amplifier controller frontend");
  Serial.println("DEBUG MODE ENABLED");
#if TEST_UART
  Serial.println("TEST_UART MODE ENABLED");
  Serial.println("Auto-sending settings request every 3 seconds");
#endif
  Serial.println("====================================");
#else
  Serial.println("Power amplifier controller frontend started");
#endif

  // Init Display
  if (!gfx->begin()) {
    Serial.println("gfx->begin() failed!");
    while (true) {}
  }
  
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);
  gfx->fillScreen(RGB565_BLACK);

  // Init touch device
  touchController.begin();
  touchController.setRotation(ROTATION_INVERTED);

  // init LVGL
  lv_init();
  lv_tick_set_cb(millis_cb);

#if LV_USE_LOG != 0
  lv_log_register_print_cb(my_print);
#endif

  screenWidth = gfx->width();
  screenHeight = gfx->height();
  bufSize = screenWidth * 40;

  disp_draw_buf = (lv_color_t *)heap_caps_malloc(bufSize * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!disp_draw_buf) {
    disp_draw_buf = (lv_color_t *)heap_caps_malloc(bufSize * 2, MALLOC_CAP_8BIT);
  }
  if (!disp_draw_buf) {
    Serial.println("LVGL disp_draw_buf allocate failed!");
    while (true) {}
  } else {
    disp = lv_display_create(screenWidth, screenHeight);
    lv_display_set_flush_cb(disp, my_disp_flush);
    lv_display_set_buffers(disp, disp_draw_buf, NULL, bufSize * 2, LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, my_touchpad_read);   
  }
  ui_init();
  
#if DEBUG
  Serial.println("[DEBUG] Setup completed successfully");
#else
  Serial.println("Setup done");
#endif
  // Setup LVGL button handler and fill text areas
  setupLVGLButtonHandler();
  updateLVGLTextAreasWithSavedCredentials();
  
  connectToWiFi();
  // CHANGE - setup OTA only if wi-fi connected
  setupOTA();
  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/band", HTTP_GET, handleBandPage);
  server.on("/settings", HTTP_GET, handleSettingsPage);
  server.on("/getsettings", HTTP_GET, handleGetSettings);
  server.on("/savesettings", HTTP_POST, handleSaveSettings);
  server.on("/setband", HTTP_POST, handleSetBand);
  server.on("/setstate", HTTP_POST, handleSetState);
  server.on("/style.css", HTTP_GET, handleCSS);
  Serial.println("HTTP server started");
}

void loop() {
  lv_task_handler();
  unsigned long currentTime = millis();
  
  // Handle WiFi connection process
  if (isConnecting) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nConnected successfully!");
      printWiFiStatus();
      
      // Start web server only after successful WiFi connection
      if (!serverStarted) {
        server.begin();
        serverStarted = true;
        Serial.println("HTTP server started");
      }
      
      isConnecting = false;
    } else if (currentTime - connectionStartTime > CONNECTION_TIMEOUT) {
      Serial.println("\nConnection timeout!");
      isConnecting = false;
      WiFi.disconnect();
    }
  }
  
  // Handle WiFi reconnection
  if (!isConnecting && WiFi.status() != WL_CONNECTED) {
    if (currentTime - lastWifiReconnectAttempt >= WIFI_RECONNECT_INTERVAL) {
      Serial.println("WiFi connection lost. Reconnecting...");
      connectToWiFi();
      lastWifiReconnectAttempt = currentTime;
    }
  }

  handleUARTData();
  handleResponseRetry();
  handleTestRequests();
  // CHANGE check wi-fi connecnted before start
  ArduinoOTA.handle();
  
  // Handle server only if it's started
  if (serverStarted) {
    server.handleClient();
  }
  
  //if (status.alarm) {lv_label_set_text(ui_alertReason, String(status.alert_reason).c_str()); lv_scr_load(ui_warning);}
}