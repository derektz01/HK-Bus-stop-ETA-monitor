#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Logs the wake-up reason from the previous deep sleep (timer / touch /
// power-on). Call from setup() right after Serial.begin().
void Sleep_Init(void);

// Evaluate the user-configured sleep/wake schedule once per loop iteration.
// Enters deep sleep when the current wall-clock time falls inside a sleep
// window (subject to a post-boot grace period). No-op when no schedule is
// configured or before NTP has synced the clock.
void Sleep_Tick(void);

// Enter deep sleep for the given number of seconds. Touch on the GT911 INT
// pin (GPIO 4, active-low) also wakes the device. Powers down the backlight
// and Wi-Fi radio first so power draw drops cleanly. Does not return.
void Sleep_EnterDeepSleep(uint64_t sleepSeconds) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif
