#include "ConfigManager.h"
#include <LittleFS.h>

ConfigManager ConfigMgr;

static void readScheduleTaskList(JsonVariantConst node,
                                 std::vector<SleepScheduleTask> &out)
{
  out.clear();
  if (!node.is<JsonArrayConst>()) return;
  for (JsonVariantConst v : node.as<JsonArrayConst>())
  {
    SleepScheduleTask t{};
    int h = v["hour"] | -1;
    int m = v["minute"] | -1;
    if (h < 0 || h > 23 || m < 0 || m > 59) continue;
    t.hour = (uint8_t)h;
    t.minute = (uint8_t)m;
    if (v["days"].is<JsonArrayConst>())
    {
      for (JsonVariantConst d : v["days"].as<JsonArrayConst>())
      {
        int idx = d.as<int>();
        if (idx >= 0 && idx <= 6) t.days_mask |= (uint8_t)(1 << idx);
      }
    }
    if (t.days_mask == 0) continue;  // skip tasks with no active days
    out.push_back(t);
  }
}

static void writeScheduleTaskList(JsonArray arr,
                                  const std::vector<SleepScheduleTask> &tasks)
{
  for (const auto &t : tasks)
  {
    JsonObject o = arr.add<JsonObject>();
    o["hour"] = t.hour;
    o["minute"] = t.minute;
    JsonArray days = o["days"].to<JsonArray>();
    for (int i = 0; i < 7; i++)
      if (t.days_mask & (1 << i)) days.add(i);
  }
}

static void readStopList(JsonVariantConst node, std::vector<String> &out)
{
  out.clear();
  if (node.is<JsonArrayConst>())
  {
    for (JsonVariantConst v : node.as<JsonArrayConst>())
    {
      String s = v.as<String>();
      s.trim();
      if (s.length() > 0)
        out.push_back(s);
    }
  }
  else if (node.is<const char *>())
  {
    String s = node.as<String>();
    s.trim();
    if (s.length() > 0)
      out.push_back(s);
  }
}

bool ConfigManager::load()
{
  if (!LittleFS.begin(true))
  {
    printf("LittleFS Mount Failed\n");
    return loadDefault();
  }

  File file = LittleFS.open("/config.json", "r");
  if (!file)
  {
    printf("config.json not found, using defaults\n");
    return loadDefault();
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error)
  {
    printf("Failed to parse config.json\n");
    return loadDefault();
  }

  config.wifi_ssid = doc["wifi"]["ssid"] | "YOUR_HOME_WIFI_SSID";
  config.wifi_pass = doc["wifi"]["password"] | "";
  config.ap_ssid = doc["ap"]["ssid"] | "BusETA-Config";
  config.ap_pass = doc["ap"]["password"] | "12345678";

  readStopList(doc["stops"]["kmb"], config.kmb_stop_ids);
  readStopList(doc["stops"]["ctb"], config.ctb_stop_ids);

  readScheduleTaskList(doc["sleep_schedule"]["wake_tasks"], config.wake_tasks);
  readScheduleTaskList(doc["sleep_schedule"]["sleep_tasks"], config.sleep_tasks);
  {
    int g = doc["sleep_schedule"]["grace_period_minutes"] | 5;
    if (g < 0) g = 0;
    if (g > 60) g = 60;
    config.grace_period_minutes = (uint8_t)g;
  }

  printf("Config loaded: %d KMB stop(s), %d CTB stop(s), %d wake task(s), %d sleep task(s)\n",
         (int)config.kmb_stop_ids.size(), (int)config.ctb_stop_ids.size(),
         (int)config.wake_tasks.size(), (int)config.sleep_tasks.size());
  return true;
}

bool ConfigManager::loadDefault()
{
  config.wifi_ssid = "YOUR_HOME_WIFI_SSID";
  config.wifi_pass = "";
  config.ap_ssid = "BusETA-Config";
  config.ap_pass = "12345678";
  config.kmb_stop_ids = {"904DEAF7441E3BB8"};
  config.ctb_stop_ids.clear();
  return true;
}

bool ConfigManager::save()
{
  if (!LittleFS.begin(true))
    return false;

  JsonDocument doc;
  doc["wifi"]["ssid"] = config.wifi_ssid;
  doc["wifi"]["password"] = config.wifi_pass;
  doc["ap"]["ssid"] = config.ap_ssid;
  doc["ap"]["password"] = config.ap_pass;

  JsonArray kmb = doc["stops"]["kmb"].to<JsonArray>();
  for (const String &s : config.kmb_stop_ids)
    kmb.add(s);

  JsonArray ctb = doc["stops"]["ctb"].to<JsonArray>();
  for (const String &s : config.ctb_stop_ids)
    ctb.add(s);

  writeScheduleTaskList(doc["sleep_schedule"]["wake_tasks"].to<JsonArray>(),
                        config.wake_tasks);
  writeScheduleTaskList(doc["sleep_schedule"]["sleep_tasks"].to<JsonArray>(),
                        config.sleep_tasks);
  doc["sleep_schedule"]["grace_period_minutes"] = config.grace_period_minutes;

  File file = LittleFS.open("/config.json", "w");
  if (!file)
    return false;

  serializeJson(doc, file);
  file.close();
  printf("config.json saved\n");
  return true;
}
