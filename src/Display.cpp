#include "Display.h"
#include "BusData.h"
#include "WeatherData.h"
#include "HolidayData.h"
#include "Wireless.h"
#include "Diagnostics.h"
#include "lvgl_v8_port.h"
#include "ui/ui.h"
#include <esp_display_panel.hpp>
#include <Arduino.h>
#include <time.h>

using namespace esp_panel::drivers;
using namespace esp_panel::board;

static Board *s_board = nullptr;

struct LvglGuard {
    LvglGuard()  { lvgl_port_lock(-1); }
    ~LvglGuard() { lvgl_port_unlock(); }
};
#define WITH_LVGL() LvglGuard _lvgl_guard

void Display_Init()
{
    s_board = new Board();
    s_board->init();

#if LVGL_PORT_AVOID_TEARING_MODE
    auto lcd = s_board->getLCD();
    lcd->configFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM);
#  if ESP_PANEL_DRIVERS_BUS_ENABLE_RGB && CONFIG_IDF_TARGET_ESP32S3
    auto bus = lcd->getBus();
    if (bus->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
        static_cast<BusRGB *>(bus)->configRGB_BounceBufferSize(lcd->getFrameWidth() * 10);
    }
#  endif
#endif
    assert(s_board->begin());

    lvgl_port_init(s_board->getLCD(), s_board->getTouch());

    lvgl_port_lock(-1);
    ui_init();
    // Make the wifi info label scroll horizontally like the Nextion gWifiInfo did.
    //lv_label_set_long_mode(ui_lblWifiInfo, LV_LABEL_LONG_SCROLL_CIRCULAR);
    // SquareLine clears CLICKABLE on ui_ctrTouch (ui_Main.c) so the touch
    // hotspot never receives LV_EVENT_PRESSED. Re-enable here so we don't
    // have to edit a generated file.
    lv_obj_add_flag(ui_ctrTouch, LV_OBJ_FLAG_CLICKABLE);
    // Collapse the three SquareLine background widgets into one. The day
    // widget stays and Update_Background() swaps its image source via
    // lv_img_set_src(); the other two are deleted from the scene graph so
    // LVGL doesn't iterate them on every refresh.
    lv_obj_del(ui_imgBackgroundSunset);
    lv_obj_del(ui_imgBackgroundNight);
    ui_imgBackgroundSunset = nullptr;
    ui_imgBackgroundNight  = nullptr;
    lvgl_port_unlock();

    printf("Display initialised (LVGL on Waveshare ESP32-S3-Touch-LCD-4.3, 800x480)\n");
    Heap_Log("after Display_Init");
}

// ============================================================================
// Time / date
// ============================================================================
void Update_Time()
{
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);

    char s[6];
    snprintf(s, sizeof(s), "%02d:%02d", t.tm_hour, t.tm_min);

    // HH:MM only changes once per minute; skip ~59 of every 60 calls.
    static char last[6] = "";
    if (strcmp(s, last) == 0) return;
    strcpy(last, s);
    WITH_LVGL();
    lv_label_set_text(ui_lblNowTime, s);
}

void Update_Date_And_Weekday()
{
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);

    static const char *weekdays[] = {"日", "一", "二", "三", "四", "五", "六"};
    char s[40];
    snprintf(s, sizeof(s), "%04d年%02d月%02d日(%s)",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, weekdays[t.tm_wday]);

    // Date string only changes once per day.
    static char last[40] = "";
    if (strcmp(s, last) == 0) return;
    strcpy(last, s);

    WITH_LVGL();
    lv_label_set_text(ui_lblNowDate, s);
}

// ============================================================================
// Weather
// ============================================================================
void Update_Weather()
{
    char temp[16];
    snprintf(temp, sizeof(temp), "%.1f", current_temperature);

    static char lastTemp[16] = "";
    static char lastDesc[32] = "";
    bool tempChanged = strcmp(temp, lastTemp) != 0;
    bool descChanged = strcmp(current_weather_desc, lastDesc) != 0;
    if (!tempChanged && !descChanged) return;
    if (tempChanged) strcpy(lastTemp, temp);
    if (descChanged) {
        strncpy(lastDesc, current_weather_desc, sizeof(lastDesc) - 1);
        lastDesc[sizeof(lastDesc) - 1] = '\0';
    }
    printf("Weather updated → %s, %.1f°C\n", current_weather_desc, current_temperature);
    WITH_LVGL();
    if (tempChanged) lv_label_set_text(ui_lblTemp, temp);
    if (descChanged) lv_label_set_text(ui_lblWeather, current_weather_desc);
}

