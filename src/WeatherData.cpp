#include "WeatherData.h"
#include "Display.h"
#include "Diagnostics.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ================================================================
// Weather Data (from Open-Meteo)
// ================================================================
float current_temperature = 24.5f;
uint8_t current_humidity = 68;
char current_weather_emoji[8] = "☀️";
char current_weather_desc[32] = "晴朗";

// ================================================================
// Field filter for the Open-Meteo response. Drops the ~8 metadata fields
// the response carries (latitude, longitude, generationtime_ms,
// utc_offset_seconds, timezone, timezone_abbreviation, elevation,
// hourly_units) and the unused `hourly.time` array, leaving only the two
// arrays we index into.
// ================================================================
static const JsonDocument &weatherFilter()
{
    static JsonDocument filter;
    if (filter.isNull())
    {
        filter["hourly"]["temperature_2m"] = true;
        filter["hourly"]["weather_code"]   = true;
    }
    return filter;
}

// ================================================================
// Open-Meteo weather code to emoji + description
// ================================================================
static void getWeatherFromCode(int code)
{
    if (code == 0)
    {
        strcpy(current_weather_emoji, "☀️");
        strcpy(current_weather_desc, "晴朗");
    }
    else if (code <= 3)
    {
        strcpy(current_weather_emoji, "⛅");
        strcpy(current_weather_desc, "薄雲");
    }
    else if (code == 45 || code == 48)
    {
        strcpy(current_weather_emoji, "🌫️");
        strcpy(current_weather_desc, "霧");
    }
    else if (code <= 67)
    {
        strcpy(current_weather_emoji, "🌧️");
        strcpy(current_weather_desc, "雨");
    }
    else if (code <= 77)
    {
        strcpy(current_weather_emoji, "❄️");
        strcpy(current_weather_desc, "雪");
    }
    else if (code <= 82)
    {
        strcpy(current_weather_emoji, "🌧️");
        strcpy(current_weather_desc, "陣雨");
    }
    else if (code >= 95)
    {
        strcpy(current_weather_emoji, "⛈️");
        strcpy(current_weather_desc, "雷暴");
    }
    else
    {
        strcpy(current_weather_emoji, "☁️");
        strcpy(current_weather_desc, "多雲");
    }
}

// ================================================================
// Fetch latest hourly data from Open-Meteo API (Hong Kong)
// ================================================================
extern volatile bool g_apiFetchInProgress;

void Weather_FetchOpenMeteo(void)
{
    if (WiFi.status() != WL_CONNECTED)
    {
        printf("WiFi not connected - cannot fetch weather\r\n");
        return;
    }

    g_apiFetchInProgress = true;
    HTTPClient http;
    String url = "https://api.open-meteo.com/v1/forecast?"
                 "latitude=22.2783&longitude=114.1747"
                 "&timezone=Asia/Hong_Kong"
                 "&hourly=temperature_2m,weather_code"
                 "&forecast_days=1";

    http.begin(url);
    Heap_Log("Weather pre-GET");
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK)
    {
        Heap_Log("Weather post-GET ok");
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError error = deserializeJson(
            doc, payload,
            DeserializationOption::Filter(weatherFilter()));
        if (!error)
        {
            // Open-Meteo's hourly arrays are aligned to local time when
            // `timezone=` is set, so element N is N:00 of today. Read only
            // the entry at the current hour — no iteration, no per-hour
            // timestamp strings kept in memory.
            time_t now = time(nullptr);
            struct tm t;
            localtime_r(&now, &t);
            int hour = t.tm_hour;  // 0..23

            float temp = doc["hourly"]["temperature_2m"][hour] | -999.0f;
            int   code = doc["hourly"]["weather_code"][hour]   | -1;

            if (temp > -200.0f && code >= 0)
            {
                current_temperature = temp;
                getWeatherFromCode(code);

                // Push the new values to the LVGL UI immediately.
                Update_Weather();

                printf("Open-Meteo updated: %.1f°C, %s (hour=%d)\r\n",
                       current_temperature, current_weather_desc, hour);
            }
            else
            {
                printf("Open-Meteo: missing data at hour=%d (temp=%.1f code=%d)\n",
                       hour, temp, code);
            }
        }
    }
    else
    {
        Heap_Log("Weather post-GET FAIL");
        printf("Weather HTTP %d\n", httpCode);
    }
    http.end();
    g_apiFetchInProgress = false;
}