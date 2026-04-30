#include "WebPortal.h"
#include "ConfigManager.h"

#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

static WebServer server(80);

static void sendJson(int code, const JsonDocument &doc)
{
  String out;
  serializeJson(doc, out);
  server.send(code, "application/json", out);
}

static void writeStopArray(JsonObject parent, const char *key, const std::vector<String> &ids)
{
  JsonArray arr = parent[key].to<JsonArray>();
  for (const String &s : ids)
    arr.add(s);
}

static void readStopListFromJson(JsonVariantConst node, std::vector<String> &out)
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

static void writeScheduleArray(JsonArray arr,
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

static void readScheduleArray(JsonVariantConst node,
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
    if (t.days_mask == 0) continue;
    out.push_back(t);
  }
}

static void handleGetConfig()
{
  const Config &cfg = ConfigMgr.getConfig();

  JsonDocument doc;
  doc["wifi"]["ssid"] = cfg.wifi_ssid;
  doc["wifi"]["password"] = cfg.wifi_pass;
  doc["ap"]["ssid"] = cfg.ap_ssid;
  doc["ap"]["password"] = cfg.ap_pass;

  JsonObject stops = doc["stops"].to<JsonObject>();
  writeStopArray(stops, "kmb", cfg.kmb_stop_ids);
  writeStopArray(stops, "ctb", cfg.ctb_stop_ids);

  JsonObject sched = doc["sleep_schedule"].to<JsonObject>();
  writeScheduleArray(sched["wake_tasks"].to<JsonArray>(), cfg.wake_tasks);
  writeScheduleArray(sched["sleep_tasks"].to<JsonArray>(), cfg.sleep_tasks);
  sched["grace_period_minutes"] = cfg.grace_period_minutes;

  sendJson(200, doc);
}

static void handlePostConfig()
{
  if (!server.hasArg("plain"))
  {
    JsonDocument err;
    err["ok"] = false;
    err["error"] = "missing body";
    sendJson(400, err);
    return;
  }

  String body = server.arg("plain");
  JsonDocument doc;
  DeserializationError parseErr = deserializeJson(doc, body);
  if (parseErr)
  {
    JsonDocument err;
    err["ok"] = false;
    err["error"] = "invalid json";
    sendJson(400, err);
    return;
  }

  Config cfg = ConfigMgr.getConfig();

  if (doc["wifi"]["ssid"].is<const char *>())
    cfg.wifi_ssid = doc["wifi"]["ssid"].as<String>();
  if (doc["wifi"]["password"].is<const char *>())
    cfg.wifi_pass = doc["wifi"]["password"].as<String>();

  if (!doc["stops"]["kmb"].isNull())
    readStopListFromJson(doc["stops"]["kmb"], cfg.kmb_stop_ids);
  if (!doc["stops"]["ctb"].isNull())
    readStopListFromJson(doc["stops"]["ctb"], cfg.ctb_stop_ids);

  if (!doc["sleep_schedule"]["wake_tasks"].isNull())
    readScheduleArray(doc["sleep_schedule"]["wake_tasks"], cfg.wake_tasks);
  if (!doc["sleep_schedule"]["sleep_tasks"].isNull())
    readScheduleArray(doc["sleep_schedule"]["sleep_tasks"], cfg.sleep_tasks);
  if (doc["sleep_schedule"]["grace_period_minutes"].is<int>())
  {
    int g = doc["sleep_schedule"]["grace_period_minutes"].as<int>();
    if (g < 0) g = 0;
    if (g > 60) g = 60;
    cfg.grace_period_minutes = (uint8_t)g;
  }

  ConfigMgr.setConfig(cfg);
  bool ok = ConfigMgr.save();

  JsonDocument resp;
  resp["ok"] = ok;
  sendJson(ok ? 200 : 500, resp);
}

static void handlePostReboot()
{
  JsonDocument resp;
  resp["ok"] = true;
  sendJson(200, resp);
  server.client().clear();
  delay(200);
  ESP.restart();
}

static void handleNotFound()
{
  server.send(404, "text/plain", "Not found");
}

void WebPortal_Begin()
{
  server.serveStatic("/", LittleFS, "/index.html");
  server.serveStatic("/favicon.ico", LittleFS, "/favicon.ico");
  server.serveStatic("/leaflet.js", LittleFS, "/leaflet.js");
  server.serveStatic("/leaflet.css", LittleFS, "/leaflet.css");
  server.serveStatic("/leaflet.markercluster.js", LittleFS, "/leaflet.markercluster.js");
  server.serveStatic("/MarkerCluster.css", LittleFS, "/MarkerCluster.css");
  server.serveStatic("/MarkerCluster.Default.css", LittleFS, "/MarkerCluster.Default.css");
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handlePostConfig);
  server.on("/api/reboot", HTTP_POST, handlePostReboot);
  server.onNotFound(handleNotFound);

  server.begin();
  printf("WebPortal started on port 80\n");
}

void WebPortal_Loop()
{
  server.handleClient();
}
