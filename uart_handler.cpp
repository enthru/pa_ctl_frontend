#include "uart_handler.h"
#include "ui_handlers.h"   // set_switch_state

// Owned by pa_ctl.ino. loop() auto-returns from ui_warning to ui_main only for
// alarm-driven warnings (it checks !warningDismissed). The comms-error screen
// below sets it true so that auto-return does not instantly swallow the error.
extern bool warningDismissed;

// ─── Forward declarations of data processors ─────────────────────────────────
static void processParsedData();
static void processSettingsData();
static void processCalibrationData();
static void processAckResponse(ResponseType expectedResponse);
static bool parseAckResponse(const char* json, ResponseType expectedResponse);

// ─── Debug helpers ────────────────────────────────────────────────────────────

// These dumps are one-shot diagnostics (a settings/calibration frame just
// arrived, or a manual request completed). They are NOT called per status
// frame — that would flood the UART hot path. Each line is TRACE-gated, so at
// LOG_LEVEL < 3 the whole body compiles to nothing.
void debugSettingsData() {
    LOG_TRACE("=== SETTINGS DATA ===");
    LOG_TRACE("Max SWR: %d  Current: %d  Voltage: %d",
              settings.max_swr, settings.max_current, settings.max_voltage);
    LOG_TRACE("Temp max water/plate: %d/%d", settings.max_water_temp, settings.max_plate_temp);
    LOG_TRACE("Pump temp min/max: %d/%d", settings.min_pump_speed_temp, settings.max_pump_speed_temp);
    LOG_TRACE("Fan temp min/max: %d/%d", settings.min_fan_speed_temp, settings.max_fan_speed_temp);
    LOG_TRACE("Min Coeff: %d  Max input power: %d", settings.min_coeff, settings.max_input_power);
    LOG_TRACE("Autoband: %d  Default Band: %s", settings.autoband, settings.default_band);
    LOG_TRACE("====================");
}

void debugCalibrationData() {
    LOG_TRACE("=== CALIBRATION DATA ===");
    LOG_TRACE("Low  FWD/REV/IFWD: %.4f/%.4f/%.4f",
              calibration.low_fwd_coeff, calibration.low_rev_coeff, calibration.low_ifwd_coeff);
    LOG_TRACE("Mid  FWD/REV/IFWD: %.4f/%.4f/%.4f",
              calibration.mid_fwd_coeff, calibration.mid_rev_coeff, calibration.mid_ifwd_coeff);
    LOG_TRACE("High FWD/REV/IFWD: %.4f/%.4f/%.4f",
              calibration.high_fwd_coeff, calibration.high_rev_coeff, calibration.high_ifwd_coeff);
    LOG_TRACE("Voltage: %.4f  Current: %.4f  Reserve: %.4f",
              calibration.voltage_coeff, calibration.current_coeff, calibration.rsrv_coeff);
    LOG_TRACE("ACS zero/sens: %.4f/%.4f", calibration.acs_zero, calibration.acs_sens);
    LOG_TRACE("=======================");
}

// ─── Backend-access gating ────────────────────────────────────────────────────
// The backend goes deaf for ~1s while it commits settings/calibration to EEPROM.
// These predicates replace the old blocking delay()s: instead of freezing the
// whole controller, every user/web action that talks to the backend checks them
// first and simply declines when it is not safe to send.

bool backendBusy() {
    // A transaction is already outstanding — don't start another one.
    return waitingForResponse != RESPONSE_NONE;
}

bool backendDeaf() {
    // Backend is mid EEPROM write; anything sent now is lost.
    return waitingForResponse == RESPONSE_SETTINGS_SEND ||
           waitingForResponse == RESPONSE_CALIBRATION_SEND;
}

bool isTransmitting() {
    // While keyed, configuration *changes* (settings/calibration save, band,
    // amp enable) are refused. Telemetry RX, config reads and PTT control are
    // still allowed.
    return state.ptt || status.ptt;
}

// ─── ACK parser ───────────────────────────────────────────────────────────────

static bool parseAckResponse(const char* json, ResponseType expectedResponse) {
    if (strstr(json, "\"response\":\"settings updated\"") != NULL) {
        LOG_TRACE("ACK: settings updated");
        return expectedResponse == RESPONSE_SETTINGS_SEND;
    }
    if (strstr(json, "\"response\":\"state updated\"") != NULL) {
        LOG_TRACE("ACK: state updated");
        return expectedResponse == RESPONSE_STATE_SEND;
    }
    if (strstr(json, "\"response\":\"calibration updated\"") != NULL) {
        LOG_TRACE("ACK: calibration updated");
        return expectedResponse == RESPONSE_CALIBRATION_SEND;
    }
    return false;
}