// ============================================================================
// Holiday — logic ported from former Nextion.cpp:27-101
// ============================================================================
void Update_Holiday_Display()
{
    char info[96]        = "";
    char dateDisplay[40] = "-";
    String nextHolidayName;
    int minDays = 999;

    if (holidayDoc["holidays"].isNull()) {
        snprintf(info, sizeof(info), "公眾假期資料載入失敗");
        printf("Holiday data not loaded - using fallback\n");
    } else if (time(nullptr) < 1577836800) {
        // Wall clock not synced (system still on 1970 epoch). Without this
        // guard, every 2026 holiday is ~20 000 days away → no `daysLeft`
        // beats the sentinel, `minDays` stays at 999, and the label reads
        // "還有999天". The networkTask re-invokes us once SNTP delivers
        // a real timestamp.
        snprintf(info, sizeof(info), "等待網絡時間同步...");
        printf("Holiday display deferred — wall clock not synced\n");
    } else {
        time_t now = time(nullptr);
        struct tm today;
        localtime_r(&now, &today);
        // Normalise today to local midnight so the diff against holiday-midnight
        // produces a whole-day count instead of fractional hours rounded toward
        // zero. Without this, "today 14:30 vs holiday tomorrow 00:00" yields
        // 9.5 h / 86400 = 0 days and `今天是X` would fire one day early.
        today.tm_hour = 0;
        today.tm_min  = 0;
        today.tm_sec  = 0;
        time_t todaySeconds = mktime(&today);

        String nextHolidayDate;

        JsonArray holidays = holidayDoc["holidays"].as<JsonArray>();
        for (JsonObject h : holidays) {
            const char *dateStr = h["date"]    | "";
            const char *name    = h["name_tc"] | "";

            struct tm holidayTm = {};
            sscanf(dateStr, "%4d-%2d-%2d", &holidayTm.tm_year, &holidayTm.tm_mon, &holidayTm.tm_mday);
            holidayTm.tm_year -= 1900;
            holidayTm.tm_mon  -= 1;
            time_t holidaySeconds = mktime(&holidayTm);

            int daysLeft = (int)difftime(holidaySeconds, todaySeconds) / (60 * 60 * 24);
            if (daysLeft >= 0 && daysLeft < minDays) {
                minDays = daysLeft;
                nextHolidayName = name;
                nextHolidayDate = dateStr;
            }
        }

        if (minDays == 0) {
            snprintf(info, sizeof(info), "今天是%s", nextHolidayName.c_str());
        } else if (minDays == 1) {
            snprintf(info, sizeof(info), "明天是%s", nextHolidayName.c_str());
        } else {
            snprintf(info, sizeof(info), "%s還有%d天", nextHolidayName.c_str(), minDays);
        }

        if (nextHolidayDate.length() > 0) {
            struct tm nextDate = {};
            sscanf(nextHolidayDate.c_str(), "%4d-%2d-%2d",
                   &nextDate.tm_year, &nextDate.tm_mon, &nextDate.tm_mday);
            nextDate.tm_year -= 1900;
            nextDate.tm_mon  -= 1;
            mktime(&nextDate);
            static const char *weekdays[] = {"日", "一", "二", "三", "四", "五", "六"};
            snprintf(dateDisplay, sizeof(dateDisplay), "%04d年%02d月%02d日(%s)",
                     nextDate.tm_year + 1900,
                     nextDate.tm_mon + 1,
                     nextDate.tm_mday,
                     weekdays[nextDate.tm_wday]);
        }
    }

    static char lastInfo[96] = "";
    static char lastDate[40] = "";
    bool infoChanged = strcmp(info, lastInfo) != 0;
    bool dateChanged = strcmp(dateDisplay, lastDate) != 0;
    if (!infoChanged && !dateChanged) return;
    if (infoChanged) strcpy(lastInfo, info);
    if (dateChanged) strcpy(lastDate, dateDisplay);

    {
        WITH_LVGL();
        if (infoChanged) lv_label_set_text(ui_lblHolidayInfo, info);
        if (dateChanged) lv_label_set_text(ui_lblHolidayDate, dateDisplay);
    }
    printf("Holiday updated → %s 還有 %d 天\n", nextHolidayName.c_str(), minDays);
}

// ============================================================================
// Bus list (4 slots per page, 2 ETA columns each)
// ============================================================================
void Update_Bus_List()
{
    lv_obj_t *route[4] = { ui_lblRoute1, ui_lblRoute2, ui_lblRoute3, ui_lblRoute4 };
    lv_obj_t *eta1[4]  = { ui_lblETA11,  ui_lblETA21,  ui_lblETA31,  ui_lblETA41 };
    lv_obj_t *eta2[4]  = { ui_lblETA12,  ui_lblETA22,  ui_lblETA32,  ui_lblETA42 };
    lv_obj_t *dest[4]  = { ui_lblDest1,  ui_lblDest2,  ui_lblDest3,  ui_lblDest4 };

    // Format the per-slot log lines into stack buffers while locked, then
    // print AFTER releasing both locks. USB-CDC printf takes 1-10 ms each
    // and previously held WITH_LVGL for ~50-200 ms, blocking the LVGL render
    // task and triggering a contention burst when the lock finally released.
    char lines[4][128];
    int  lineCount = 0;
    int  page      = 0;
    int  total     = 0;

    {
        // Lock order rule: WITH_LVGL first, then BusData_Lock — never the reverse.
        WITH_LVGL();
        BusData_Lock();
        page      = currentPage;
        int start = page * 4;
        total     = (int)displayRoutes.size();
        for (int s = 0; s < 4; s++) {
            int i = start + s;
            bool has = (i < total);
            const char *r  = has ? displayRoutes[i].route        : "";
            const char *e1 = has ? displayRoutes[i].etaDisplay1  : "";
            const char *e2 = has ? displayRoutes[i].etaDisplay2  : "";
            const char *d  = has ? displayRoutes[i].destination  : "";
            lv_label_set_text(route[s], r);
            lv_label_set_text(eta1[s],  e1);
            lv_label_set_text(eta2[s],  e2);
            lv_label_set_text(dest[s],  d);
            if (has) {
                snprintf(lines[lineCount], sizeof(lines[lineCount]),
                         "Displayed Route %d | %s | %s | %s | %s",
                         s + 1, r, e1, e2, d);
                lineCount++;
            }
        }
        BusData_Unlock();
    }

    for (int i = 0; i < lineCount; i++) {
        printf("%s\n", lines[i]);
    }
    printf("Bus list updated (Page %d, Total: %d routes)\n", page, total);
}

