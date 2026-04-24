# HK Bus Stop ETA Monitor

Real-time Hong Kong bus arrival time display using **Nextion** + **Waveshare ESP32-C6-LCD-1.47**.

## Overview

This project turns a Waveshare ESP32-C6 board with a Nextion 4.3" display into a professional-looking bus stop electronic arrival board.

It fetches live ETA data from:
- KMB (九巴)
- Citybus (城巴) using the official V2 API

The display shows:
- Current time and date
- Next public holiday countdown
- Up to 4 bus routes per page with smart ETA (`-`, `< 1`, or minutes)
- Auto slideshow (changes page every 3 seconds)
- Manual page switching via touch hotspot on the right side

## Features

- Real-time KMB + Citybus ETA (only the next scheduled bus)
- **Multi-stop support** — pick any number of KMB *and* Citybus stops via the web UI; the device cycles through every selected stop
- **Captive web settings portal** — configure Wi-Fi, stops, and reboot from a browser (`/api/config`, `/api/reboot` in [src/WebPortal.cpp](src/WebPortal.cpp))
- **Dynamic Citybus route/stop discovery** — no hardcoded route lists; the browser builds and caches the full stop list, with a 60-second cooldown and 2-hour rate-limit gate to protect the upstream API
- **AP-mode fallback** — device starts its own Wi-Fi AP if it can't join the configured network ([src/Wireless.cpp](src/Wireless.cpp))
- Both inbound and outbound directions shown separately when available
- Smart ETA display (`-` = arriving soon, `< 1` = less than 1 minute)
- Routes automatically sorted by route number
- Hong Kong public holiday countdown (from `holiday.json`)
- Auto slideshow every 3 seconds
- Manual touch control (right side hotspot)
- NTP time synchronization (Hong Kong time, UTC+8)
- Clean separation of concerns (BusData, HolidayData, Nextion, WebPortal, ConfigManager, Wireless)

## Hardware

- **Main board**: Waveshare ESP32-C6-LCD-1.47 (or other ESP32-C6 board)
- **Display**: Nextion NX4827P043-011C (4.3" Intelligent HMI)
- **Connection**: UART1 (TX=16, RX=17) + 5V/GND

## Repository Structure

```
src/
├── main.cpp                 # Main program, timers, slideshow
├── Wireless.cpp / .h        # Wi-Fi STA + AP-mode fallback
├── WebPortal.cpp / .h       # HTTP server: /api/config, /api/reboot, serves index.html
├── ConfigManager.cpp / .h   # Persisted config (Wi-Fi, stop ID lists) on LittleFS
├── Nextion.cpp / .h         # All Nextion display logic
├── BusData.cpp / .h         # KMB + Citybus ETA fetching & processing
├── HolidayData.cpp / .h     # Holiday JSON loading & countdown
└── WeatherData.cpp / .h     # Open-Meteo weather (optional)
data/
├── index.html               # Captive web settings portal (uploaded to LittleFS)
├── config.json              # Runtime config (Wi-Fi + selected stops)
└── holiday.json             # 2026 Hong Kong public holidays
```

## Dependencies

The build relies on these PlatformIO libraries:

- `moononournation/GFX Library for Arduino @ ^1.6.5`
- `esp-arduino-libs/esp-lib-utils@^0.3.0`
- `bblanchon/ArduinoJson @ ^7.2.0`

## Configuration

All runtime configuration — Wi-Fi credentials, KMB stop IDs, Citybus stop IDs — is managed at runtime through the device's **captive web portal**. Nothing needs to be set at compile time.

### First boot

If the device cannot join the saved Wi-Fi (or none is configured), it falls back to AP mode ([src/Wireless.cpp](src/Wireless.cpp)). Connect your phone or laptop to the AP advertised by the device, then open `http://192.168.4.1` in a browser.

Once the device is on your home Wi-Fi, the same page is reachable at the device's IP (shown on its display).

### Using the settings page

1. Connect to the device (AP mode or station mode).
2. Open the IP in a browser.
3. Enter Wi-Fi SSID and password.
4. Add KMB and/or Citybus stops — search by Chinese name or paste a stop ID directly. **You can add as many stops as you want** for either operator.
5. Click **Save**, then **Reboot** if you changed Wi-Fi credentials.

See **[TUTORIAL.md](TUTORIAL.md)** for a full screenshot walkthrough, including the cache-rebuild cooldown and rate-limit gate.

### Holiday Data

The `data/holiday.json` file contains Hong Kong holiday information for use in the display (e.g., for holiday countdown messages).

**To upload the holiday data to the device, run:**

```bash
pio run -t uploadfs
```

This will upload the `data/holiday.json` file to the ESP32 filesystem for use by the firmware.

## Build & Upload

```bash
# 1. Upload data/ to LittleFS (settings page + holiday data)
pio run -t uploadfs

# 2. Build and upload firmware
pio run -t upload

# 3. Build only
pio run
```

## Notes

- The display automatically cycles through pages every **3 seconds**.
- You can still tap the right side to manually switch pages.
- If Wi-Fi fails, the board continues running with the last known data.
- Holiday countdown updates automatically every day.

## License

This is a personal project. Feel free to modify it for your own bus stop.
