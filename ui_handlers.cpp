#include "ui_handlers.h"
#include "uart_handler.h"
#include "wifi_manager.h"

// PINS_JC4827W543.h defines gfx, bus, GFX_BL — included ONLY in this file.
// All display operations go through initDisplay() and the callbacks below.
#include <PINS_JC4827W543.h>

extern bool warningDismissed;

// ─── Display init ─────────────────────────────────────────────────────────────

bool initDisplay() {
    if (!gfx->begin()) {
        LOG_WARN("gfx->begin() failed!");
        return false;
    }
    pinMode(GFX_BL, OUTPUT);
    digitalWrite(GFX_BL, HIGH);
    gfx->fillScreen(RGB565_BLACK);
    return true;
}

// ─── Band button table ────────────────────────────────────────────────────────
// Single source of truth for the band-selection buttons. The addresses of the
// ui_Button* globals are stable (the pointers themselves are populated by
// ui_init()), so both bandOpened() and set_band() iterate this one table.
struct BandButton { lv_obj_t** btn; const char* band; };
static const BandButton BAND_BUTTONS[] = {
    {&ui_Button160m, "160m"}, {&ui_Button80m,  "80m"},
    {&ui_Button60m,  "60m"},  {&ui_Button40m,  "40m"},
    {&ui_Button30m,  "30m"},  {&ui_Button20m,  "20m"},
    {&ui_Button17m,  "17m"},  {&ui_Button15m,  "15m"},
    {&ui_Button12m,  "12m"},  {&ui_Button10m,  "10m"},
    {&ui_Button6m,   "6m"},
};

// ─── LVGL display callbacks ───────────────────────────────────────────────────

void my_print(lv_log_level_t level, const char *buf) {
    LV_UNUSED(level);
    if (waitingForResponse == RESPONSE_NONE) {
        Serial.println(buf);
        Serial.flush();
    }
}

uint32_t millis_cb(void) { return millis(); }

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
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// ─── Utility ─────────────────────────────────────────────────────────────────

void set_switch_state(lv_obj_t *sw, bool st) {
    if (st) lv_obj_add_state(sw, LV_STATE_CHECKED);
    else    lv_obj_clear_state(sw, LV_STATE_CHECKED);
}

void dropdown_set_by_text(lv_obj_t *dropdown, const char *text) {
    const char *options      = lv_dropdown_get_options(dropdown);
    uint16_t    option_count = lv_dropdown_get_option_count(dropdown);
    for (uint16_t i = 0; i < option_count; i++) {
        const char *option_start = options;
        while (*options != '\n' && *options != '\0') options++;
        size_t len = options - option_start;
        if (strlen(text) == len && strncmp(option_start, text, len) == 0) {
            lv_dropdown_set_selected(dropdown, i);
            return;
        }
        if (*options == '\n') options++;
    }
}

String getUptime() {
    unsigned long up = millis() / 1000;
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%lud %02lu:%02lu:%02lu",
             up / 86400, (up % 86400) / 3600, (up % 3600) / 60, up % 60);
    return String(buffer);
}

// ─── Screen event handlers ────────────────────────────────────────────────────

void mainRightLoaded(lv_event_t * /*e*/) {
    lv_label_set_text(ui_SSID,   WiFi.SSID().c_str());
    lv_label_set_text(ui_ipADDR, WiFi.localIP().toString().c_str());
    lv_label_set_text(ui_uptime, getUptime().c_str());
}

