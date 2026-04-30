#include "Sleep.h"
#include "Display.h"
#include "ConfigManager.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <time.h>

// === Configuration =========================================================

// Grace period default if config is unreadable. The active value comes from
// Config.grace_period_minutes (user-settable from the web page; 1/2/5/10/30).
static constexpr uint32_t SLEEP_GRACE_PERIOD_DEFAULT_MS = 5 * 60 * 1000UL;

// Fallback duration when sleep tasks are configured but no wake tasks exist
// (or the next wake task is more than this far away). Keeps the chip from
// being asleep indefinitely on a misconfigured schedule.
static constexpr uint64_t SLEEP_MAX_DURATION_S = 24ULL * 3600ULL;  // 24 h

// GT911 INT pin — must match ESP_PANEL_BOARD_TOUCH_INT_IO in
// include/esp_panel_board_custom_conf.h. Active-low.
static constexpr gpio_num_t TOUCH_INT_GPIO = GPIO_NUM_4;

// 1577836800 = 2020-01-01 UTC. Earlier = NTP not yet synced; we can't
// evaluate a wall-clock schedule until we have real time.
static constexpr time_t WALL_CLOCK_VALID_THRESHOLD = 1577836800;

// Boot counter that survives deep sleep — handy for confirming wake cycles
// in the serial log without manual bookkeeping.
static RTC_DATA_ATTR uint32_t s_bootCount = 0;

// === Wake-reason logging ===================================================

void Sleep_Init(void)
{
    s_bootCount++;
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    const char *reason;
    switch (cause) {
        case ESP_SLEEP_WAKEUP_TIMER:     reason = "TIMER"; break;
        case ESP_SLEEP_WAKEUP_EXT1: {
            uint64_t pins = esp_sleep_get_ext1_wakeup_status();
            printf("Sleep: boot #%lu — woke from TOUCH (ext1 pin mask 0x%llx)\n",
                   (unsigned long)s_bootCount, (unsigned long long)pins);
            return;
        }
        case ESP_SLEEP_WAKEUP_UNDEFINED: reason = "POWER-ON / RESET"; break;
        default:                         reason = "OTHER"; break;
    }
    printf("Sleep: boot #%lu — wake reason %s (%d)\n",
           (unsigned long)s_bootCount, reason, (int)cause);
}

// === Schedule computation ==================================================

// Next occurrence of `task` strictly after `now`, looking up to 8 days ahead.
// Returns 0 when the task has no active days.
static time_t nextOccurrence(const SleepScheduleTask &task, time_t now)
{
    if (task.days_mask == 0) return 0;

    struct tm now_tm;
    localtime_r(&now, &now_tm);

    for (int dayOffset = 0; dayOffset < 8; dayOffset++) {
        struct tm cand = now_tm;
        cand.tm_mday += dayOffset;
        cand.tm_hour = task.hour;
        cand.tm_min  = task.minute;
        cand.tm_sec  = 0;
        cand.tm_isdst = -1;
        time_t cand_t = mktime(&cand);
        if (cand_t == (time_t)-1 || cand_t <= now) continue;

        struct tm cand_norm;
        localtime_r(&cand_t, &cand_norm);
        if (task.days_mask & (1 << cand_norm.tm_wday)) return cand_t;
    }
    return 0;
}

// Most recent occurrence of `task` strictly before `now`, looking up to 8
// days back.
static time_t mostRecentOccurrence(const SleepScheduleTask &task, time_t now)
{
    if (task.days_mask == 0) return 0;

    struct tm now_tm;
    localtime_r(&now, &now_tm);

    for (int dayOffset = 0; dayOffset < 8; dayOffset++) {
        struct tm cand = now_tm;
        cand.tm_mday -= dayOffset;
        cand.tm_hour = task.hour;
        cand.tm_min  = task.minute;
        cand.tm_sec  = 0;
        cand.tm_isdst = -1;
        time_t cand_t = mktime(&cand);
        if (cand_t == (time_t)-1 || cand_t > now) continue;

        struct tm cand_norm;
        localtime_r(&cand_t, &cand_norm);
        if (task.days_mask & (1 << cand_norm.tm_wday)) return cand_t;
    }
    return 0;
}

static time_t earliestNext(const std::vector<SleepScheduleTask> &tasks, time_t now)
{
    time_t best = 0;
    for (const auto &t : tasks) {
        time_t n = nextOccurrence(t, now);
        if (n > 0 && (best == 0 || n < best)) best = n;
    }
    return best;
}

