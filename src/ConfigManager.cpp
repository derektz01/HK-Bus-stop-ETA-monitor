#include "ConfigManager.h"
#include <LittleFS.h>

ConfigManager ConfigMgr;

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

  printf("Config loaded: %d KMB stop(s), %d CTB stop(s)\n",
         (int)config.kmb_stop_ids.size(), (int)config.ctb_stop_ids.size());
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

  File file = LittleFS.open("/config.json", "w");
  if (!file)
    return false;

  serializeJson(doc, file);
  file.close();
  printf("config.json saved\n");
  return true;
}