void protectionOpened(lv_event_t * /*e*/) {
    LOG_INFO("Opening protection screen, requesting settings");
    if (!requestAndWaitForSettings(300)) return;

    lv_label_set_text(ui_maxSWR,          String(settings.max_swr).c_str());
    lv_slider_set_value(ui_swrSlider,     settings.max_swr, LV_ANIM_OFF);
    lv_label_set_text(ui_maxCurr,         String(settings.max_current).c_str());
    lv_slider_set_value(ui_currentSlider, settings.max_current, LV_ANIM_OFF);
    lv_label_set_text(ui_maxVoltage,      String(settings.max_voltage).c_str());
    lv_slider_set_value(ui_voltageSlider, settings.max_voltage, LV_ANIM_OFF);
    lv_label_set_text(ui_maxPlateTemp,    String(settings.max_plate_temp).c_str());
    lv_slider_set_value(ui_plateTmpSlider,settings.max_plate_temp, LV_ANIM_OFF);
    lv_label_set_text(ui_maxWaterTemp,    String(settings.max_water_temp).c_str());
    lv_slider_set_value(ui_waterTmpSlider,settings.max_water_temp, LV_ANIM_OFF);

    lv_label_set_text(ui_maxPumpSpeedTmp,         String(settings.max_pump_speed_temp).c_str());
    lv_slider_set_value(ui_maxPumpSpdTmpSlider,   settings.max_pump_speed_temp, LV_ANIM_OFF);
    lv_label_set_text(ui_minPumpSpeedTmp,         String(settings.min_pump_speed_temp).c_str());
    lv_slider_set_value(ui_minPumpSpdTmpSlider,   settings.min_pump_speed_temp, LV_ANIM_OFF);
    lv_label_set_text(ui_maxFanSpeedTmp,          String(settings.max_fan_speed_temp).c_str());
    lv_slider_set_value(ui_maxFanSpdTmpSlider,    settings.max_fan_speed_temp, LV_ANIM_OFF);
    lv_label_set_text(ui_minFanSpeedTmp,          String(settings.min_fan_speed_temp).c_str());
    lv_slider_set_value(ui_minFanSpdTmpSlider,    settings.min_fan_speed_temp, LV_ANIM_OFF);

    lv_label_set_text(ui_minCoeff,        String(settings.min_coeff).c_str());
    lv_slider_set_value(ui_minCoeffSlider,settings.min_coeff, LV_ANIM_OFF);
    lv_label_set_text(ui_maxIPWR,         String(settings.max_input_power).c_str());
    lv_slider_set_value(ui_maxIPWRSlider, settings.max_input_power, LV_ANIM_OFF);

    set_switch_state(ui_protectionSwitch, status.protection_enabled);
    dropdown_set_by_text(ui_defaultBandDropdown, settings.default_band);
}

void bandOpened(lv_event_t * /*e*/) {
    for (auto &m : BAND_BUTTONS) {
        bool active = strcmp(state.band, m.band) == 0;
        lv_obj_set_style_bg_color(*m.btn,
            active ? lv_color_hex(0xC00000) : lv_color_hex(0x2196F3),
            LV_PART_MAIN);
    }
    if (requestAndWaitForSettings(300))
        set_switch_state(ui_autoSelectSwitch, settings.autoband);
}

void resetAlert(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target_obj(e);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_CLICKABLE);

    if (status.alarm) {
        state.alarm  = false;  status.alarm  = false;
        status.ptt   = false;  state.ptt     = false;
        state.state  = false;  status.state  = false;
        sendStateData();
    }

    warningDismissed = true;
    lv_label_set_text(ui_alertReason, "Delaying...");

    lv_timer_t *t = lv_timer_create([](lv_timer_t *timer) {
        lv_obj_t *b = (lv_obj_t *)lv_timer_get_user_data(timer);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);  // восстановить
        lv_timer_del(timer);
        if (!status.alarm) {
            lv_scr_load(ui_main);
        }
    }, 1000, btn);
    lv_timer_set_repeat_count(t, 1);
}

// ─── Settings UI handlers ─────────────────────────────────────────────────────

void settingsNext(lv_event_t * /*e*/) {
    settings.max_swr         = lv_slider_get_value(ui_swrSlider);
    settings.max_current     = lv_slider_get_value(ui_currentSlider);
    settings.max_voltage     = lv_slider_get_value(ui_voltageSlider);
    settings.max_plate_temp  = lv_slider_get_value(ui_plateTmpSlider);
    settings.max_water_temp  = lv_slider_get_value(ui_waterTmpSlider);
    settings.max_input_power = lv_slider_get_value(ui_maxIPWRSlider);
}

