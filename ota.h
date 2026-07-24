#pragma once
#include "globals.h"

inline void setupOTA() {
    ArduinoOTA.setHostname(otaHostname);
    ArduinoOTA.setPassword(otaPassword);
    ArduinoOTA.setRebootOnSuccess(true);

    ArduinoOTA.onStart([]() {
        LOG_INFO("[OTA] Start updating %s",
                 ArduinoOTA.getCommand() == U_FLASH ? "sketch" : "filesystem");
    });
    ArduinoOTA.onEnd([]() {
        LOG_INFO("[OTA] End");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        // Live progress meter (\r, no newline) — only ever runs during an
        // update, so print it directly regardless of level.
        if (total) Serial.printf("[OTA] Progress: %u%%\r", progress / (total / 100));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        const char* reason =
            error == OTA_AUTH_ERROR    ? "Auth Failed"    :
            error == OTA_BEGIN_ERROR   ? "Begin Failed"   :
            error == OTA_CONNECT_ERROR ? "Connect Failed" :
            error == OTA_RECEIVE_ERROR ? "Receive Failed" :
            error == OTA_END_ERROR     ? "End Failed"     : "Unknown";
        LOG_WARN("[OTA] Error[%u]: %s", error, reason);
    });

    ArduinoOTA.begin();
    LOG_INFO("[OTA] Ready, IP %s", WiFi.localIP().toString().c_str());
}
