#pragma once
#include "globals.h"

void loadWiFiCredentials();
void saveWiFiCredentials(const char* newSSID, const char* newPassword);
void connectToWiFi();
void printWiFiStatus();
void updateLVGLTextAreasWithSavedCredentials();
