#include "JSONParser.h"
#include <string.h>
#include <stdlib.h>
#include <Arduino.h>
#include "log.h"

extern StatusData status;
extern SettingsData settings;
extern StateData state;
extern CalibrationData calibration;

// ─── Single-pass object tokenizer ──────────────────────────────────────────────
// Walks one flat JSON object in place, inserting NUL terminators, and invokes
// handler(key, value) for every "key":value pair. Surrounding quotes are
// stripped from string values. This replaces the previous approach of copying
// the object and then running strstr()+snprintf() once per field (O(fields ×
// length), plus a searchKey format per lookup) — here each object is scanned a
// single time. `buf` is mutated; pass a scratch copy, not the receive buffer.

typedef void (*KVHandler)(const char* key, const char* val);

static void forEachPair(char* buf, KVHandler handler) {
    char* p = strchr(buf, '{');
    if (!p) return;
    p++;

    while (*p) {
        while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r' || *p == '\t') p++;
        if (*p != '"') break;                 // '}' or malformed → done

        char* key = ++p;
        while (*p && *p != '"') p++;
        if (*p != '"') break;
        *p++ = '\0';                          // terminate key

        while (*p == ' ') p++;
        if (*p != ':') break;
        p++;
        while (*p == ' ') p++;

        char* val;
        if (*p == '"') {                      // string value
            val = ++p;
            while (*p && *p != '"') p++;
            if (*p != '"') break;
            *p++ = '\0';                      // terminate value
        } else {                              // number / bool
            val = p;
            while (*p && *p != ',' && *p != '}' &&
                   *p != ' ' && *p != '\n' && *p != '\r') p++;
            if (*p == '}') {                  // last field: keep '}' out of val
                *p = '\0';
                handler(key, val);
                break;
            }
            if (*p) *p++ = '\0';
        }
        handler(key, val);
    }
}

// Copy the object delimited by the first {...} after `objKey` into `out`.
// Returns 1 on success, 0 if the key/braces are missing or too large.
static int extractObject(char* jsonString, const char* objKey, char* out, size_t outSize) {
    if (!jsonString) return 0;

    char* keyStart = strstr(jsonString, objKey);
    if (!keyStart) return 0;

    char* braceStart = strchr(keyStart, '{');
    if (!braceStart) return 0;
    char* braceEnd = strchr(braceStart, '}');
    if (!braceEnd) return 0;

    int objLen = braceEnd - braceStart + 1;
    if (objLen <= 0 || (size_t)objLen > outSize - 1) return 0;

    strncpy(out, braceStart, objLen);
    out[objLen] = '\0';
    return 1;
}

// ─── Status ─────────────────────────────────────────────────────────────────────

static void statusPair(const char* k, const char* v) {
    if      (!strcmp(k, "fwd"))                status.fwd        = atof(v);
    else if (!strcmp(k, "ref"))                status.ref        = atof(v);
    else if (!strcmp(k, "trxfwd"))             status.trxfwd     = atof(v);
    else if (!strcmp(k, "swr"))                status.swr        = atof(v);
    else if (!strcmp(k, "current"))            status.current    = atof(v);
    else if (!strcmp(k, "voltage"))            status.voltage    = atof(v);
    else if (!strcmp(k, "water_temp"))         status.water_temp = atof(v);
    else if (!strcmp(k, "plate_temp"))         status.plate_temp = atof(v);
    else if (!strcmp(k, "coeff"))              status.coeff      = atof(v);
    else if (!strcmp(k, "rsrv"))               status.rsrv       = atof(v);
    else if (!strcmp(k, "alarm"))              status.alarm      = !strcmp(v, "true");
    else if (!strcmp(k, "state"))              status.state      = !strcmp(v, "true");
    else if (!strcmp(k, "ptt"))                status.ptt        = !strcmp(v, "true");
    else if (!strcmp(k, "pwm_pump"))           status.pwm_pump   = atoi(v);
    else if (!strcmp(k, "pwm_cooler"))         status.pwm_cooler = atoi(v);
    else if (!strcmp(k, "auto_pwm_pump"))      status.auto_pwm_pump      = !strcmp(v, "true");
    else if (!strcmp(k, "auto_pwm_fan"))       status.auto_pwm_fan       = !strcmp(v, "true");
    else if (!strcmp(k, "protection_enabled")) status.protection_enabled = !strcmp(v, "true");
    else if (!strcmp(k, "debug"))              status.debug              = !strcmp(v, "true");
    else if (!strcmp(k, "alert_reason")) {
        strncpy(status.alert_reason, v, sizeof(status.alert_reason) - 1);
        status.alert_reason[sizeof(status.alert_reason) - 1] = '\0';
    }
    else if (!strcmp(k, "band")) {
        strncpy(status.band, v, sizeof(status.band) - 1);
        status.band[sizeof(status.band) - 1] = '\0';
    }
}

int parseStatusJson(char* jsonString) {
    char statusObj[512];
    if (!extractObject(jsonString, "\"status\":", statusObj, sizeof(statusObj))) {
        return 0;
    }

    LOG_TRACE("Parsing status object: %s", statusObj);
    forEachPair(statusObj, statusPair);
    return 1;
}

// ─── Calibration ─────────────────────────────────────────────────────────────────

static void calibrationPair(const char* k, const char* v) {
    if      (!strcmp(k, "low_fwd_coeff"))   calibration.low_fwd_coeff   = atof(v);
    else if (!strcmp(k, "low_rev_coeff"))   calibration.low_rev_coeff   = atof(v);
    else if (!strcmp(k, "low_ifwd_coeff"))  calibration.low_ifwd_coeff  = atof(v);
    else if (!strcmp(k, "mid_fwd_coeff"))   calibration.mid_fwd_coeff   = atof(v);
    else if (!strcmp(k, "mid_rev_coeff"))   calibration.mid_rev_coeff   = atof(v);
    else if (!strcmp(k, "mid_ifwd_coeff"))  calibration.mid_ifwd_coeff  = atof(v);
    else if (!strcmp(k, "high_fwd_coeff"))  calibration.high_fwd_coeff  = atof(v);
    else if (!strcmp(k, "high_rev_coeff"))  calibration.high_rev_coeff  = atof(v);
    else if (!strcmp(k, "high_ifwd_coeff")) calibration.high_ifwd_coeff = atof(v);
    else if (!strcmp(k, "voltage_coeff"))   calibration.voltage_coeff   = atof(v);
    else if (!strcmp(k, "current_coeff"))   calibration.current_coeff   = atof(v);
    else if (!strcmp(k, "rsrv_coeff"))      calibration.rsrv_coeff      = atof(v);
    else if (!strcmp(k, "acs_zero"))        calibration.acs_zero        = atof(v);
    else if (!strcmp(k, "acs_sens"))        calibration.acs_sens        = atof(v);
}

int parseCalibrationJson(char* jsonString) {
    char calibrationObj[512];
    if (!extractObject(jsonString, "\"calibration\":", calibrationObj, sizeof(calibrationObj))) {
        return 0;
    }

    LOG_TRACE("Parsing calibration object: %s", calibrationObj);
    forEachPair(calibrationObj, calibrationPair);
    return 1;
}

// ─── Settings ─────────────────────────────────────────────────────────────────────

static void settingsPair(const char* k, const char* v) {
    if      (!strcmp(k, "max_swr"))             settings.max_swr             = atoi(v);
    else if (!strcmp(k, "max_current"))         settings.max_current         = atoi(v);
    else if (!strcmp(k, "max_voltage"))         settings.max_voltage         = atoi(v);
    else if (!strcmp(k, "max_water_temp"))      settings.max_water_temp      = atoi(v);
    else if (!strcmp(k, "max_plate_temp"))      settings.max_plate_temp      = atoi(v);
    else if (!strcmp(k, "max_pump_speed_temp")) settings.max_pump_speed_temp = atoi(v);
    else if (!strcmp(k, "min_pump_speed_temp")) settings.min_pump_speed_temp = atoi(v);
    else if (!strcmp(k, "max_fan_speed_temp"))  settings.max_fan_speed_temp  = atoi(v);
    else if (!strcmp(k, "min_fan_speed_temp"))  settings.min_fan_speed_temp  = atoi(v);
    else if (!strcmp(k, "max_input_power"))     settings.max_input_power     = atof(v);
    else if (!strcmp(k, "min_coeff"))           settings.min_coeff           = atof(v);
    else if (!strcmp(k, "autoband"))            settings.autoband            = !strcmp(v, "true");
    else if (!strcmp(k, "default_band")) {
        strncpy(settings.default_band, v, sizeof(settings.default_band) - 1);
        settings.default_band[sizeof(settings.default_band) - 1] = '\0';
    }
}

int parseSettingsJson(char* jsonString) {
    char settingsObj[512];
    if (!extractObject(jsonString, "\"settings\":", settingsObj, sizeof(settingsObj))) {
        return 0;
    }

    LOG_TRACE("Parsing settings object: %s", settingsObj);
    forEachPair(settingsObj, settingsPair);
    return 1;
}
