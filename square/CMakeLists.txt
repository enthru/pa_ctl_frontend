SET(SOURCES ui_main.c
    ui_menu.c
    ui_protection.c
    ui_wifi.c
    ui_mainLeft.c
    ui_bands.c
    ui_mainRight.c
    ui_warning.c
    ui_protection2.c
    ui_calibration.c
    ui.c
    ui_comp_hook.c
    ui_helpers.c)

add_library(ui ${SOURCES})
