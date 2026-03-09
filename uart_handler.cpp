#include "uart_handler.h"
#include "ui_handlers.h"   // getCurrentScreenName, set_switch_state

// ─── Forward declarations of data processors ─────────────────────────────────
static void processParsedData();
static void processSettingsData();
static void processCalibrationData();
static void processAckResponse(ResponseType expectedResponse);
static bool parseAckResponse(const char* json, ResponseType expectedResponse);

// ─── Debug helpers ────────────────────────────────────────────────────────────

void debugStatusData() {
    if (waitingForResponse != RESPONSE_NONE) return;
    Serial.println("=== STATUS DATA ===");
    Serial.print("FWD: ");         Serial.println(status.fwd);
    Serial.print("REF: ");         Serial.println(status.ref);
    Serial.print("TRX FWD: ");     Serial.println(status.trxfwd);
    Serial.print("SWR: ");         Serial.println(status.swr);
    Serial.print("Current: ");     Serial.println(status.current);
    Serial.print("Voltage: ");     Serial.println(status.voltage);
    Serial.print("Water temp: ");  Serial.println(status.water_temp);
    Serial.print("Plate temp: ");  Serial.println(status.plate_temp);
    Serial.print("Coeff: ");       Serial.println(status.coeff);
    Serial.print("Alarm: ");       Serial.println(status.alarm ? "true" : "false");
    Serial.print("Alert reason: ");Serial.println(status.alert_reason);
    Serial.print("State: ");       Serial.println(status.state  ? "true" : "false");
    Serial.print("PTT: ");         Serial.println(status.ptt    ? "true" : "false");
    Serial.print("Band: ");        Serial.println(status.band);
    Serial.print("PWM Pump: ");    Serial.println(status.pwm_pump);
    Serial.print("PWM Cooler: ");  Serial.println(status.pwm_cooler);
    Serial.print("Auto PWM Pump: ");Serial.println(status.auto_pwm_pump ? "true" : "false");
    Serial.print("Auto PWM Fan: "); Serial.println(status.auto_pwm_fan  ? "true" : "false");
    Serial.println("===================");
}

void debugSettingsData() {
    Serial.println("=== SETTINGS DATA ===");
    Serial.print("Max SWR: ");            Serial.println(settings.max_swr);
    Serial.print("Max Current: ");        Serial.println(settings.max_current);
    Serial.print("Max Voltage: ");        Serial.println(settings.max_voltage);
    Serial.print("Max Water Temp: ");     Serial.println(settings.max_water_temp);
    Serial.print("Max Plate Temp: ");     Serial.println(settings.max_plate_temp);
    Serial.print("Max Pump Speed Temp: ");Serial.println(settings.max_pump_speed_temp);
    Serial.print("Min Pump Speed Temp: ");Serial.println(settings.min_pump_speed_temp);
    Serial.print("Max Fan Speed Temp: "); Serial.println(settings.max_fan_speed_temp);
    Serial.print("Min Fan Speed Temp: "); Serial.println(settings.min_fan_speed_temp);
    Serial.print("Min Coeff: ");          Serial.println(settings.min_coeff);
    Serial.print("Max input power: ");    Serial.println(settings.max_input_power);
    Serial.print("Autoband: ");           Serial.println(settings.autoband ? "true" : "false");
    Serial.print("Default Band: ");       Serial.println(settings.default_band);
    Serial.println("====================");
}

void debugCalibrationData() {
    Serial.println("=== CALIBRATION DATA ===");
    Serial.print("Low FWD Coeff: ");  Serial.println(calibration.low_fwd_coeff,  4);
    Serial.print("Low REV Coeff: ");  Serial.println(calibration.low_rev_coeff,  4);
    Serial.print("Low IFWD Coeff: "); Serial.println(calibration.low_ifwd_coeff, 4);
    Serial.print("Mid FWD Coeff: ");  Serial.println(calibration.mid_fwd_coeff,  4);
    Serial.print("Mid REV Coeff: ");  Serial.println(calibration.mid_rev_coeff,  4);
    Serial.print("Mid IFWD Coeff: "); Serial.println(calibration.mid_ifwd_coeff, 4);
    Serial.print("High FWD Coeff: "); Serial.println(calibration.high_fwd_coeff, 4);
    Serial.print("High REV Coeff: "); Serial.println(calibration.high_rev_coeff, 4);
    Serial.print("High IFWD Coeff: ");Serial.println(calibration.high_ifwd_coeff,4);
    Serial.print("Voltage Coeff: ");  Serial.println(calibration.voltage_coeff,  4);
    Serial.print("Current Coeff: ");  Serial.println(calibration.current_coeff,  4);
    Serial.print("Reserve Coeff: ");  Serial.println(calibration.rsrv_coeff,     4);
    Serial.print("ACS zero: ");       Serial.println(calibration.acs_zero,       4);
    Serial.print("ACS sens: ");       Serial.println(calibration.acs_sens,       4);
    Serial.println("=======================");
}

