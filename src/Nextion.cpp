#include "Nextion.h"
#include "BusData.h"
#include "WeatherData.h"
#include "HolidayData.h"
#include <ArduinoJson.h>
#include "Wireless.h"

// ================================================================
// Send weather data to Nextion display
// ================================================================
void Update_Weather_On_Nextion()
{
  char cmd[64];

  // Update Temperature (tTemp)
  sprintf(cmd, "tTemp.txt=\"%.1f\"", current_temperature);
  Nextion_Send(cmd);

  // Update Weather Description (add tWeather component in Nextion Editor)
  sprintf(cmd, "tWeather.txt=\"%s\"", current_weather_desc);
  Nextion_Send(cmd);
}

// ================================================================
// Holiday display logic (tHoliday + tHolidayDate)
// ================================================================
void Update_Holiday_Display()
{
  // If holidayDoc is empty, show fallback
  if (holidayDoc["holidays"].isNull())
  {
    Nextion_Send("tHoliday.txt=\"公眾假期資料載入失敗\"");
    Nextion_Send("tHolidayDate.txt=\"-\"");
    printf("Holiday data not loaded - using fallback\n");
    return;
  }

  time_t now = time(nullptr);
  struct tm today;
  localtime_r(&now, &today);
  time_t todaySeconds = mktime(&today);

  String nextHolidayName = "";
  String nextHolidayDate = "";
  int minDays = 999;

  JsonArray holidays = holidayDoc["holidays"].as<JsonArray>();

  for (JsonObject h : holidays)
  {
    const char *dateStr = h["date"] | "";
    const char *name = h["name_tc"] | "";

    struct tm holidayTm = {};
    sscanf(dateStr, "%4d-%2d-%2d", &holidayTm.tm_year, &holidayTm.tm_mon, &holidayTm.tm_mday);
    holidayTm.tm_year -= 1900;
    holidayTm.tm_mon -= 1;
    time_t holidaySeconds = mktime(&holidayTm);

    int daysLeft = (int)difftime(holidaySeconds, todaySeconds) / (60 * 60 * 24);

    if (daysLeft >= 0 && daysLeft < minDays)
    {
      minDays = daysLeft;
      nextHolidayName = name;
      nextHolidayDate = dateStr;
    }
  }

  char cmd[128];

  if (minDays == 0)
  {
    sprintf(cmd, "tHoliday.txt=\"%s\"", nextHolidayName.c_str());
  }
  else
  {
    sprintf(cmd, "tHoliday.txt=\"%s還有%d天\"", nextHolidayName.c_str(), minDays);
  }
  Nextion_Send(cmd);

  // tDate example: "2026年4月4日(六)"
  char dateDisplay[32];
  struct tm nextDate;
  sscanf(nextHolidayDate.c_str(), "%4d-%2d-%2d", &nextDate.tm_year, &nextDate.tm_mon, &nextDate.tm_mday);
  nextDate.tm_year -= 1900;
  nextDate.tm_mon -= 1;
  mktime(&nextDate);

  const char *weekdays[] = {"日", "一", "二", "三", "四", "五", "六"};
  sprintf(dateDisplay, "%04d年%02d月%02d日(%s)",
          nextDate.tm_year + 1900,
          nextDate.tm_mon + 1,
          nextDate.tm_mday,
          weekdays[nextDate.tm_wday]);

  sprintf(cmd, "tHolidayDate.txt=\"%s\"", dateDisplay);
  Nextion_Send(cmd);

  printf("Holiday updated → %s 還有 %d 天\n", nextHolidayName.c_str(), minDays);
}

// ================================================================
// Core Nextion Functions
// ================================================================

HardwareSerial &nexSerial = Serial1;

void Nextion_Init(uint8_t txPin, uint8_t rxPin)
{
  nexSerial.begin(9600, SERIAL_8N1, rxPin, txPin);
  delay(200);
  printf("Nextion initialized (TX=%d, RX=%d)\r\n", txPin, rxPin);
}

void Nextion_Send(const char *cmd)
{
  nexSerial.print(cmd);
  nexSerial.write(0xFF);
  nexSerial.write(0xFF);
  nexSerial.write(0xFF);
  delay(25);
}

void Update_Time()
{
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  char timeStr[6];
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);

  char cmd[32];
  sprintf(cmd, "tTime.txt=\"%s\"", timeStr);
  Nextion_Send(cmd);
}

void Update_Date_And_Weekday()
{
  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  const char *weekdays[] = {"日", "一", "二", "三", "四", "五", "六"};
  const char *weekday = weekdays[timeinfo.tm_wday];

  char dateStr[32];
  snprintf(dateStr, sizeof(dateStr), "%04d年%02d月%02d日(%s)",
           timeinfo.tm_year + 1900,
           timeinfo.tm_mon + 1,
           timeinfo.tm_mday,
           weekday);

  char cmd[64];
  sprintf(cmd, "tNow.txt=\"%s\"", dateStr);
  Nextion_Send(cmd);
}

