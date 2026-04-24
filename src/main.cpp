#include <Arduino.h>
#include "Nextion.h"
#include "BusData.h"
#include "WiFi.h"
#include "WeatherData.h"
#include "HolidayData.h"
#include "ConfigManager.h"
#include "Wireless.h"
#include "WebPortal.h"

void printLocalTime()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S"); // Formatted print
}

void setup()
{
  Serial.begin(115200);
  delay(800);

  // Initialize Nextion (TX=16, RX=17)
  Nextion_Init(16, 17);

  // Load configuration from LittleFS
  ConfigMgr.load();

  // === Centralized WiFi handling ===
  WiFi_Connect();

  // Start configuration web server (works in both AP and STA mode)
  WebPortal_Begin();

  // Sync time if connected
  if (WiFi_IsConnected())
  {
    // Sync time with NTP (Hong Kong timezone UTC+8)
    configTime(8 * 3600, 0, "pool.ntp.org", "time.google.com");
    printf("NTP Time Sync started (UTC+8) Time\n");
    printLocalTime();

    // Fetch weather immediately on startup
    Weather_FetchOpenMeteo();
  }

  // Initialize bus data
  BusData_Init();

  // Initialize holiday data
  Holiday_Init();
  Update_Holiday_Display();

  // Show initial full display
  Update_Full_Display();
  Update_Date_And_Weekday();
  Update_Background();
  ShowWifiInfo();

  if (WiFi_IsConnected())
  {
    const Config &cfg = ConfigMgr.getConfig();
    AutoRefreshBusETA(cfg.kmb_stop_ids, cfg.ctb_stop_ids);
  }

  printf("HK Bus Stop ETA Board Ready!\r\n");
}

void loop()
{
  static unsigned long lastTimeUpdate = 0;
  static unsigned long lastWeatherUpdate = 0;
  static unsigned long lastBusRefresh = 0;
  static unsigned long lastSlideshow = 0;
  static unsigned long lastHolidayUpdate = 0;
  static unsigned long lastBackgroundCheck = 0;
  static unsigned long bootTime = millis();
  static bool wifiInfoHidden = false;

  // Service pending HTTP clients
  WebPortal_Loop();

  if (!wifiInfoHidden && (millis() - bootTime > 120000))
  { // 120 seconds
    HideWifiInfo();
    wifiInfoHidden = true;
    printf("WiFi info hidden after 120 seconds\n");
  }

  // Update time every second
  if (millis() - lastTimeUpdate > 1000)
  {
    lastTimeUpdate = millis();
    Update_Time();
  }

  // Update holiday info every hour
  if (millis() - lastHolidayUpdate > 3600000)
  { // 1 hours
    lastHolidayUpdate = millis();
    Update_Holiday_Display();
  }

  // Update background picture every minute (only sends on change)
  if (millis() - lastBackgroundCheck > 60000)
  {
    lastBackgroundCheck = millis();
    Update_Background();
  }

  // Fetch weather every 30 minutes (1,800,000 ms)
  if (millis() - lastWeatherUpdate > 1800000)
  {
    lastWeatherUpdate = millis();
    Weather_FetchOpenMeteo();
  }

  // Auto refresh bus ETA every 45 seconds
  if (millis() - lastBusRefresh > 45000)
  {
    lastBusRefresh = millis();
    if (WiFi_IsConnected())
    {
      const Config &cfg = ConfigMgr.getConfig();
      AutoRefreshBusETA(cfg.kmb_stop_ids, cfg.ctb_stop_ids);
    }
  }

  // Auto slideshow every 5 seconds
  if (millis() - lastSlideshow > 5000)
  {
    lastSlideshow = millis();
    Switch_To_Next_Page();
    Update_Bus_List();
  }

  // Check for touch input to switch page
  Touch_To_Switch_Page();

  delay(10);
}