void saveSettings(lv_event_t * /*e*/) {
    if (backendBusy() || isTransmitting()) return;   // EEPROM write: not while busy or transmitting
    settings.max_pump_speed_temp = lv_slider_get_value(ui_maxPumpSpdTmpSlider);
    settings.min_pump_speed_temp = lv_slider_get_value(ui_minPumpSpdTmpSlider);
    settings.max_fan_speed_temp  = lv_slider_get_value(ui_maxFanSpdTmpSlider);
    settings.min_fan_speed_temp  = lv_slider_get_value(ui_minFanSpdTmpSlider);
    settings.min_coeff           = lv_slider_get_value(ui_minCoeffSlider);
    status.protection_enabled    = lv_obj_get_state(ui_protectionSwitch) & LV_STATE_CHECKED;
    lv_dropdown_get_selected_str(ui_defaultBandDropdown, settings.default_band, sizeof(settings.default_band));

    if (settings.max_pump_speed_temp < settings.min_pump_speed_temp)
        settings.max_pump_speed_temp = settings.min_pump_speed_temp;
    if (settings.max_fan_speed_temp < settings.min_fan_speed_temp)
        settings.max_fan_speed_temp = settings.min_fan_speed_temp;

    LOG_INFO("Settings saved from UI, pushing to backend");
    sendStateData(false);   // fire-and-forget; the settings send below is tracked
    sendSettingsData();
}

void toggleAutoBand(lv_event_t * /*e*/) {
    if (backendBusy() || isTransmitting()) {
        set_switch_state(ui_autoSelectSwitch, settings.autoband);   // revert the visual toggle
        return;
    }
    settings.autoband = lv_obj_get_state(ui_autoSelectSwitch) & LV_STATE_CHECKED;
    sendSettingsData();
}

// ─── Control event handlers ───────────────────────────────────────────────────

void set_band(lv_event_t *e) {
    lv_obj_t *target = lv_event_get_target_obj(e);
    if (backendBusy() || isTransmitting()) return;   // no band change mid-transaction or during TX
    memset(state.band, 0, sizeof(state.band));

    for (auto &m : BAND_BUTTONS) {
        if (target == *m.btn) {
            strncpy(state.band, m.band, sizeof(state.band) - 1);
            LOG_INFO("Band set: %s", state.band);
            sendStateData();
            return;
        }
    }
    LOG_WARN("Unknown band button!");
}

void enableAmp(lv_event_t *e) {
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
    if (backendBusy() || isTransmitting()) {
        set_switch_state(sw, state.state);   // revert the visual toggle
        return;
    }
    state.state  = lv_obj_has_state(sw, LV_STATE_CHECKED);
    LOG_INFO("Amplifier: %s", state.state ? "ON" : "OFF");
    sendStateData();
}

void togglePTT(lv_event_t *e) {
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);

    // PTT is the TX control itself, so it is NOT blocked during TX — you must
    // always be able to unkey. Only refuse while the backend is mid EEPROM
    // write, when the command would be lost anyway.
    if (backendDeaf()) return;

    if (state.ptt) {
        state.ptt = false;
        lv_obj_set_style_bg_color(sw, lv_color_hex(0x2196F3), LV_PART_INDICATOR);
        LOG_INFO("PTT: OFF");
    } else if (state.state) {
        state.ptt = true;
        lv_obj_set_style_bg_color(sw, lv_color_hex(0xFF0000), LV_PART_INDICATOR);
        LOG_INFO("PTT: ON");
    }
    sendStateData();
}

// ─── WiFi UI ──────────────────────────────────────────────────────────────────

static void saveWiFiSettingsEventHandler(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        const char* ssid_text     = lv_textarea_get_text(ui_TextArea1);
        const char* password_text = lv_textarea_get_text(ui_TextArea3);
        saveWiFiCredentials(ssid_text, password_text);
        connectToWiFi();
        LOG_INFO("WiFi settings saved via LVGL button");
    }
}

void setupLVGLButtonHandler() {
    lv_obj_add_event_cb(ui_Button10, saveWiFiSettingsEventHandler, LV_EVENT_CLICKED, NULL);
}

// ui_mainLeft gauge dashboard

// Recolour a gauge's arc as its value nears (amber) / crosses (red) its
// protection limit; green otherwise. state: -1 uninit, 0 ok, 1 warn, 2 alarm.
// alarmAt<=0 means the limit isn't known yet (settings not received) -> green.
static void colorArc(lv_obj_t* arc, int8_t* state, float v, float warnAt, float alarmAt) {
    int8_t s = (alarmAt <= 0) ? 0 : (v >= alarmAt) ? 2 : (v >= warnAt) ? 1 : 0;
    if (s != *state) {
        *state = s;
        lv_color_t c = (s == 2) ? lv_color_hex(0xE00000)
                     : (s == 1) ? lv_color_hex(0xE07000) : lv_color_hex(0x00A000);
        lv_obj_set_style_arc_color(arc, c, LV_PART_INDICATOR);
    }
}

