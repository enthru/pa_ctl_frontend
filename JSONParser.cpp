#include "JSONParser.h"
#include <string.h>
#include <stdlib.h>
#include <Arduino.h>

extern StatusData status;
extern SettingsData settings;
extern StateData state;

char* getJsonValue(char* json, const char* key) {
    static char value[128];
    value[0] = '\0';
    
    if (!json || !key) {
        return value;
    }
    
    char searchKey[64];
    snprintf(searchKey, sizeof(searchKey), "\"%s\":", key);
    
    char* keyPos = strstr(json, searchKey);
    if (!keyPos) {
        return value;
    }

    char* valueStart = keyPos + strlen(searchKey);

    while (*valueStart == ' ') {
        valueStart++;
    }

    if (*valueStart == '"') {
        valueStart++;
        char* valueEnd = strchr(valueStart, '"');
        if (valueEnd) {
            int len = valueEnd - valueStart;
            if (len > 0 && len < 127) {
                strncpy(value, valueStart, len);
                value[len] = '\0';
            }
        }
    } else {
        char* valueEnd = valueStart;

        while (*valueEnd && *valueEnd != ',' && *valueEnd != '}' && *valueEnd != ' ' && *valueEnd != '\n' && *valueEnd != '\r') {
            valueEnd++;
        }
        
        int len = valueEnd - valueStart;
        if (len > 0 && len < 127) {
            strncpy(value, valueStart, len);
            value[len] = '\0';
        }
    }
    
    return value;
}

int parseStatusJson(char* jsonString) {
    if (!jsonString) {
        return 0;
    }
    
    char* statusStart = strstr(jsonString, "\"status\":");
    if (!statusStart) {
        return 0;
    }
    
    char* braceStart = strchr(statusStart, '{');
    char* braceEnd = strchr(braceStart, '}');
    if (!braceStart || !braceEnd) {
        return 0;
    }
    
    char statusObj[512];
    int objLen = braceEnd - braceStart + 1;
    if (objLen <= 0 || objLen > 511) {
        return 0;
    }
    
    strncpy(statusObj, braceStart, objLen);
    statusObj[objLen] = '\0';
    
#if DEBUG
    Serial.print("[DEBUG] Parsing status object: ");
    Serial.println(statusObj);
#endif

    status.fwd = atof(getJsonValue(statusObj, "fwd"));
    status.ref = atof(getJsonValue(statusObj, "ref"));
    status.trxfwd = atof(getJsonValue(statusObj, "trxfwd"));
    status.swr = atof(getJsonValue(statusObj, "swr"));
    status.current = atof(getJsonValue(statusObj, "current"));
    status.voltage = atof(getJsonValue(statusObj, "voltage"));
    status.water_temp = atof(getJsonValue(statusObj, "water_temp"));
    status.plate_temp = atof(getJsonValue(statusObj, "plate_temp"));
    
    char* alarmStr = getJsonValue(statusObj, "alarm");
    status.alarm = (strcmp(alarmStr, "true") == 0);
    
    char* alertReason = getJsonValue(statusObj, "alert_reason");
    strncpy(status.alert_reason, alertReason, sizeof(status.alert_reason) - 1);
    status.alert_reason[sizeof(status.alert_reason) - 1] = '\0';
    
    char* stateStr = getJsonValue(statusObj, "state");
    status.state = (strcmp(stateStr, "true") == 0);
    
    char* pttStr = getJsonValue(statusObj, "ptt");
    status.ptt = (strcmp(pttStr, "true") == 0);
    
    char* bandStr = getJsonValue(statusObj, "band");
    strncpy(status.band, bandStr, sizeof(status.band) - 1);
    status.band[sizeof(status.band) - 1] = '\0';
    
    status.pwm_pump = atoi(getJsonValue(statusObj, "pwm_pump"));
    status.pwm_cooler = atoi(getJsonValue(statusObj, "pwm_cooler"));
    
    char* autoPumpStr = getJsonValue(statusObj, "auto_pwm_pump");
    status.auto_pwm_pump = (strcmp(autoPumpStr, "true") == 0);
    
    char* autoFanStr = getJsonValue(statusObj, "auto_pwm_fan");
    status.auto_pwm_fan = (strcmp(autoFanStr, "true") == 0);
    
    //bool
    char* protectionStr = getJsonValue(statusObj, "protection_enabled");
    status.protection_enabled = (strcmp(protectionStr, "true") == 0);
    
#if DEBUG
    Serial.println("[DEBUG] Status parsing completed successfully");
#endif
    
    return 1;
}

int parseSettingsJson(char* jsonString) {
    if (!jsonString) {
        return 0;
    }
    
    char* settingsStart = strstr(jsonString, "\"settings\":");
    if (!settingsStart) {
        return 0;
    }
    
    char* braceStart = strchr(settingsStart, '{');
    char* braceEnd = strchr(braceStart, '}');
    if (!braceStart || !braceEnd) {
        return 0;
    }
    
    char settingsObj[512];
    int objLen = braceEnd - braceStart + 1;
    if (objLen <= 0 || objLen > 511) {
        return 0;
    }
    
    strncpy(settingsObj, braceStart, objLen);
    settingsObj[objLen] = '\0';
    
#if DEBUG
    Serial.print("[DEBUG] Parsing settings object: ");
    Serial.println(settingsObj);
#endif

    settings.max_swr = atof(getJsonValue(settingsObj, "max_swr"));
    settings.max_current = atof(getJsonValue(settingsObj, "max_current"));
    settings.max_voltage = atof(getJsonValue(settingsObj, "max_voltage"));
    settings.max_water_temp = atof(getJsonValue(settingsObj, "max_water_temp"));
    settings.max_plate_temp = atof(getJsonValue(settingsObj, "max_plate_temp"));
    settings.max_pump_speed_temp = atof(getJsonValue(settingsObj, "max_pump_speed_temp"));
    settings.min_pump_speed_temp = atof(getJsonValue(settingsObj, "min_pump_speed_temp"));
    settings.max_fan_speed_temp = atof(getJsonValue(settingsObj, "max_fan_speed_temp"));
    settings.min_fan_speed_temp = atof(getJsonValue(settingsObj, "min_fan_speed_temp"));

    char* autobandStr = getJsonValue(settingsObj, "autoband");
    settings.autoband = (strcmp(autobandStr, "true") == 0);
    
    char* defaultBandStr = getJsonValue(settingsObj, "default_band");
    strncpy(settings.default_band, defaultBandStr, sizeof(settings.default_band) - 1);
    settings.default_band[sizeof(settings.default_band) - 1] = '\0';
    
#if DEBUG
    Serial.println("[DEBUG] Settings parsing completed successfully");
#endif
    
    return 1;
}