// ─── Send functions ───────────────────────────────────────────────────────────

void sendStatusRequest() {
    // Fires several times a second — TRACE only, never INFO.
    LOG_TRACE("Sending status request");
    Serial1.println("{\"command\":{\"value\":\"status\"}}");
}

void sendSettingsCommand() {
    LOG_INFO("Sending settings request");
    Serial1.println("{\"command\":{\"value\":\"settings\"}}");
    waitingForResponse   = RESPONSE_SETTINGS_REQUEST;
    responseRequestTime  = millis();
}

void sendSettingsData() {
    LOG_INFO("Sending settings data");

    memcpy(&pendingSettings, &settings, sizeof(SettingsData));

    char json[768];
    snprintf(json, sizeof(json),
        "{\"settings\":{"
        "\"max_swr\":%d,"
        "\"max_current\":%d,"
        "\"max_voltage\":%d,"
        "\"max_water_temp\":%d,"
        "\"max_plate_temp\":%d,"
        "\"max_pump_speed_temp\":%d,"
        "\"min_pump_speed_temp\":%d,"
        "\"max_fan_speed_temp\":%d,"
        "\"min_fan_speed_temp\":%d,"
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

    LOG_TRACE("Settings JSON: %s", json);
    Serial1.println(json);
    waitingForResponse  = RESPONSE_SETTINGS_SEND;
    responseRequestTime = millis();
    // No blocking delay here: the backend's ~1s EEPROM commit is absorbed by
    // SEND_RETRY_INTERVAL in handleResponseRetry(), and new commands are held
    // off by the backendBusy() guard at every entry point instead of by freezing
    // the whole controller.
}

void sendStateData(bool trackResponse) {
    LOG_INFO("Sending state data");

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
    strcat(json, "\"auto_pwm_pump\":"); strcat(json, status.auto_pwm_pump ? "true" : "false"); strcat(json, ",");
    strcat(json, "\"auto_pwm_fan\":"); strcat(json, status.auto_pwm_fan  ? "true" : "false");
    strcat(json, "}}");

    Serial1.println(json);
    LOG_TRACE("State JSON: %s", json);
    if (trackResponse) {
        waitingForResponse  = RESPONSE_STATE_SEND;
        responseRequestTime = millis();
    }
}

void sendCalibrationCommand() {
    LOG_INFO("Sending calibration request");
    Serial1.println("{\"command\":{\"value\":\"calibration\"}}");
    waitingForResponse  = RESPONSE_CALIBRATION_REQUEST;
    responseRequestTime = millis();
}

void sendCalibrationData() {
    LOG_INFO("Sending calibration data");

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

    LOG_TRACE("Calibration JSON: %s", json);
    Serial1.println(json);
    waitingForResponse  = RESPONSE_CALIBRATION_SEND;
    responseRequestTime = millis();
    // See sendSettingsData(): the EEPROM commit window is handled by
    // SEND_RETRY_INTERVAL + the backendBusy() guards, not by blocking here.
}

// ─── Blocking request helpers ─────────────────────────────────────────────────

bool requestAndWaitForSettings(unsigned long timeout) {
    LOG_TRACE("Starting settings request with waiting");
    waitingForResponse  = RESPONSE_NONE;
    responseRetryCount  = 0;
    settingsReceived    = false;
    sendSettingsCommand();

    unsigned long startTime = millis();
    while (millis() - startTime < timeout) {
        handleUARTData();
        if (settingsReceived) {
            LOG_INFO("Settings received");
            debugSettingsData();
            return true;
        }
        handleResponseRetry();
    }
    LOG_WARN("Settings request timeout after %lu ms", timeout);
    return false;
}

bool requestAndWaitForCalibration(unsigned long timeout) {
    LOG_TRACE("Starting calibration request with waiting");
    waitingForResponse  = RESPONSE_NONE;
    responseRetryCount  = 0;
    calibrationReceived = false;
    sendCalibrationCommand();

    unsigned long startTime = millis();
    while (millis() - startTime < timeout) {
        handleUARTData();
        if (calibrationReceived) {
            LOG_INFO("Calibration received");
            debugCalibrationData();
            return true;
        }
        handleResponseRetry();
    }
    LOG_WARN("Calibration request timeout after %lu ms", timeout);
    return false;
}

// ─── Data processors (internal) ───────────────────────────────────────────────

// Update a label only when its text actually changed. lv_label_set_text always
// reallocates the label's string and invalidates the widget (LVGL does not
// compare), so on the status hot path — where most values are static during RX
// — an unconditional set forces a needless repaint every frame. Caching the
// last text collapses those to zero work until a value moves. `cache` must be a
// static buffer per label of at least LABEL_CACHE bytes.
#define LABEL_CACHE 24
static void labelSetCached(lv_obj_t* obj, char* cache, const char* text) {
    if (strcmp(cache, text) != 0) {
        strncpy(cache, text, LABEL_CACHE - 1);
        cache[LABEL_CACHE - 1] = '\0';
        lv_label_set_text(obj, text);
    }
}

static void processParsedData() {
    if (waitingForResponse != RESPONSE_NONE) return;
    // No per-frame dump here: status frames arrive several times a second and
    // dumping every one would flood serial and stall this hot path. Enable
    // LOG_LEVEL 3 and inspect the raw "RX:" trace line instead if you need it.
    // Direct pointer compare — avoids building a screen-name string and strcmp'ing it.
    if (lv_scr_act() == ui_main) {
        // Format into a stack buffer instead of temporary Arduino Strings to
        // avoid heap churn/fragmentation on this per-frame hot path.
        char buf[LABEL_CACHE];
        // One persistent cache per label; skips the repaint when text is unchanged.
        static char cPwr[LABEL_CACHE], cSwr[LABEL_CACHE], cRef[LABEL_CACHE],
                    cVol[LABEL_CACHE], cCur[LABEL_CACHE], cWater[LABEL_CACHE],
                    cPlate[LABEL_CACHE], cCoeff[LABEL_CACHE], cPump[LABEL_CACHE],
                    cFan[LABEL_CACHE], cBand[LABEL_CACHE], cIPwr[LABEL_CACHE];

        snprintf(buf, sizeof(buf), "%.2fW", status.fwd);        labelSetCached(ui_pwrTxt, cPwr, buf);
        lv_bar_set_value(ui_pwrBar, int(status.fwd), LV_ANIM_ON);

        // PTT rarely changes but status frames arrive several times a second.
        // lv_obj_set_style_bg_color invalidates + repaints unconditionally, so
        // only recolour the four widgets when the PTT state actually flips.
        static int prevPtt = -1;
        if ((int)status.ptt != prevPtt) {
            prevPtt = status.ptt;
            lv_color_t pttColor = status.ptt ? lv_color_hex(0xFF0000) : lv_color_hex(0x007AFF);
            lv_obj_set_style_bg_color(ui_pwrBar,  pttColor, LV_PART_INDICATOR);
            lv_obj_set_style_bg_color(ui_menuBtn, pttColor, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(ui_Button1, pttColor, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(ui_Button2, pttColor, LV_PART_MAIN | LV_STATE_DEFAULT);
        }

        snprintf(buf, sizeof(buf), "%.2f",  status.swr);        labelSetCached(ui_swrValue, cSwr,  buf);
        snprintf(buf, sizeof(buf), "%.2fW", status.ref);        labelSetCached(ui_refTxt,   cRef,  buf);
        snprintf(buf, sizeof(buf), "%.1fV", status.voltage);    labelSetCached(ui_volTxt,   cVol,  buf);
        snprintf(buf, sizeof(buf), "%.1fA", status.current);    labelSetCached(ui_current,  cCur,  buf);
        snprintf(buf, sizeof(buf), "%.1fC", status.water_temp); labelSetCached(ui_waterTmp, cWater,buf);
        snprintf(buf, sizeof(buf), "%.1fC", status.plate_temp); labelSetCached(ui_plateTmp, cPlate,buf);
        snprintf(buf, sizeof(buf), "%.1f%%",status.coeff);      labelSetCached(ui_coeff,    cCoeff,buf);
        snprintf(buf, sizeof(buf), "%d%%",  status.pwm_pump);   labelSetCached(ui_pumpSTxt, cPump, buf);
        snprintf(buf, sizeof(buf), "%d%%",  status.pwm_cooler); labelSetCached(ui_fanSTxt,  cFan,  buf);
        // Amp-enable switch flips rarely; set_switch_state add/clear_state is
        // unconditional, so gate it like the PTT recolor above.
        static int prevState = -1;
        if ((int)status.state != prevState) {
            prevState = status.state;
            set_switch_state(ui_mainSwitch, status.state);
        }
        labelSetCached(ui_Label2,   cBand, status.band);
        // Mirror band into state.band only when it actually changed.
        if (strcmp(state.band, status.band) != 0) {
            strncpy(state.band, status.band, sizeof(state.band) - 1);
            state.band[sizeof(state.band) - 1] = '\0';
        }
        snprintf(buf, sizeof(buf), "%.2fW", status.trxfwd);     labelSetCached(ui_iPWRTxt,  cIPwr, buf);
    }
}

static void processSettingsData() {
    LOG_TRACE("Processing settings data");
    debugSettingsData();
    settingsReceived = true;
    if (waitingForResponse == RESPONSE_SETTINGS_REQUEST) {
        waitingForResponse = RESPONSE_NONE;
        responseRetryCount = 0;
    }
}

static void processCalibrationData() {
    LOG_TRACE("Processing calibration data");
    debugCalibrationData();
    calibrationReceived = true;
    if (waitingForResponse == RESPONSE_CALIBRATION_REQUEST) {
        waitingForResponse = RESPONSE_NONE;
        responseRetryCount = 0;
    }
}

static void processAckResponse(ResponseType /*expectedResponse*/) {
    LOG_TRACE("Received ACK confirmation");
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
                LOG_TRACE("RX: %s", receivedData);
                // Check for ACK first
                if (waitingForResponse != RESPONSE_NONE) {
                    if (parseAckResponse(receivedData, waitingForResponse)) {
                        processAckResponse(waitingForResponse);
                        dataIndex = 0;
                        receivedData[0] = 0;
                        continue;
                    }
                }

                // Parse data types. Status is by far the most frequent message,
                // so try it first to spare the common path two failed scans.
                if (parseStatusJson(receivedData)) {
                    processParsedData();
                } else if (parseSettingsJson(receivedData)) {
                    processSettingsData();
                } else if (parseCalibrationJson(receivedData)) {
                    processCalibrationData();
                } else {
                    // A frame from the backend that matched no known schema.
                    // Previously dropped silently — the single most useful thing
                    // to see when the link misbehaves, so surface it at WARN.
                    LOG_WARN("UART parse failed: %s", receivedData);
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

    // EEPROM writes need the longer window; everything else uses the fast one.
    unsigned long interval =
        (waitingForResponse == RESPONSE_SETTINGS_SEND ||
         waitingForResponse == RESPONSE_CALIBRATION_SEND)
        ? SEND_RETRY_INTERVAL : RETRY_INTERVAL;

    if (millis() - responseRequestTime >= interval) {
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
            // The backend missed a command — a real link anomaly worth seeing
            // even in production, not just under full trace.
            LOG_WARN("Retry %d/%d for %s", responseRetryCount, MAX_RETRIES, ResponseTypeString.c_str());
        } else {
            LOG_WARN("Max retries (%d) reached for %s, giving up", MAX_RETRIES, ResponseTypeString.c_str());
            webErrorMessage = "Max retries (" + String(MAX_RETRIES) + ") reached for " + ResponseTypeString;
            hasWebError     = true;
            // Only take over the physical screen for user-initiated sends.
            // Background read requests (settings/calibration) are also polled
            // from the web UI, and must not hijack the device display.
            if (waitingForResponse == RESPONSE_SETTINGS_SEND ||
                waitingForResponse == RESPONSE_STATE_SEND ||
                waitingForResponse == RESPONSE_CALIBRATION_SEND) {
                lv_label_set_text(ui_alertReason, ("Max retries for " + ResponseTypeString).c_str());
                lv_scr_load(ui_warning);
                // Mark dismissed so loop()'s alarm-return logic (which fires on
                // !warningDismissed && !status.alarm) doesn't wipe this comms
                // error off the screen on the very next iteration. It stays until
                // the user acknowledges it or a real alarm supersedes it.
                warningDismissed = true;
            }
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
        LOG_TRACE("[TEST_UART] Sending automatic settings request");
        responseRetryCount = 0;
        sendSettingsCommand();
    }
#endif
}
