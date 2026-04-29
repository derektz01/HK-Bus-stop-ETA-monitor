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
void Weather_FetchOpenMeteo(void)
{
    if (WiFi.status() != WL_CONNECTED)
    {
        printf("WiFi not connected - cannot fetch weather\r\n");
        return;
    }

    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure();   // arduino-esp32 v3.x requires explicit TLS policy
    String url = "https://api.open-meteo.com/v1/forecast?"
                 "latitude=22.2783&longitude=114.1747"
                 "&timezone=Asia/Hong_Kong"
                 "&hourly=temperature_2m,relative_humidity_2m,weather_code"
                 "&forecast_days=2";

    http.begin(client, url);
    Heap_Log("Weather pre-GET");
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK)
    {
        Heap_Log("Weather post-GET ok");
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);

        if (!error)
        {
            JsonArray times = doc["hourly"]["time"].as<JsonArray>();
            JsonArray temps = doc["hourly"]["temperature_2m"].as<JsonArray>();
            JsonArray hums = doc["hourly"]["relative_humidity_2m"].as<JsonArray>();
            JsonArray codes = doc["hourly"]["weather_code"].as<JsonArray>();

            time_t now = time(nullptr); // UTC+8
            struct tm t;
            localtime_r(&now, &t);
            char currentHour[20];
            snprintf(currentHour, sizeof(currentHour), "%04d-%02d-%02dT%02d:00",
                     t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour);
            printf("Current hour: %s\r\n", currentHour);
            for (size_t i = 0; i < times.size(); i++)
            {
                if (strcmp(times[i], currentHour) == 0)
                {
                    current_temperature = temps[i];
                    current_humidity = hums[i];
                    getWeatherFromCode(codes[i]);

                    // Update Nextion display immediately
                    Update_Weather_On_Nextion();

                    printf("Open-Meteo updated: %.1f°C, %d%%, %s\r\n",
                           current_temperature, current_humidity, current_weather_desc);
                    break;
                }
            }
        }
    }
    else
    {
        Heap_Log("Weather post-GET FAIL");
        printf("Weather HTTP %d\n", httpCode);
    }
    http.end();
}