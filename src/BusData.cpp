#include "BusData.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "Display.h"
#include "Diagnostics.h"
#include <time.h>
#include <vector>
#include <algorithm>

// ================================================================
// Global storage
// ================================================================
std::vector<BusInfo> busList;       // Staging area, only touched on the fetch task
std::vector<BusInfo> displayRoutes; // Sorted for display, read by other tasks

uint8_t currentPage = 0;
const uint8_t ROUTES_PER_PAGE = 4;

// Recursive mutex guarding displayRoutes + currentPage.
// Lock order rule: take WITH_LVGL FIRST, then BusData_Lock (never the reverse)
// to avoid deadlock between the LVGL task on core 1 and the network task on core 0.
static SemaphoreHandle_t s_busMutex = nullptr;

void BusData_Lock()
{
    if (s_busMutex) xSemaphoreTakeRecursive(s_busMutex, portMAX_DELAY);
}

void BusData_Unlock()
{
    if (s_busMutex) xSemaphoreGiveRecursive(s_busMutex);
}

// ================================================================
// Parse ISO8601 time
// ================================================================
static time_t parseISO8601(const char *isoTime)
{
    struct tm t = {};
    int year, month, day, hour, min, sec;
    sscanf(isoTime, "%4d-%2d-%2dT%2d:%2d:%2d", &year, &month, &day, &hour, &min, &sec);

    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = min;
    t.tm_sec = sec;

    return mktime(&t);
}

// ================================================================
// Calculate ETA display string
// ================================================================
static void calculateETA(const char *etaStr, const char *dataTimestampStr, char *displayStr)
{
    if (etaStr == nullptr || strlen(etaStr) == 0)
    {
        strcpy(displayStr, "-");
        return;
    }

    time_t etaTime = parseISO8601(etaStr);
    time_t dataTime = parseISO8601(dataTimestampStr);
    int diffSeconds = (int)difftime(etaTime, dataTime);

    if (diffSeconds < 30 || diffSeconds < 0)
    {
        strcpy(displayStr, "-");
    }
    else if (diffSeconds < 60)
    {
        strcpy(displayStr, "<1");
    }
    else
    {
        sprintf(displayStr, "%d", diffSeconds / 60);
    }
}

// ================================================================
// Add route with seq=1 + seq=2 support + skip rule for KMB
// ================================================================
static void addRoute(const char *route, int seq, const char *etaStr, const char *dataTs, const char *dest, const char *dir)
{
    if (seq != 1 && seq != 2)
        return;

    // Requirement 1: For KMB, if eta_seq=1 and eta is null → skip entire route
    if (seq == 1 && (etaStr == nullptr || strlen(etaStr) == 0))
    {
        printf("Skipped route %s (eta_seq=1 is null - not in service)\n", route);
        return;
    }

    char etaDisplay[8] = "-";
    if (etaStr && strlen(etaStr) > 0)
    {
        calculateETA(etaStr, dataTs, etaDisplay);
    }

    // Check if this route+dir already exists
    for (auto &existing : busList)
    {
        if (strcmp(existing.route, route) == 0 && strcmp(existing.dir, dir) == 0)
        {
            if (seq == 1)
                strcpy(existing.etaDisplay1, etaDisplay);
            else
                strcpy(existing.etaDisplay2, etaDisplay);
            return;
        }
    }

    // New route entry
    BusInfo info = {};
    strncpy(info.route, route, sizeof(info.route) - 1);
    strncpy(info.destination, dest, sizeof(info.destination) - 1);
    strncpy(info.dir, dir, sizeof(info.dir) - 1);

    // Default both to "-"
    strcpy(info.etaDisplay1, "-");
    strcpy(info.etaDisplay2, "-");

    if (seq == 1)
        strcpy(info.etaDisplay1, etaDisplay);
    else
        strcpy(info.etaDisplay2, etaDisplay);

    busList.push_back(info);
}

// ================================================================
// Rebuild sorted display list
// ================================================================
static void rebuildDisplayList()
{
    // Build sorted list into a local first, then atomically swap under the
    // mutex so readers on core 1 never see a half-rebuilt vector.
    std::vector<BusInfo> next = busList;
    std::sort(next.begin(), next.end(), [](const BusInfo &a, const BusInfo &b)
              {
        int cmp = strcmp(a.route, b.route);
        if (cmp == 0) return strcmp(a.dir, b.dir) < 0;
        return cmp < 0; });

    BusData_Lock();
    displayRoutes.swap(next);
    BusData_Unlock();
}