// ============================================================================
// Background switch by time of day:
//   06:00 - 16:59 -> Day
//   17:00 - 18:29 -> Sunset
//   18:30 - 05:59 -> Night
// ============================================================================
void Update_Background()
{
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);

    int minutes = t.tm_hour * 60 + t.tm_min;
    int pic;
    if (minutes >= 6 * 60 && minutes <= 16 * 60 + 59)
        pic = 0;
    else if (minutes >= 17 * 60 && minutes <= 18 * 60 + 29)
        pic = 1;
    else
        pic = 2;

    static int lastPic = -1;
    if (pic == lastPic) return;
    lastPic = pic;

    // Image descriptors live in flash; lv_img_set_src just stores the pointer.
    const lv_img_dsc_t *src = (pic == 0) ? &ui_img_261349413     // hk-day-lvgl.png
                            : (pic == 1) ? &ui_img_1167879829    // hk-sunset-lvgl.png
                                         : &ui_img_783909477;    // hk-night-lvgl.png

    WITH_LVGL();
    lv_img_set_src(ui_imgBackgroundDay, src);
    printf("Background switched to pic=%d\n", pic);
}

// ============================================================================
// Wi-Fi info banner (replaces Nextion gWifiInfo scrolling text)
// ============================================================================
// Cycle state for the wifi-info banner. The label is too narrow for the full
// "AP:<ssid> P:<pwd> IP:<ip>" string and we don't want the scrolling animation
// (PSRAM-bandwidth contention with the LCD EDMA). Instead we rotate through
// short frames; the loop calls Cycle_Wifi_Info() every few seconds to advance.
static int  s_wifiFrame          = 0;
static char s_wifiLastText[80]   = "";

static void paintWifiFrame(int frameIndex)
{
    char buf[80];
    GetWiFiInfoFrame(frameIndex, buf, sizeof(buf));
    if (strcmp(buf, s_wifiLastText) == 0) return;
    strncpy(s_wifiLastText, buf, sizeof(s_wifiLastText) - 1);
    s_wifiLastText[sizeof(s_wifiLastText) - 1] = '\0';

    WITH_LVGL();
    lv_label_set_text(ui_lblWifiInfo, buf);
}

void ShowWifiInfo()
{
    s_wifiFrame       = 0;
    s_wifiLastText[0] = '\0';   // force first paint via cache miss

    char buf[80];
    GetWiFiInfoFrame(0, buf, sizeof(buf));
    strncpy(s_wifiLastText, buf, sizeof(s_wifiLastText) - 1);
    s_wifiLastText[sizeof(s_wifiLastText) - 1] = '\0';

    WITH_LVGL();
    lv_label_set_text(ui_lblWifiInfo, buf);
    // Translucent backdrop (alpha 150/255) so the SSID/IP stays readable
    // against any background image.
    lv_obj_set_style_bg_opa(ui_lblWifiInfo, 150, (uint32_t)LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(ui_lblWifiInfo, LV_OBJ_FLAG_HIDDEN);
}

void Cycle_Wifi_Info()
{
    int count = GetWiFiInfoFrameCount();
    if (count <= 0) return;
    s_wifiFrame = (s_wifiFrame + 1) % count;
    paintWifiFrame(s_wifiFrame);
}

void HideWifiInfo()
{
    {
        WITH_LVGL();
        lv_label_set_text(ui_lblWifiInfo, "");
        lv_obj_set_style_bg_opa(ui_lblWifiInfo, 150, (uint32_t)LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_flag(ui_lblWifiInfo, LV_OBJ_FLAG_HIDDEN);
    }
    // Logged outside the LVGL guard so we measure the heap *after* any
    // simple-layer buffer (LV_LAYER_SIMPLE_BUF_SIZE) has been freed.
    Heap_Log("after HideWifiInfo");
}

// ============================================================================
// Touch callback bridge — invoked from src/ui/ui_events.c::NextPage().
// Runs inside the LVGL task; recursive mutex makes Update_Bus_List safe.
// ============================================================================
extern "C" void OnNextPagePressed(void)
{
    Switch_To_Next_Page();
    Update_Bus_List();
}
