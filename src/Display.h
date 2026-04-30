#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void Display_Init(void);

void ShowWifiInfo(void);
void HideWifiInfo(void);
void Cycle_Wifi_Info(void);   // Advance the wifi-info banner to the next frame

void Update_Time(void);
void Update_Date_And_Weekday(void);
void Update_Bus_List(void);
void Update_Weather(void);
void Update_Holiday_Display(void);
void Update_Background(void);

void OnNextPagePressed(void);

// Power down the LCD backlight (CH422G expander pin). Used by the sleep
// module before entering deep sleep so the panel goes dark immediately.
void Display_BacklightOff(void);

#ifdef __cplusplus
}
#endif
