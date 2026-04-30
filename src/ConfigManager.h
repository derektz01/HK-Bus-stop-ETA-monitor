#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

struct SleepScheduleTask
{
  uint8_t hour;       // 0-23
  uint8_t minute;     // 0-59
  uint8_t days_mask;  // bit 0 = Sun, bit 1 = Mon, ..., bit 6 = Sat
};

struct Config
{
  String wifi_ssid;
  String wifi_pass;
  String ap_ssid;
  String ap_pass;
  std::vector<String> kmb_stop_ids;
  std::vector<String> ctb_stop_ids;
  std::vector<SleepScheduleTask> wake_tasks;
  std::vector<SleepScheduleTask> sleep_tasks;
  uint8_t grace_period_minutes = 5;  // wake-window grace before re-sleep
};

class ConfigManager
{
public:
  bool load();
  bool save();
  Config getConfig() { return config; }
  void setConfig(const Config &newConfig) { config = newConfig; }

private:
  Config config;
  bool loadDefault();
};

extern ConfigManager ConfigMgr;