// ─── ACK parser ───────────────────────────────────────────────────────────────

static bool parseAckResponse(const char* json, ResponseType expectedResponse) {
    if (strstr(json, "\"response\":\"settings updated\"") != NULL) {
#if DEBUG
        Serial.println("Settings update RESPONSE!");
#endif
        return expectedResponse == RESPONSE_SETTINGS_SEND;
    }
    if (strstr(json, "\"response\":\"state updated\"") != NULL) {
#if DEBUG
        Serial.println("Status update RESPONSE!");
#endif
        return expectedResponse == RESPONSE_STATE_SEND;
    }
    if (strstr(json, "\"response\":\"calibration updated\"") != NULL) {
#if DEBUG
        Serial.println("Calibration update RESPONSE!");
#endif
        return expectedResponse == RESPONSE_CALIBRATION_SEND;
    }
    return false;
}

// ─── Send functions ───────────────────────────────────────────────────────────

void sendStatusRequest() {
#if DEBUG
    if (waitingForResponse == RESPONSE_NONE)
        Serial.println("[DEBUG] Sending status request command");
#endif
    Serial1.println("{\"command\":{\"value\":\"status\"}}");
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
    waitingForResponse   = RESPONSE_SETTINGS_REQUEST;
    responseRequestTime  = millis();
}

void sendSettingsData() {
#if DEBUG
    Serial.print("[DEBUG] Sending settings data");
    if (responseRetryCount > 0) {
        Serial.print(" (retry "); Serial.print(responseRetryCount);
        Serial.print("/"); Serial.print(MAX_RETRIES); Serial.print(")");
    }
    Serial.println();
#endif

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
        "\"max_input_power\":%d,"
        "\"min_coeff\":%d,"
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
        settings.max_input_power,
        settings.min_coeff,
        settings.autoband ? "true" : "false",
        settings.default_band
    );

#if DEBUG
    Serial.print("[DEBUG] JSON: "); Serial.println(json);
#endif
    Serial1.println(json);
    waitingForResponse  = RESPONSE_SETTINGS_SEND;
    delay(1000);
    responseRequestTime = millis();
}

void sendStateData() {
#if DEBUG
    Serial.print("[DEBUG] Sending state data");
    if (responseRetryCount > 0) {
        Serial.print(" (retry "); Serial.print(responseRetryCount);
        Serial.print("/"); Serial.print(MAX_RETRIES); Serial.print(")");
    }
    Serial.println();
#endif

    memcpy(&pendingState, &state, sizeof(StateData));

    char json[256];
    char temp[10];

    strcpy(json, "{\"state\":{");
    strcat(json, "\"alarm\":"); strcat(json, state.alarm  ? "true" : "false"); strcat(json, ",");
    strcat(json, "\"enabled\":"); strcat(json, state.state ? "true" : "false"); strcat(json, ",");
    strcat(json, "\"protection_enabled\":"); strcat(json, status.protection_enabled ? "true" : "false"); strcat(json, ",");
    strcat(json, "\"ptt\":"); strcat(json, state.ptt ? "true" : "false"); strcat(json, ",");
    strcat(json, "\"pwm_pump\":"); itoa(state.pwm_pump,   temp, 10); strcat(json, temp); strcat(json, ",");
    strcat(json, "\"pwm_cooler\":"); itoa(state.pwm_cooler, temp, 10); strcat(json, temp); strcat(json, ",");
    strcat(json, "\"band\":\""); strcat(json, state.band); strcat(json, "\",");
    strcat(json, "\"auto_pwm_pump\":"); strcat(json, state.auto_pwm_pump ? "true" : "false"); strcat(json, ",");
    strcat(json, "\"auto_pwm_fan\":"); strcat(json, state.auto_pwm_fan  ? "true" : "false");
    strcat(json, "}}");

    Serial1.println(json);
    Serial.println(json);
    waitingForResponse  = RESPONSE_STATE_SEND;
    responseRequestTime = millis();
}

void sendCalibrationCommand() {
#if DEBUG
    Serial.print("[DEBUG] Sending calibration request command");
    if (responseRetryCount > 0) {
        Serial.print(" (retry "); Serial.print(responseRetryCount);
        Serial.print("/"); Serial.print(MAX_RETRIES); Serial.print(")");
    }
    Serial.println();
#endif
    Serial1.println("{\"command\":{\"value\":\"calibration\"}}");
    waitingForResponse  = RESPONSE_CALIBRATION_REQUEST;
    responseRequestTime = millis();
}

