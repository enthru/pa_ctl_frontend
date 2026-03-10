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
    float coeff;
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
    int max_swr;
    int max_current;
    int max_voltage;
    int max_water_temp;
    int max_plate_temp;
    int max_pump_speed_temp;
    int min_pump_speed_temp;
    int max_fan_speed_temp;
    int min_fan_speed_temp;
    int max_input_power;
    int min_coeff;
    bool autoband;
    char default_band[10];
} SettingsData;

typedef struct {
    float low_fwd_coeff;
    float low_rev_coeff;
    float low_ifwd_coeff;
    float mid_fwd_coeff;
    float mid_rev_coeff;
    float mid_ifwd_coeff;
    float high_fwd_coeff;
    float high_rev_coeff;
    float high_ifwd_coeff;
    float voltage_coeff;
    float current_coeff;
    float rsrv_coeff;
    float acs_zero;
    float acs_sens;
} CalibrationData;

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
int parseCalibrationJson(char* jsonString);

#ifdef __cplusplus
}
#endif

#endif