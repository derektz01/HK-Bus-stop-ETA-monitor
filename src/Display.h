#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void Display_Init(void);

void ShowWifiInfo(void);
void HideWifiInfo(void);

void Update_Time(void);
void Update_Date_And_Weekday(void);
void Update_Bus_List(void);
void Update_Weather(void);
void Update_Holiday_Display(void);
void Update_Background(void);

void OnNextPagePressed(void);

#ifdef __cplusplus
}
#endif