void Update_Bus_List()
{
  int start = currentPage * 4;
  int end = start + 4;
  int total = displayRoutes.size();

  // Clear all 8 ETA slots first
  for (int i = 1; i <= 4; i++)
  {
    char cmd[32];
    sprintf(cmd, "tRoute%d.txt=\"\"", i);
    Nextion_Send(cmd);
    sprintf(cmd, "tETA%d1.txt=\"\"", i);
    Nextion_Send(cmd);
    sprintf(cmd, "tETA%d2.txt=\"\"", i);
    Nextion_Send(cmd);
    sprintf(cmd, "tDest%d.txt=\"\"", i);
    Nextion_Send(cmd);
  }

  // Fill the slots with available data
  for (int i = start; i < end && i < total; i++)
  {
    int slot = (i % 4) + 1; // slot 1~4

    char cmd[80];

    // Route name (still one per slot)
    sprintf(cmd, "tRoute%d.txt=\"%s\"", slot, displayRoutes[i].route);
    Nextion_Send(cmd);

    // ETA seq=1 → tETA11, tETA21, tETA31, tETA41
    sprintf(cmd, "tETA%d1.txt=\"%s\"", slot, displayRoutes[i].etaDisplay1);
    Nextion_Send(cmd);

    // ETA seq=2 → tETA12, tETA22, tETA32, tETA42 (currently empty, you can fill later)
    sprintf(cmd, "tETA%d2.txt=\"%s\"", slot, displayRoutes[i].etaDisplay2);
    Nextion_Send(cmd);

    // Destination
    sprintf(cmd, "tDest%d.txt=\"%s\"", slot, displayRoutes[i].destination);
    Nextion_Send(cmd);

    printf("Displayed Route %d | %s | %s | %s| %s\n",
           slot, displayRoutes[i].route, displayRoutes[i].etaDisplay1, displayRoutes[i].etaDisplay2, displayRoutes[i].destination);
  }

  printf("Bus list updated (Page %d, Total: %d routes)\n", currentPage, total);
}

void Update_Full_Display()
{
  Update_Time();
  Update_Date_And_Weekday();
  Update_Weather_On_Nextion();
  Update_Holiday_Display();
  Update_Background();

  Update_Bus_List();
}

// ================================================================
// Time-based background picture
//   06:00 - 16:59 -> picBG.pic=0  (day)
//   17:00 - 18:29 -> picBG.pic=1  (evening)
//   18:30 - 05:59 -> picBG.pic=2  (night)
// Call this every minute; it only sends the command when the value changes.
// ================================================================
void Update_Background()
{
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);

  int minutes = t.tm_hour * 60 + t.tm_min;
  int pic;
  if (minutes >= 6 * 60 && minutes <= 16 * 60 + 59)
    pic = 0;
  else if (minutes >= 17 * 60 && minutes <= 18 * 60 + 29)
    pic = 1;
  else
    pic = 2;

  static int lastPic = -1;
  if (pic == lastPic)
    return;
  lastPic = pic;

  char cmd[24];
  snprintf(cmd, sizeof(cmd), "picBG.pic=%d", pic);
  Nextion_Send(cmd);
  printf("Background switched to pic=%d\n", pic);
}

// ==================== HOTSPOT TOUCH HANDLER ====================
void Touch_To_Switch_Page()
{
  static uint8_t buffer[10];
  static uint8_t index = 0;

  while (nexSerial.available())
  {
    uint8_t c = nexSerial.read();
    buffer[index] = c;
    index++;

    if (index >= 7)
    {
      if (buffer[0] == 0x65 && buffer[6] == 0xFF)
      {
        uint8_t componentID = buffer[2];
        uint8_t eventType = buffer[3];

        if (componentID == 20 && eventType == 0x01)
        {
          printf("Hotspot Pressed! Switching page...\r\n");
          delay(150);
          Switch_To_Next_Page();
          Update_Bus_List();
        }
        index = 0;
      }
      else if (index >= 10)
      {
        index = 0;
      }
    }
  }
}

// ================================================================
// WiFi Information Display (gWifiInfo - scrolling text, 90 chars)
// ================================================================
void ShowWifiInfo()
{
    String info = GetWiFiInfoString();

    char cmd[128];
    sprintf(cmd, "gWifiInfo.txt=\"%s\"", info.c_str());
    Nextion_Send(cmd);
    Nextion_Send("gWifiInfo.aph=100"); 
}

void HideWifiInfo()
{
    Nextion_Send("gWifiInfo.txt=\"\"");   // Clear scrolling text
    Nextion_Send("gWifiInfo.aph=0"); 
}