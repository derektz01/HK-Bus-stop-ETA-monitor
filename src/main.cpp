#include <Arduino.h>
#include "Display.h"
#include "Diagnostics.h"
#include "BusData.h"
#include "WiFi.h"
#include "WeatherData.h"
#include "HolidayData.h"
#include "ConfigManager.h"
#include "Wireless.h"
#include "WebPortal.h"
#include "Sleep.h"

// Set true by network task while an API fetch is in progress; gates the loop
// task's UI work to free PSRAM bandwidth for the LCD EDMA. Single writer
// (network task), multiple readers — volatile is sufficient on ESP32-S3.
volatile bool g_apiFetchInProgress = false;

void printLocalTime()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    printf("Failed to obtain time");
    return;
  }
  printf("%A, %B %d %Y %H:%M:%S", &timeinfo); // Formatted print
}

// All HTTP/TLS work runs on core 0 (the WiFi core). Keeps the LCD-driving
// LVGL task on core 1 free of WiFi-DMA + JSON-parsing PSRAM contention,
// which previously caused horizontal drift during fetches.
static bool isWallClockValid()
{
  // 1577836800 = 2020-01-01 00:00:00 UTC. Anything earlier means SNTP hasn't
  // delivered a real timestamp yet (the system is still on the 1970 epoch).
  return time(nullptr) > 1577836800;
}

