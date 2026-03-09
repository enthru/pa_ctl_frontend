#pragma once
#include "globals.h"

// Register all routes and start the server
void registerWebRoutes();

// Individual handlers (public for potential reuse)
void handleRoot();
void handleStatus();
void handleBandPage();
void handleSettingsPage();
void handleGetSettings();
void handleSaveSettings();
void handleCalibrationPage();
void handleGetCalibration();
void handleSaveCalibration();
void handleSetBand();
void handleSetState();
void handleEvents();
void handleResetAlert();
void handleResetError();
void handleCSS();
