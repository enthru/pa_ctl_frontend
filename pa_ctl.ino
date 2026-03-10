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
#endif
    Serial.println("====================================");
#else
    Serial.println("Power amplifier controller frontend started");
#endif

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
        Serial.println("LVGL disp_draw_buf allocate failed!");
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

    Serial.println("Setup done");
}

void loop() {
    lv_task_handler();

    unsigned long now = millis();

    // ── WiFi connection management ────────────────────────────────────────────
    if (isConnecting) {
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\nConnected successfully!");
            printWiFiStatus();

            if (!serverStarted) {
                server.begin();
                serverStarted = true;
                Serial.println("HTTP server started");
            }
            if (!otaStarted) {
                setupOTA();
                otaStarted = true;
                Serial.println("OTA server started");
            }
            isConnecting = false;

        } else if (now - connectionStartTime > CONNECTION_TIMEOUT) {
            Serial.println("\nConnection timeout!");
            isConnecting = false;
            WiFi.disconnect();
        }
    }

    if (!isConnecting && WiFi.status() != WL_CONNECTED) {
        if (now - lastWifiReconnectAttempt >= WIFI_RECONNECT_INTERVAL) {
            Serial.println("WiFi connection lost. Reconnecting...");
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

    // ── Alarm screen routing ──────────────────────────────────────────────────
    if (status.alarm && lv_scr_act() != ui_warning) {
        const char* alarmText = "Unknown error";

        if      (String(status.alert_reason) == "water_temp") alarmText = "Water temperature too high";
        else if (String(status.alert_reason) == "plate_temp") alarmText = "Waterblock temperature too high";
        else if (String(status.alert_reason) == "coeff")      alarmText = "Low efficiency";
        else if (String(status.alert_reason) == "swr")        alarmText = "High SWR";
        else if (String(status.alert_reason) == "voltage")    alarmText = "Overvoltage";
        else if (String(status.alert_reason) == "current")    alarmText = "Current too high";
        else if (String(status.alert_reason) == "ipower")     alarmText = "High input power";
        else if (String(status.alert_reason) == "band")       alarmText = "Band error";

        lv_label_set_text(ui_alertReason, alarmText);
        lv_scr_load(ui_warning);
        warningDismissed = false;
    }
    
    // reset via web
    if (!warningDismissed && lv_scr_act() == ui_warning && !status.alarm) {
        warningDismissed = true;
        lv_scr_load(ui_main);
    }
}