// ================================================================
// Citybus Batch Stop ETA (all routes for this stop in one call)
// ================================================================
void Fetch_Citybus_StopETA(const char *stop_id)
{
    if (WiFi.status() != WL_CONNECTED)
        return;
    if (!stop_id || strlen(stop_id) == 0)
        return;

    HTTPClient http;
    String url = "https://rt.data.gov.hk/v1/transport/batch/stop-eta/ctb/";
    url += stop_id;
    url += "?lang=zh-hant";

    http.begin(url);
    Heap_Log("Citybus pre-GET");
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK)
    {
        Heap_Log("Citybus post-GET ok");
        // Stream-parse from the WiFi socket — avoids materialising a 20-50 KB
        // String in PSRAM (which contended with the RGB EDMA and caused drift).
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, http.getStream());

        if (!error && doc["data"].is<JsonArray>())
        {
            JsonArray data = doc["data"].as<JsonArray>();

            for (JsonObject item : data)
            {
                int seq = item["eta_seq"] | 99;
                if (seq != 1 && seq != 2)
                    continue;

                const char *route = item["route"] | "";
                const char *etaStr = item["eta"] | "";
                const char *dataTs = item["data_timestamp"] | "";
                const char *dest = item["dest"] | "未知目的地";
                const char *dir = item["dir"] | "I";

                addRoute(route, seq, etaStr, dataTs, dest, dir);
            }
        }
        else
        {
            printf("Citybus batch ETA parse error: %s\n", error.c_str());
        }
    }
    else
    {
        Heap_Log("Citybus post-GET FAIL");
        printf("Citybus batch ETA HTTP %d\n", httpCode);
    }
    http.end();
    // No rebuildDisplayList() here — AutoRefreshBusETA does it once at the end
    // of the cycle, so the slideshow tick on core 1 doesn't see a half-built
    // KMB-only displayRoutes mid-fetch.
}

// ================================================================
// KMB Stop ETA (seq=1 + seq=2)
// ================================================================
void Fetch_KMB_StopETA(const char *stop_id)
{
    if (WiFi.status() != WL_CONNECTED)
        return;

    HTTPClient http;
    String url = "https://data.etabus.gov.hk/v1/transport/kmb/stop-eta/";
    url += stop_id;

    http.begin(url);
    Heap_Log("KMB pre-GET");
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK)
    {
        Heap_Log("KMB post-GET ok");
        // Stream-parse from the WiFi socket — see Fetch_Citybus_StopETA for rationale.
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, http.getStream());

        if (!error && doc["data"].is<JsonArray>())
        {
            JsonArray data = doc["data"].as<JsonArray>();

            for (JsonObject item : data)
            {
                int seq = item["eta_seq"] | 99;
                if (seq != 1 && seq != 2)
                    continue;

                const char *route = item["route"] | "";
                const char *etaStr = item["eta"] | "";
                const char *dataTs = item["data_timestamp"] | "";
                const char *dest = item["dest_tc"] | "未知目的地";
                const char *dir = item["dir"] | "I";

                addRoute(route, seq, etaStr, dataTs, dest, dir);
            }
        }
    }
    else
    {
        Heap_Log("KMB post-GET FAIL");
        printf("KMB stop-ETA HTTP %d\n", httpCode);
    }
    http.end();
    // No rebuildDisplayList() here — see Fetch_Citybus_StopETA.
}

// ================================================================
// Auto Refresh (multi-stop)
// ================================================================
extern volatile bool g_apiFetchInProgress;

void AutoRefreshBusETA(const std::vector<String> &kmb_stop_ids,
                       const std::vector<String> &ctb_stop_ids)
{
    printf("=== Auto Refresh Bus ETA (KMB:%d CTB:%d) ===\r\n",
           (int)kmb_stop_ids.size(), (int)ctb_stop_ids.size());

    g_apiFetchInProgress = true;
    busList.clear();

    for (const String &id : kmb_stop_ids)
    {
        if (id.length() == 0)
            continue;
        Fetch_KMB_StopETA(id.c_str());
        delay(100);
    }

    for (const String &id : ctb_stop_ids)
    {
        if (id.length() == 0)
            continue;
        Fetch_Citybus_StopETA(id.c_str());
        delay(100);
    }

    rebuildDisplayList();

    BusData_Lock();
    currentPage = 0;
    int total = (int)displayRoutes.size();
    BusData_Unlock();

    printf("Combined ETA updated: %d routes (seq=1 + seq=2)\n", total);
    Update_Bus_List();
    g_apiFetchInProgress = false;
}

void BusData_Init()
{
    if (!s_busMutex) {
        s_busMutex = xSemaphoreCreateRecursiveMutex();
    }
    printf("Bus Data Initialized (KMB + Citybus with seq=1 + seq=2 support)\r\n");
}

void Switch_To_Next_Page()
{
    BusData_Lock();
    int total = displayRoutes.size();
    int maxPage = (total + ROUTES_PER_PAGE - 1) / ROUTES_PER_PAGE - 1;
    if (maxPage < 0)
        maxPage = 0;
    currentPage = (currentPage + 1) % (maxPage + 1);
    uint8_t page = currentPage;
    BusData_Unlock();
    printf("Switched to page %d\n", page);
}