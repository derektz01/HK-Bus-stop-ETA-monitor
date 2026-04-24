#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

struct Config
{
  String wifi_ssid;
  String wifi_pass;
  String ap_ssid;
  String ap_pass;
  std::vector<String> kmb_stop_ids;
  std::vector<String> ctb_stop_ids;
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