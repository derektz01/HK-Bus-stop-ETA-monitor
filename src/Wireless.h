#pragma once
#include "WiFi.h"

extern bool WIFI_Connection;
extern uint8_t WIFI_NUM;
extern uint8_t BLE_NUM;
extern bool Scan_finish;

void WiFi_Connect();                    // Connect using ConfigManager
bool WiFi_IsConnected();                // Check before API calls
String GetWiFiInfoString();             // For tWifiInfo (AP or Station mode)
void StartAPMode();                     // Enter AP mode when WiFi fails