static time_t latestRecent(const std::vector<SleepScheduleTask> &tasks, time_t now)
{
    time_t best = 0;
    for (const auto &t : tasks) {
        time_t n = mostRecentOccurrence(t, now);
        if (n > 0 && (best == 0 || n > best)) best = n;
    }
    return best;
}

// === Tick: evaluate schedule once per second ===============================

void Sleep_Tick(void)
{
    // Throttle to once per second; nothing in the schedule changes faster.
    static time_t s_lastCheck = 0;
    time_t now = time(nullptr);
    if (now == s_lastCheck) return;
    s_lastCheck = now;

    // Wait for valid wall clock (NTP synced) before evaluating the schedule.
    if (now < WALL_CLOCK_VALID_THRESHOLD) return;

    const Config &cfg = ConfigMgr.getConfig();
    if (cfg.sleep_tasks.empty()) return;  // schedule not configured

    time_t lastSleep = latestRecent(cfg.sleep_tasks, now);
    time_t lastWake  = latestRecent(cfg.wake_tasks, now);
    bool inSleepWindow = (lastSleep > 0 && lastSleep > lastWake);

    uint32_t graceMs = (uint32_t)cfg.grace_period_minutes * 60UL * 1000UL;
    if (graceMs == 0 && cfg.grace_period_minutes != 0) graceMs = SLEEP_GRACE_PERIOD_DEFAULT_MS;
    bool inGracePeriod = (millis() < graceMs);

    bool shouldSleep = false;
    if (inSleepWindow && !inGracePeriod) {
        // Currently in a scheduled-off window past any grace period.
        shouldSleep = true;
    } else if (!inSleepWindow) {
        // In an awake window — sleep when the next scheduled sleep arrives.
        time_t nextSleep = earliestNext(cfg.sleep_tasks, now);
        if (nextSleep > 0 && nextSleep <= now + 1) shouldSleep = true;
    }

    if (!shouldSleep) return;

    // Compute how long to stay asleep: until the next wake task, capped at
    // SLEEP_MAX_DURATION_S so a typo can't strand the device for weeks.
    time_t nextWake = earliestNext(cfg.wake_tasks, now);
    uint64_t duration;
    if (nextWake > now) {
        duration = (uint64_t)(nextWake - now);
        if (duration > SLEEP_MAX_DURATION_S) duration = SLEEP_MAX_DURATION_S;
    } else {
        duration = SLEEP_MAX_DURATION_S;
    }

    char buf[32];
    struct tm wake_tm;
    if (nextWake > 0) {
        localtime_r(&nextWake, &wake_tm);
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &wake_tm);
    } else {
        snprintf(buf, sizeof(buf), "(no wake task)");
    }
    printf("Sleep: scheduled — sleeping %llu s until %s\n",
           (unsigned long long)duration, buf);

    Sleep_EnterDeepSleep(duration);
}

// === Deep sleep entry ======================================================

void Sleep_EnterDeepSleep(uint64_t sleepSeconds)
{
    // Backlight off first — the panel goes dark immediately, before the
    // ~1 s WiFi disconnect blocks.
    Display_BacklightOff();

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    esp_sleep_enable_timer_wakeup(sleepSeconds * 1000000ULL);

    // GT911 INT is open-drain with no external pull-up, so the line floats
    // low at idle. Enable the RTC pull-up (RTC variant survives the IO-domain
    // transition into deep sleep on ESP32-S3).
    rtc_gpio_pullup_en(TOUCH_INT_GPIO);
    rtc_gpio_pulldown_dis(TOUCH_INT_GPIO);
    delay(2);  // let the pull-up settle before sampling

    if (gpio_get_level(TOUCH_INT_GPIO) != 0) {
        esp_sleep_enable_ext1_wakeup(1ULL << TOUCH_INT_GPIO,
                                     ESP_EXT1_WAKEUP_ANY_LOW);
    } else {
        // Defensive: if the pull-up didn't take, skip ext1 entirely. Better
        // to lose touch wake for one cycle than to loop-wake on a held-low
        // line. The timer wake is unaffected.
        printf("Sleep: WARNING - GPIO%d held low even with pull-up; "
               "skipping TOUCH wake for this cycle\n",
               (int)TOUCH_INT_GPIO);
    }

    printf("Sleep: entering deep sleep now.\n");
    Serial.flush();
    esp_deep_sleep_start();

    // esp_deep_sleep_start() never returns; this is just for the noreturn
    // attribute to be satisfied.
    for (;;) {}
}