void sendCalibrationData() {
#if DEBUG
    Serial.print("[DEBUG] Sending calibration data");
    if (responseRetryCount > 0) {
        Serial.print(" (retry "); Serial.print(responseRetryCount);
        Serial.print("/"); Serial.print(MAX_RETRIES); Serial.print(")");
    }
    Serial.println();
#endif

    char json[768];
    snprintf(json, sizeof(json),
        "{\"calibration\":{"
        "\"low_fwd_coeff\":%.4f,"
        "\"low_rev_coeff\":%.4f,"
        "\"low_ifwd_coeff\":%.4f,"
        "\"mid_fwd_coeff\":%.4f,"
        "\"mid_rev_coeff\":%.4f,"
        "\"mid_ifwd_coeff\":%.4f,"
        "\"high_fwd_coeff\":%.4f,"
        "\"high_rev_coeff\":%.4f,"
        "\"high_ifwd_coeff\":%.4f,"
        "\"voltage_coeff\":%.4f,"
        "\"current_coeff\":%.4f,"
        "\"rsrv_coeff\":%.4f,"
        "\"acs_zero\":%.4f,"
        "\"acs_sens\":%.4f}}",
        calibration.low_fwd_coeff,  calibration.low_rev_coeff,  calibration.low_ifwd_coeff,
        calibration.mid_fwd_coeff,  calibration.mid_rev_coeff,  calibration.mid_ifwd_coeff,
        calibration.high_fwd_coeff, calibration.high_rev_coeff, calibration.high_ifwd_coeff,
        calibration.voltage_coeff,  calibration.current_coeff,  calibration.rsrv_coeff,
        calibration.acs_zero,       calibration.acs_sens
    );

#if DEBUG
    Serial.print("[DEBUG] Calibration JSON: "); Serial.println(json);
#endif
    Serial1.println(json);
    waitingForResponse  = RESPONSE_CALIBRATION_SEND;
    delay(1000);
    responseRequestTime = millis();
}

// ─── Blocking request helpers ─────────────────────────────────────────────────

