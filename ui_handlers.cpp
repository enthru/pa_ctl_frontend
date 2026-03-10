#include "ui_handlers.h"
#include "uart_handler.h"
#include "wifi_manager.h"

// PINS_JC4827W543.h defines gfx, bus, GFX_BL — included ONLY in this file.
// All display operations go through initDisplay() and the callbacks below.
#include <PINS_JC4827W543.h>

// ─── Display init ─────────────────────────────────────────────────────────────

bool initDisplay() {
    if (!gfx->begin()) {
        Serial.println("gfx->begin() failed!");
        return false;
    }
    pinMode(GFX_BL, OUTPUT);
    digitalWrite(GFX_BL, HIGH);
    gfx->fillScreen(RGB565_BLACK);
    return true;
}

// ─── Screen name helper ───────────────────────────────────────────────────────

const char* getCurrentScreenName() {
    lv_obj_t* active = lv_scr_act();
    if (active == ui_main)       return "main";
    if (active == ui_menu)       return "menu";
    if (active == ui_protection) return "protection";
    if (active == ui_wifi)       return "wifi";
    if (active == ui_mainLeft)   return "mainLeft";
    if (active == ui_bands)      return "bands";
    if (active == ui_mainRight)  return "mainRight";
    if (active == ui_warning)    return "warning";
    if (active == ui_protection2)return "protection2";
    return "none";
}

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
    char buffer[20];
    snprintf(buffer, sizeof(buffer), "%lu:%02lu:%02lu",
             up / 86400, (up % 86400) / 3600, (up % 3600) / 60);
    return String(buffer);
}

// ─── Screen event handlers ────────────────────────────────────────────────────

void mainRightLoaded(lv_event_t * /*e*/) {
    lv_label_set_text(ui_SSID,   WiFi.SSID().c_str());
    lv_label_set_text(ui_ipADDR, WiFi.localIP().toString().c_str());
    lv_label_set_text(ui_uptime, getUptime().c_str());
}

void protectionOpened(lv_event_t * /*e*/) {
#if DEBUG
    Serial.println("[DEBUG] Sending settings request command");
#endif
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
    struct { lv_obj_t **btn; const char *band; } map[] = {
        {&ui_Button160m, "160m"}, {&ui_Button80m,  "80m"},
        {&ui_Button60m,  "60m"},  {&ui_Button40m,  "40m"},
        {&ui_Button30m,  "30m"},  {&ui_Button20m,  "20m"},
        {&ui_Button17m,  "17m"},  {&ui_Button15m,  "15m"},
        {&ui_Button12m,  "12m"},  {&ui_Button10m,  "10m"},
        {&ui_Button6m,   "6m"},
    };
    for (auto &m : map) {
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
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_CLICKABLE);

    if (status.alarm) {
        state.alarm  = false;  status.alarm  = false;
        status.ptt   = false;  state.ptt     = false;
        state.state  = false;  status.state  = false;
        sendStateData();
    }

    lv_label_set_text(ui_alertReason, "Delaying...");

    lv_timer_t *t = lv_timer_create([](lv_timer_t *timer) {
        lv_timer_del(timer);
        lv_scr_load(ui_main);
    }, 1000, NULL);
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

#if DEBUG
    Serial.println("[DEBUG] Settings saved from UI to variables:");
#endif
    sendStateData();
    sendSettingsData();
}

void toggleAutoBand(lv_event_t * /*e*/) {
    settings.autoband = lv_obj_get_state(ui_autoSelectSwitch) & LV_STATE_CHECKED;
    sendSettingsData();
}

// ─── Control event handlers ───────────────────────────────────────────────────

void set_band(lv_event_t *e) {
    lv_obj_t *target = lv_event_get_target_obj(e);
    memset(state.band, 0, sizeof(state.band));

    struct { lv_obj_t **btn; const char *band; } map[] = {
        {&ui_Button160m, "160m"}, {&ui_Button80m,  "80m"},
        {&ui_Button60m,  "60m"},  {&ui_Button40m,  "40m"},
        {&ui_Button30m,  "30m"},  {&ui_Button20m,  "20m"},
        {&ui_Button17m,  "17m"},  {&ui_Button15m,  "15m"},
        {&ui_Button12m,  "12m"},  {&ui_Button10m,  "10m"},
        {&ui_Button6m,   "6m"},
    };
    for (auto &m : map) {
        if (target == *m.btn) {
            strncpy(state.band, m.band, sizeof(state.band) - 1);
            Serial.print("Band set: "); Serial.println(state.band);
            sendStateData();
            return;
        }
    }
    Serial.println("Unknown band button!");
}

void enableAmp(lv_event_t *e) {
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
    state.state  = lv_obj_has_state(sw, LV_STATE_CHECKED);
    Serial.println(state.state ? "Amplifier: ON" : "Amplifier: OFF");
    sendStateData();
}

void togglePTT(lv_event_t *e) {
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);

    if (state.ptt) {
        state.ptt = false;
        lv_obj_set_style_bg_color(sw, lv_color_hex(0x2196F3), LV_PART_INDICATOR);
        Serial.println("PTT: OFF");
    } else if (state.state) {
        state.ptt = true;
        lv_obj_set_style_bg_color(sw, lv_color_hex(0xFF0000), LV_PART_INDICATOR);
        Serial.println("PTT: ON");
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
        Serial.println("WiFi settings saved via LVGL button!");
    }
}

void setupLVGLButtonHandler() {
    lv_obj_add_event_cb(ui_Button10, saveWiFiSettingsEventHandler, LV_EVENT_CLICKED, NULL);
}

// charts

void graphOpened(lv_event_t * /*e*/) {
    static int32_t pwr_buf[CHART_POINTS];
    static int32_t tmp_buf[CHART_POINTS];

    for (int i = 0; i < CHART_POINTS; i++) {
        pwr_buf[i] = LV_CHART_POINT_NONE;
        tmp_buf[i] = LV_CHART_POINT_NONE;
    }

    uint8_t count = (history.count > CHART_POINTS) ? CHART_POINTS : history.count;
    uint8_t start = (count < CHART_POINTS) ? 0 : history.head;

    for (uint8_t i = 0; i < count; i++) {
        uint8_t idx = (start + i) % CHART_POINTS;
        pwr_buf[i] = history.power[idx];
        tmp_buf[i] = history.plate_temp[idx];
    }

    lv_chart_series_t *ser_pwr  = lv_chart_get_series_next(ui_powerChart, NULL);
    lv_chart_series_t *ser_temp = lv_chart_get_series_next(ui_tempChart,  NULL);

    if (ser_pwr)  lv_chart_set_ext_y_array(ui_powerChart, ser_pwr,  pwr_buf);
    if (ser_temp) lv_chart_set_ext_y_array(ui_tempChart,  ser_temp, tmp_buf);

    lv_chart_refresh(ui_powerChart);
    lv_chart_refresh(ui_tempChart);
}