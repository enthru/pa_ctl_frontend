#pragma once
#include "globals.h"

// ─── Display init (owns gfx/bus — PINS_JC4827W543.h included only here) ──────
bool initDisplay();

// ─── Screen name helper ───────────────────────────────────────────────────────
const char* getCurrentScreenName();

// ─── LVGL display callbacks ───────────────────────────────────────────────────
void my_print(lv_log_level_t level, const char *buf);
uint32_t millis_cb(void);
void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
void my_touchpad_read(lv_indev_t *indev, lv_indev_data_t *data);

// ─── Utility ─────────────────────────────────────────────────────────────────
void set_switch_state(lv_obj_t *sw, bool state);
void dropdown_set_by_text(lv_obj_t *dropdown, const char *text);
String getUptime();

// ─── Screen event handlers ────────────────────────────────────────────────────
void mainRightLoaded(lv_event_t *e);
void protectionOpened(lv_event_t *e);
void bandOpened(lv_event_t *e);
void resetAlert(lv_event_t *e);

// ─── Settings UI handlers ─────────────────────────────────────────────────────
void settingsNext(lv_event_t *e);
void saveSettings(lv_event_t *e);
void toggleAutoBand(lv_event_t *e);

// ─── Control event handlers ───────────────────────────────────────────────────
void set_band(lv_event_t *e);
void enableAmp(lv_event_t *e);
void togglePTT(lv_event_t *e);

// ─── WiFi UI ──────────────────────────────────────────────────────────────────
void setupLVGLButtonHandler();

// chart
void graphOpened(lv_event_t *e);