bool requestAndWaitForSettings(unsigned long timeout) {
#if DEBUG
    Serial.println("[DEBUG] Starting settings request with waiting");
#endif
    waitingForResponse  = RESPONSE_NONE;
    responseRetryCount  = 0;
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

bool requestAndWaitForCalibration(unsigned long timeout) {
#if DEBUG
    Serial.println("[DEBUG] Starting calibration request with waiting");
#endif
    waitingForResponse  = RESPONSE_NONE;
    responseRetryCount  = 0;
    sendCalibrationCommand();

    unsigned long startTime = millis();
    while (millis() - startTime < timeout) {
        handleUARTData();
        if (waitingForResponse == RESPONSE_NONE) {
            if (calibration.low_fwd_coeff > 0 || calibration.voltage_coeff > 0) {
#if DEBUG
                Serial.println("[DEBUG] Calibration received successfully");
                debugCalibrationData();
#endif
                return true;
            }
        }
        handleResponseRetry();
    }
#if DEBUG
    Serial.println("[DEBUG] Calibration request timeout");
#endif
    return false;
}

// ─── Data processors (internal) ───────────────────────────────────────────────

static void processParsedData() {
    if (waitingForResponse != RESPONSE_NONE) return;
#if DEBUG
    Serial.println("[DEBUG] Processing status data");
    debugStatusData();
#endif
    if (strcmp(getCurrentScreenName(), "main") == 0) {
        lv_label_set_text(ui_pwrTxt, (String(status.fwd) + "W").c_str());
        lv_bar_set_value(ui_pwrBar, int(status.fwd), LV_ANIM_ON);

        lv_color_t pttColor = status.ptt ? lv_color_hex(0xFF0000) : lv_color_hex(0x007AFF);
        lv_obj_set_style_bg_color(ui_pwrBar,  pttColor, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(ui_menuBtn, pttColor, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(ui_Button1, pttColor, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(ui_Button2, pttColor, LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_label_set_text(ui_swrValue, String(status.swr).c_str());
        lv_label_set_text(ui_refTxt,   (String(status.ref)          + "W").c_str());
        lv_label_set_text(ui_volTxt,   (String(status.voltage, 1)   + "V").c_str());
        lv_label_set_text(ui_current,  (String(status.current, 1)   + "A").c_str());
        lv_label_set_text(ui_waterTmp, (String(status.water_temp, 1)+ "C").c_str());
        lv_label_set_text(ui_plateTmp, (String(status.plate_temp, 1)+ "C").c_str());
        lv_label_set_text(ui_coeff,    (String(status.coeff, 1)     + "%").c_str());
        lv_label_set_text(ui_pumpSTxt, (String(status.pwm_pump)     + "%").c_str());
        lv_label_set_text(ui_fanSTxt,  (String(status.pwm_cooler)   + "%").c_str());
        set_switch_state(ui_mainSwitch, status.state);
        lv_label_set_text(ui_Label2,   String(status.band).c_str());
        strncpy(state.band, status.band, sizeof(state.band) - 1);
        lv_label_set_text(ui_iPWRTxt,  (String(status.trxfwd) + "W").c_str());
    }
}

static void processSettingsData() {
#if DEBUG
    Serial.println("[DEBUG] Processing settings data");
    debugSettingsData();
#endif
    if (waitingForResponse == RESPONSE_SETTINGS_REQUEST) {
        waitingForResponse = RESPONSE_NONE;
        responseRetryCount = 0;
    }
}

static void processCalibrationData() {
#if DEBUG
    Serial.println("[DEBUG] Processing calibration data");
    debugCalibrationData();
#endif
    if (waitingForResponse == RESPONSE_CALIBRATION_REQUEST) {
        waitingForResponse = RESPONSE_NONE;
        responseRetryCount = 0;
    }
}

static void processAckResponse(ResponseType /*expectedResponse*/) {
#if DEBUG
    Serial.println("[DEBUG] Received ACK confirmation");
#endif
    waitingForResponse = RESPONSE_NONE;
    responseRetryCount = 0;
}

// ─── UART receive loop ────────────────────────────────────────────────────────

void handleUARTData() {
    while (Serial1.available()) {
        char c = Serial1.read();

        if (c == '\n') {
            if (dataIndex > 0) {
                receivedData[dataIndex] = 0;
#if DEBUG
                Serial.println(receivedData);
#endif
                // Check for ACK first
                if (waitingForResponse != RESPONSE_NONE) {
                    if (parseAckResponse(receivedData, waitingForResponse)) {
                        processAckResponse(waitingForResponse);
                        dataIndex = 0;
                        receivedData[0] = 0;
                        continue;
                    }
                }

                // Parse data types
                if (parseSettingsJson(receivedData)) {
                    processSettingsData();
                } else if (parseCalibrationJson(receivedData)) {
                    processCalibrationData();
                } else {
                    if (parseStatusJson(receivedData))
                        processParsedData();
                }

                dataIndex = 0;
                receivedData[0] = 0;
            }
        } else if (c != '\r' && dataIndex < 511) {
            receivedData[dataIndex++] = c;
        }
    }
}

// ─── Retry state machine ──────────────────────────────────────────────────────

void handleResponseRetry() {
    if (waitingForResponse == RESPONSE_NONE) return;

    if (millis() - responseRequestTime >= RETRY_INTERVAL) {
        if (responseRetryCount < MAX_RETRIES) {
            responseRetryCount++;
            switch (waitingForResponse) {
                case RESPONSE_SETTINGS_REQUEST:   sendSettingsCommand();   ResponseTypeString = "settings request";   break;
                case RESPONSE_SETTINGS_SEND:      sendSettingsData();      ResponseTypeString = "settings send";      break;
                case RESPONSE_STATE_SEND:         sendStateData();         ResponseTypeString = "state send";         break;
                case RESPONSE_CALIBRATION_REQUEST:sendCalibrationCommand();ResponseTypeString = "calibration request";break;
                case RESPONSE_CALIBRATION_SEND:   sendCalibrationData();   ResponseTypeString = "calibration send";   break;
                default:
                    waitingForResponse = RESPONSE_NONE;
                    responseRetryCount = 0;
                    break;
            }
        } else {
#if DEBUG
            Serial.println("[DEBUG] Max retries reached, giving up");
#endif
            webErrorMessage = "Max retries (" + String(MAX_RETRIES) + ") reached for " + ResponseTypeString;
            hasWebError     = true;
            lv_label_set_text(ui_alertReason, ("Max retries for " + ResponseTypeString).c_str());
            lv_scr_load(ui_warning);
            waitingForResponse = RESPONSE_NONE;
            responseRetryCount = 0;
        }
    }
}

// ─── Test auto-requests ───────────────────────────────────────────────────────

void handleTestRequests() {
#if TEST_UART
    if (waitingForResponse != RESPONSE_NONE) return;
    if (millis() - lastTestRequest >= TEST_REQUEST_INTERVAL) {
        lastTestRequest = millis();
#if DEBUG
        Serial.println("[TEST_UART] Sending automatic settings request");
#endif
        responseRetryCount = 0;
        sendSettingsCommand();
    }
#endif
}
