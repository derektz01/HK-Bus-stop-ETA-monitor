#pragma once
#include <Arduino.h>
#include "BusData.h"
#include "HolidayData.h"

// Core Nextion functions
void Nextion_Init(uint8_t txPin, uint8_t rxPin);
void Nextion_Send(const char *cmd);

void ShowWifiInfo();
void HideWifiInfo();

// Display update functions
void Update_Full_Display();
void Update_Time();
void Update_Date_And_Weekday();
void Update_Bus_List();
void Update_Weather_On_Nextion();
void Update_Holiday_Display();
void Update_Background();

// Touch event handler
void Touch_To_Switch_Page();

// Dynamic bus data
extern uint8_t currentPage;
extern std::vector<BusInfo> displayRoutes;