// Push the live telemetry onto the ui_mainLeft gauge dashboard. Each figure is
// gated on its last value so a static reading (e.g. during RX) does no work.
void updateGauges(bool force) {
    if (!ui_mainLeft) return;
    char b[24];
    static float lPwr = -1, lSwr = -1, lWat = -1, lPlt = -1, lCur = -1, lVol = -1, lCoe = -1;
    static int8_t sSwr = -1, sWat = -1, sPlt = -1;
    // On (re)open force a full repaint: a value unchanged since the screen was
    // last shown would otherwise be gated out, leaving the labels stale.
    if (force) { lPwr = lSwr = lWat = lPlt = lCur = lVol = lCoe = -1; sSwr = sWat = sPlt = -1; }

    if (status.fwd != lPwr) {
        lPwr = status.fwd;
        lv_arc_set_value(ui_gPwr, (int)status.fwd);
        snprintf(b, sizeof(b), "%.0fW", status.fwd); lv_label_set_text(ui_gPwrVal, b);
    }
    if (status.swr != lSwr) {
        lSwr = status.swr;
        lv_arc_set_value(ui_gSwr, (int)(status.swr * 10));
        snprintf(b, sizeof(b), "%.2f", status.swr); lv_label_set_text(ui_gSwrVal, b);
    }
    colorArc(ui_gSwr, &sSwr, status.swr, 0.8f * settings.max_swr, settings.max_swr);
    if (status.water_temp != lWat) {
        lWat = status.water_temp;
        lv_arc_set_value(ui_gWater, (int)status.water_temp);
        snprintf(b, sizeof(b), "%.0fC", status.water_temp); lv_label_set_text(ui_gWaterVal, b);
    }
    colorArc(ui_gWater, &sWat, status.water_temp, 0.9f * settings.max_water_temp, settings.max_water_temp);
    if (status.plate_temp != lPlt) {
        lPlt = status.plate_temp;
        lv_arc_set_value(ui_gPlate, (int)status.plate_temp);
        snprintf(b, sizeof(b), "%.0fC", status.plate_temp); lv_label_set_text(ui_gPlateVal, b);
    }
    colorArc(ui_gPlate, &sPlt, status.plate_temp, 0.9f * settings.max_plate_temp, settings.max_plate_temp);
    if (status.current != lCur) {
        lCur = status.current;
        snprintf(b, sizeof(b), "%.1fA", status.current); lv_label_set_text(ui_gCur, b);
    }
    if (status.voltage != lVol) {
        lVol = status.voltage;
        snprintf(b, sizeof(b), "%.1fV", status.voltage); lv_label_set_text(ui_gVol, b);
    }
    if (status.coeff != lCoe) {
        lCoe = status.coeff;
        snprintf(b, sizeof(b), "%.0f%%", status.coeff); lv_label_set_text(ui_gCoeff, b);
    }
}

// Append the latest telemetry sample to the three trend sparklines. Called at
// ~1 Hz from the UART path so ~100 s of history scrolls across each chart. The
// charts live for the whole program lifetime, so they keep accruing even while
// ui_mainLeft isn't the active screen and are already current when swiped to.
void pushSparklines(void) {
    if (!ui_spPwr) return;
    lv_chart_series_t* s;
    s = lv_chart_get_series_next(ui_spPwr,   NULL); if (s) lv_chart_set_next_value(ui_spPwr,   s, (int32_t)status.fwd);
    s = lv_chart_get_series_next(ui_spWater, NULL); if (s) lv_chart_set_next_value(ui_spWater, s, (int32_t)status.water_temp);
    s = lv_chart_get_series_next(ui_spPlate, NULL); if (s) lv_chart_set_next_value(ui_spPlate, s, (int32_t)status.plate_temp);
}

// Bound to ui_mainLeft's SCREEN_LOADED: paint current values immediately so the
// gauges aren't blank until the next UART frame.
void graphOpened(lv_event_t * /*e*/) {
    updateGauges(true);
}