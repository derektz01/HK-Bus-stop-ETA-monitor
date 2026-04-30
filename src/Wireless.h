#pragma once
#include "WiFi.h"

extern bool WIFI_Connection;
extern uint8_t WIFI_NUM;
extern uint8_t BLE_NUM;
extern bool Scan_finish;

void WiFi_Connect();                    // Connect using ConfigManager
bool WiFi_IsConnected();                // Check before API calls
String GetWiFiInfoString();             // Single-string form, kept for compat
void StartAPMode();                     // Enter AP mode when WiFi fails

// Cycle-friendly form: each frame is short enough to fit in a static label.
// STA mode = 2 frames (SSID, setup URL). AP mode = 3 frames (SSID, password,
// setup URL). Disconnected = 1 frame.
int  GetWiFiInfoFrameCount();
void GetWiFiInfoFrame(int frameIndex, char *out, size_t outSize);