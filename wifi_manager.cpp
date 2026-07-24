#include "wifi_manager.h"

void loadWiFiCredentials() {
    ssid     = preferences.getString(SSID_KEY,     "");
    password = preferences.getString(PASSWORD_KEY, "");
    // Never print the password — it went to the serial log in plaintext on every
    // boot. Report only whether each credential is set.
    LOG_INFO("Loaded WiFi: SSID %s, password %s",
             ssid.length()     > 0 ? ssid.c_str() : "(not set)",
             password.length() > 0 ? "(set)"      : "(not set)");
}

void saveWiFiCredentials(const char* newSSID, const char* newPassword) {
    if (newSSID == nullptr || strlen(newSSID) == 0) {
        LOG_WARN("SSID cannot be empty, not saving");
        return;
    }
    preferences.putString(SSID_KEY,     newSSID);
    preferences.putString(PASSWORD_KEY, newPassword);
    ssid     = newSSID;
    password = newPassword;
    LOG_INFO("WiFi settings saved");
}

void connectToWiFi() {
    if (ssid.length() == 0) {
        LOG_WARN("SSID not set, cannot connect");
        return;
    }
    LOG_INFO("Connecting to WiFi: %s", ssid.c_str());
    WiFi.disconnect();
    delay(10);
    WiFi.begin(ssid.c_str(), password.c_str());
    isConnecting        = true;
    connectionStartTime = millis();
}

void printWiFiStatus() {
    switch (WiFi.status()) {
        case WL_CONNECTED:
            LOG_INFO("WiFi connected: SSID %s, IP %s, RSSI %ld dBm",
                     WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), (long)WiFi.RSSI());
            break;
        case WL_NO_SSID_AVAIL:  LOG_WARN("WiFi: network not found");   break;
        case WL_CONNECT_FAILED: LOG_WARN("WiFi: connection failed");   break;
        case WL_IDLE_STATUS:    LOG_INFO("WiFi: idle");                break;
        case WL_DISCONNECTED:   LOG_INFO("WiFi: disconnected");        break;
        default:                LOG_INFO("WiFi: status %d", WiFi.status()); break;
    }
}

void updateLVGLTextAreasWithSavedCredentials() {
    lv_textarea_set_text(ui_TextArea1, ssid.length()     > 0 ? ssid.c_str()     : "");
    lv_textarea_set_text(ui_TextArea3, password.length() > 0 ? password.c_str() : "");
    LOG_TRACE("WiFi UI fields populated (ssid:%d pw:%d)",
              ssid.length() > 0, password.length() > 0);
}
