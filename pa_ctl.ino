// ─── Main entry point ─────────────────────────────────────────────────────────
// gfx/bus/GFX_BL live in ui_handlers.cpp (PINS_JC4827W543.h included there).
// This file only wires setup() and loop().

#include "globals.h"
#include "wifi_manager.h"
#include "uart_handler.h"
#include "ui_handlers.h"
#include "web.h"
#include "ota.h"

bool warningDismissed = false;

// Maps a backend alarm reason to the text shown on the warning screen. Single
// source of truth, iterated below — same table style as BAND_BUTTONS.
struct AlarmText { const char* reason; const char* text; };
static const AlarmText ALARM_TEXTS[] = {
    {"no_output",      "Current, no output"},
    {"cur_sensor",     "Current sensor failed"},
    {"volt_sensor",    "Voltage sensor failed"},
    {"eff_high",       "Efficiency impossibly high"},
    {"swr",            "High SWR"},
    {"voltage",        "Overvoltage"},
    {"current",        "Overcurrent"},
    {"ipower",         "Input overdrive"},
    {"water_temp",     "Water overheating"},
    {"plate_temp",     "Plate overheating"},
    {"coeff",          "Low efficiency"},
    {"band",           "Unknown band"},
    {"wrong_band",     "Wrong band"},
    {"unk_freq_on_tx", "Frequency out of band"},
};

void setup() {
    Serial.begin(115200);
    Serial1.begin(115200, SERIAL_8N1, 18, 17);

    preferences.begin(PREF_NAMESPACE, false);
    loadWiFiCredentials();

    LOG_INFO("Power amplifier controller frontend started (log level %d%s)",
             LOG_LEVEL, TEST_UART ? ", TEST_UART" : "");

    // ── Display + touch init ──────────────────────────────────────────────────
    if (!initDisplay()) { while (true) {} }

    touchController.begin();
    touchController.setRotation(ROTATION_INVERTED);

    // ── LVGL init ─────────────────────────────────────────────────────────────
    lv_init();
    lv_tick_set_cb(millis_cb);
#if LV_USE_LOG != 0
    lv_log_register_print_cb(my_print);
#endif

    screenWidth  = 480;
    screenHeight = 272;
    bufSize      = screenWidth * 40;

    disp_draw_buf = (lv_color_t *)heap_caps_malloc(bufSize * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!disp_draw_buf)
        disp_draw_buf = (lv_color_t *)heap_caps_malloc(bufSize * 2, MALLOC_CAP_8BIT);
    if (!disp_draw_buf) {
        LOG_WARN("LVGL disp_draw_buf allocate failed!");
        while (true) {}
    }

    disp = lv_display_create(screenWidth, screenHeight);
    lv_display_set_flush_cb(disp, my_disp_flush);
    lv_display_set_buffers(disp, disp_draw_buf, NULL, bufSize * 2, LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, my_touchpad_read);

    ui_init();

    // ── Post-UI init ──────────────────────────────────────────────────────────
    setupLVGLButtonHandler();
    updateLVGLTextAreasWithSavedCredentials();

    // ── WiFi + web routes ─────────────────────────────────────────────────────
    connectToWiFi();
    registerWebRoutes();

    LOG_INFO("Setup done");
}

void loop() {
    lv_task_handler();

    unsigned long now = millis();

    // ── WiFi connection management ────────────────────────────────────────────
    if (isConnecting) {
        if (WiFi.status() == WL_CONNECTED) {
            LOG_INFO("Connected successfully");
            printWiFiStatus();

            if (!serverStarted) {
                server.begin();
                serverStarted = true;
                LOG_INFO("HTTP server started");
            }
            if (!otaStarted) {
                setupOTA();
                otaStarted = true;
                LOG_INFO("OTA server started");
            }
            isConnecting = false;

        } else if (now - connectionStartTime > CONNECTION_TIMEOUT) {
            LOG_WARN("WiFi connection timeout");
            isConnecting = false;
            WiFi.disconnect();
        }
    }

    if (!isConnecting && WiFi.status() != WL_CONNECTED) {
        if (now - lastWifiReconnectAttempt >= WIFI_RECONNECT_INTERVAL) {
            LOG_WARN("WiFi connection lost, reconnecting");
            connectToWiFi();
            lastWifiReconnectAttempt = now;
        }
    }

    // ── UART + protocol ───────────────────────────────────────────────────────
    handleUARTData();
    handleResponseRetry();
    handleTestRequests();

    // ── OTA + Web ─────────────────────────────────────────────────────────────
    if (otaStarted)    ArduinoOTA.handle();
    if (serverStarted) server.handleClient();
    
    static bool prev_alarm = false;
    if (status.alarm && !prev_alarm) {
        warningDismissed = false;
    }
    prev_alarm = status.alarm;
    // ── Alarm screen routing ──────────────────────────────────────────────────
    if (status.alarm && lv_scr_act() != ui_warning && !warningDismissed) {
        const char* alarmText = "Unknown error";
        for (auto &a : ALARM_TEXTS) {
            if (strcmp(status.alert_reason, a.reason) == 0) { alarmText = a.text; break; }
        }

        lv_label_set_text(ui_alertReason, alarmText);
        lv_scr_load(ui_warning);
    }
    
    // reset via web
    if (!warningDismissed && lv_scr_act() == ui_warning && !status.alarm) {
        warningDismissed = true;
        lv_scr_load(ui_main);
    }
    // Telemetry trend/gauge updates are driven from the UART status path
    // (pushSparklines at ~1 Hz + updateGauges); no separate sampling here.
}