// Pinned to core 0 so HTTP-portal request handling (config save / reboot /
// static asset reads) doesn't preempt the LVGL render task on core 1 during
// a request. Polls handleClient() with a short delay to keep CPU low when
// idle. 8 KB stack covers the JSON parse + HTML response paths.
static void webPortalTask(void *)
{
  for (;;)
  {
    WebPortal_Loop();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

static void networkTask(void *)
{
  // Wait for WiFi to come up before the first fetch.
  while (!WiFi_IsConnected())
  {
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  // If setup() couldn't finish NTP in 15 s, keep waiting here. A fetch with
  // epoch time would produce wrong "minutes-until-bus" values everywhere.
  while (!isWallClockValid())
  {
    printf("networkTask: deferring first fetch — wall clock still pre-2020 (NTP not synced)\n");
    vTaskDelay(pdMS_TO_TICKS(2000));
  }

  // Wall clock is now valid. Re-paint everything that depends on time —
  // setup() may have run these while NTP was still pre-2020 (showing
  // 1970-01-01, "還有999天", wrong day/night background, etc.). Each call
  // is cheap thanks to change-detection; if the value is already correct
  // it returns before touching LVGL.
  Update_Time();
  vTaskDelay(pdMS_TO_TICKS(500));
  Update_Date_And_Weekday();
  vTaskDelay(pdMS_TO_TICKS(500));
  Update_Holiday_Display();
  vTaskDelay(pdMS_TO_TICKS(500));
  Update_Background();
  vTaskDelay(pdMS_TO_TICKS(500));
  Weather_FetchOpenMeteo();
  vTaskDelay(pdMS_TO_TICKS(500));
  {
    const Config &cfg = ConfigMgr.getConfig();
    AutoRefreshBusETA(cfg.kmb_stop_ids, cfg.ctb_stop_ids);
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  uint32_t lastBus     = millis();
  uint32_t lastWeather = millis();

  for (;;)
  {
    if (WiFi_IsConnected())
    {
      uint32_t now = millis();
      if (now - lastBus >= 45000)
      {
        const Config &cfg = ConfigMgr.getConfig();
        AutoRefreshBusETA(cfg.kmb_stop_ids, cfg.ctb_stop_ids);
        vTaskDelay(pdMS_TO_TICKS(1000));
        lastBus = millis();
      }
      if (now - lastWeather >= 1800000)
      {
        Weather_FetchOpenMeteo();
        vTaskDelay(pdMS_TO_TICKS(500));
        lastWeather = millis();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

void setup()
{
  Serial.begin(115200);
  delay(800);

  // Log why this boot happened (power-on vs. timer-wake vs. touch-wake).
  Sleep_Init();

  // Brings up the Waveshare ESP32-S3-Touch-LCD-4.3 board, LVGL, and the
  // SquareLine UI. Must run before any Update_* / ShowWifiInfo call.
  Display_Init();

  // Load configuration from LittleFS
  ConfigMgr.load();

  // === Centralized WiFi handling ===
  WiFi_Connect();
  Heap_Log("after WiFi_Connect");

  // Start configuration web server (works in both AP and STA mode)
  WebPortal_Begin();

  // Sync time if connected
  if (WiFi_IsConnected())
  {
    // Sync time with NTP (Hong Kong timezone UTC+8). configTime() only kicks
    // off the SNTP daemon — it doesn't wait for a response. Block here for up
    // to 15 s so subsequent fetches (which compute ETAs against `time(nullptr)`)
    // don't run with epoch (1970-01-01) and produce nonsense minutes.
    configTime(8 * 3600, 0, "pool.ntp.org", "time.google.com");
    printf("NTP Time Sync started (UTC+8); waiting up to 15s for first response...\n");

    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 15000))
    {
      printf("NTP synced: %04d-%02d-%02d %02d:%02d:%02d\n",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    }
    else
    {
      printf("WARNING: NTP not synced after 15s. networkTask will defer the first fetch until time is valid.\n");
    }
  }

  // Initialize bus data
  BusData_Init();

  // Initialize holiday data
  Holiday_Init();

  // Show initial full display
  Update_Time();
  Update_Date_And_Weekday();
  Update_Weather();
  Update_Holiday_Display();
  Update_Background();
  ShowWifiInfo();

  // Spin up the network task on core 0. Weather + bus fetches happen there
  // on their own cadence; the Arduino loop on core 1 only drives UI ticks.
  xTaskCreatePinnedToCore(networkTask, "network", 12 * 1024, nullptr, 1, nullptr, 0);

  // WebPortal HTTP polling on core 0 too — keeps it off core 1 alongside
  // the network task. Both are I/O-bound and yield often, so they coexist
  // without starving each other.
  xTaskCreatePinnedToCore(webPortalTask, "webportal", 8 * 1024, nullptr, 1, nullptr, 0);

  printf("HK Bus Stop ETA Board Ready!\r\n");
}

void loop()
{
  static unsigned long lastTimeUpdate = 0;
  static unsigned long lastSlideshow = 0;
  static unsigned long lastHolidayUpdate = 0;
  static unsigned long lastBackgroundCheck = 0;
  static unsigned long lastWifiInfoCycle = 0;
  static unsigned long bootTime = millis();
  static bool wifiInfoHidden = false;

  // Test-only: enter deep sleep at uptime+3min, wake after 6min (or touch).
  // Never returns once the threshold is hit; the device reboots on wake.
  Sleep_Tick();

  // WebPortal polling lives on core 0 (see webPortalTask) so the LVGL task
  // on core 1 never has to share with HTTP-portal request handling.

  // While the network task is mid-fetch, skip everything that would dirty an
  // LVGL widget (which forces a full-frame PSRAM write and contends with the
  // LCD EDMA scanout). Resumes immediately when the fetch finishes; pending
  // ticks (time, slideshow) catch up on the next loop iteration.
  if (g_apiFetchInProgress)
  {
    delay(200);
    return;
  }

  if (!wifiInfoHidden && (millis() - bootTime > 120000))
  { // 120 seconds
    HideWifiInfo();
    wifiInfoHidden = true;
  }

  // While the wifi-info banner is visible, rotate it through its frames every
  // 5 s so the user can read the SSID, password (AP mode), and the setup URL
  // without needing a scrolling animation.
  if (!wifiInfoHidden && (millis() - lastWifiInfoCycle > 5000))
  {
    lastWifiInfoCycle = millis();
    Cycle_Wifi_Info();
  }

  // Update time every second
  if (millis() - lastTimeUpdate > 1000)
  {
    lastTimeUpdate = millis();
    Update_Time();
    Update_Date_And_Weekday();
  }

  // Update holiday info every hour
  if (millis() - lastHolidayUpdate > 3600000)
  { // 1 hours
    lastHolidayUpdate = millis();
    Update_Holiday_Display();
  }

  // Update background picture every 15 minutes (only sends on change)
  if (millis() - lastBackgroundCheck > 90000)
  {
    lastBackgroundCheck = millis();
    Update_Background();
  }

  // Weather + bus ETA fetches now run on the core-0 networkTask (see setup),
  // not from this loop. Removed to keep WiFi/HTTP off the LCD-driving core.

  // Auto slideshow every 5 seconds
  if (millis() - lastSlideshow > 5000)
  {
    lastSlideshow = millis();
    Switch_To_Next_Page();
    Update_Bus_List();
  }

  // Touch is handled by ui_event_ctrTouch → NextPage() → OnNextPagePressed()
  delay(10);
}