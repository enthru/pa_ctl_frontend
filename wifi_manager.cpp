#include "wifi_manager.h"

void loadWiFiCredentials() {
    ssid     = preferences.getString(SSID_KEY,     "");
    password = preferences.getString(PASSWORD_KEY, "");
    Serial.println("Loaded WiFi settings:");
    Serial.print("SSID: ");    Serial.println(ssid.length()     > 0 ? ssid     : "not set");
    Serial.print("Password: ");Serial.println(password.length() > 0 ? password : "not set");
}

void saveWiFiCredentials(const char* newSSID, const char* newPassword) {
    if (newSSID == nullptr || strlen(newSSID) == 0) {
        Serial.println("Error: SSID cannot be empty!");
        return;
    }
    preferences.putString(SSID_KEY,     newSSID);
    preferences.putString(PASSWORD_KEY, newPassword);
    ssid     = newSSID;
    password = newPassword;
    Serial.println("WiFi settings saved!");
}

void connectToWiFi() {
    if (ssid.length() == 0) {
        Serial.println("Error: SSID not set!");
        return;
    }
    Serial.print("Connecting to WiFi: "); Serial.println(ssid);
    WiFi.disconnect();
    delay(10);
    WiFi.begin(ssid.c_str(), password.c_str());
    isConnecting        = true;
    connectionStartTime = millis();
}

void printWiFiStatus() {
    Serial.println("\n=== WiFi Status ===");
    Serial.print("SSID: "); Serial.println(WiFi.SSID());
    Serial.print("Status: ");
    switch (WiFi.status()) {
        case WL_CONNECTED:
            Serial.println("Connected");
            Serial.print("IP: ");   Serial.println(WiFi.localIP());
            Serial.print("RSSI: "); Serial.print(WiFi.RSSI()); Serial.println(" dBm");
            break;
        case WL_NO_SSID_AVAIL:  Serial.println("Network not found");   break;
        case WL_CONNECT_FAILED: Serial.println("Connection failed");    break;
        case WL_IDLE_STATUS:    Serial.println("Idle");                 break;
        case WL_DISCONNECTED:   Serial.println("Disconnected");         break;
        default:                Serial.println("Unknown status");       break;
    }
    Serial.println("===================\n");
}

void updateLVGLTextAreasWithSavedCredentials() {
    lv_textarea_set_text(ui_TextArea1, ssid.length()     > 0 ? ssid.c_str()     : "");
    lv_textarea_set_text(ui_TextArea3, password.length() > 0 ? password.c_str() : "");
    Serial.println(ssid.length()     > 0 ? "SSID field populated"     : "SSID field cleared");
    Serial.println(password.length() > 0 ? "Password field populated" : "Password field cleared");
}
