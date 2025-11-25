#ifndef JSONPARSER_H
#define JSONPARSER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float fwd;
    float ref;
    float trxfwd;
    float swr;
    float current;
    float voltage;
    float water_temp;
    float plate_temp;
    bool alarm;
    char alert_reason[20];
    bool state;
    bool ptt;
    char band[10];
    int pwm_pump;
    int pwm_cooler;
    bool auto_pwm_pump;
    bool auto_pwm_fan;
    bool protection_enabled;
} StatusData;

typedef struct {
    float max_swr;
    float max_current;
    float max_voltage;
    float max_water_temp;
    float max_plate_temp;
    float max_pump_speed_temp;
    float min_pump_speed_temp;
    float max_fan_speed_temp;
    float min_fan_speed_temp;
    int   max_input_power;
    bool autoband;
    char default_band[10];
} SettingsData;

typedef struct {
    bool ptt;
    int pwm_pump;
    int pwm_cooler;
    char band[10];
    bool auto_pwm_pump;
    bool auto_pwm_fan;
    bool state;
    bool alarm;
} StateData;

char* getJsonValue(char* json, const char* key);
int parseStatusJson(char* jsonString);
int parseSettingsJson(char* jsonString);

#ifdef __cplusplus
}
#endif

#endif