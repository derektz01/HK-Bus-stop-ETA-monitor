# Pin Assignment — ESP32-C6 ↔ Nextion

This project is built and tested on the **Waveshare ESP32-C6-LCD-1.47**, but **any ESP32-C6 board will work**. Only one connection actually matters: a free UART pair between the ESP32-C6 and the Nextion display.

---

## Display details

| Item            | Value                                                            |
|-----------------|------------------------------------------------------------------|
| Nextion model   | **NX4827P043-011C** (4.3" Intelligent series HMI)                |
| Native resolution | **480 × 272**                                                  |
| HMI file target | The HMI project shipped with this firmware is **designed for 480 × 272**. Flashing it to a panel with a different resolution will misalign every label and image. |
| UART baud       | 9600 (8N1) — set in [src/Nextion.cpp](src/Nextion.cpp)           |
| Power           | 5 V, ~250 mA                                                     |

---

## The only rule that matters

The Nextion uses one UART. Wire it **crossed**:

```
ESP32-C6 TX ──────► Nextion RX
ESP32-C6 RX ◄────── Nextion TX
GND        ──────── GND
5V (VCC)   ──────── 5V
```

> If the screen powers on but never receives commands, you almost certainly have TX↔TX or RX↔RX (uncrossed). Swap the two data wires.

The firmware initialises the UART in [src/main.cpp](src/main.cpp):

```cpp
// Nextion_Init(txPin, rxPin)
Nextion_Init(16, 17);
```

So **GPIO 16 = ESP32-C6 TX** and **GPIO 17 = ESP32-C6 RX**. Pin numbers and baud rate are the only things you might want to change for a different board.

---

## Wiring table (Waveshare ESP32-C6-LCD-1.47)

| Nextion pin | Direction      | ESP32-C6 GPIO | Notes                                      |
|-------------|----------------|---------------|--------------------------------------------|
| RX          | ESP32-C6 → Nx  | **GPIO 16**   | First arg of `Nextion_Init`                |
| TX          | Nx → ESP32-C6  | **GPIO 17**   | Second arg of `Nextion_Init`               |
| GND         | —              | GND           | Shared ground — required                   |
| 5V (VCC)    | —              | 5V            | Take from the USB/5V pin, **not 3.3 V**    |

GPIO 16/17 are broken out to the side header on the Waveshare board, are not shared with any on-board peripheral (see "Why GPIO 16/17" below), and have been validated by a 24-hour continuous-run soak test.

---

## Why GPIO 16 / 17 (and not something else)

ESP32-C6's I/O matrix lets *any* GPIO drive a UART, so the choice is mostly about avoiding pins the on-board peripherals already own. On the Waveshare ESP32-C6-LCD-1.47 the on-board hardware claims the following pins:

| GPIO       | On-board use                                      | Safe for UART? |
|------------|---------------------------------------------------|----------------|
| GPIO 6     | LCD MOSI (ST7789, fixed wiring)                   | No             |
| GPIO 7     | LCD SCLK                                          | No             |
| GPIO 14    | LCD CS                                            | No             |
| GPIO 15    | LCD DC                                            | No             |
| GPIO 21    | LCD RST                                           | No             |
| GPIO 22    | LCD backlight (BL)                                | No             |
| GPIO 4     | microSD CS (slot present on board)                | Avoid          |
| GPIO 5     | microSD MISO                                      | Avoid          |
| GPIO 8     | RGB LED data line (WS2812) and a strapping pin    | Avoid          |
| GPIO 9     | BOOT button + strapping pin                       | Avoid          |
| GPIO 12    | USB D– (USB-CDC console)                          | No             |
| GPIO 13    | USB D+ (USB-CDC console)                          | No             |
| **GPIO 16**| Free, broken out to header                        | **Yes**        |
| **GPIO 17**| Free, broken out to header                        | **Yes**        |
| GPIO 0–3, 10, 11, 18–20, 23 | Free, broken out to header           | Yes            |

The firmware uses USB-CDC on boot (`ARDUINO_USB_CDC_ON_BOOT=1` in [platformio.ini](platformio.ini)), so pins 12/13 are reserved for the USB console and must not be touched. The LCD wiring is hard-soldered on the Waveshare board and cannot be changed.

GPIO 16 and 17 are simply the most convenient free pair: adjacent on the header, not shared with anything, and conventionally labelled TX/RX on most ESP32 dev boards.

---

## Using a different ESP32-C6 board

The pin numbers in `Nextion_Init(16, 17)` are not magic — they are passed straight through to `HardwareSerial::begin(baud, config, rxPin, txPin)` and the chip's I/O matrix routes UART1 to whatever GPIOs you specify. To use a different board:

1. Open the board's pinout diagram and pick **any two free GPIOs** that are:
   - Not used by any on-board peripheral (LCD, SD, USB, RGB LED, etc.)
   - Not strapping pins (avoid GPIO 8 and GPIO 9 on ESP32-C6)
   - Not the USB D+/D– pair if you rely on USB-CDC for the console (GPIO 12/13 on ESP32-C6)
2. Edit [src/main.cpp](src/main.cpp) and change the arguments of `Nextion_Init(tx, rx)` to your chosen pins.
3. Wire crossed: your TX → Nextion RX, your RX → Nextion TX.

That's the whole port. Everything else (LCD driver, Wi-Fi, web portal, ETA fetching) is independent of which ESP32-C6 board you use — provided the Nextion screen on the other end is the **NX4827P043-011C** (or another 480 × 272 Intelligent panel that can run the same HMI file).

---

## Hardware notes

- **Different Nextion panel?** If you use a panel with a different resolution (e.g. 800 × 480), you must redesign the HMI file in the Nextion Editor to match. The firmware-side commands stay the same.
- **Baud rate**: the Nextion is configured at 9600 by default. If you re-flash the HMI to use a higher baud (e.g. 115200), update `nexSerial.begin(9600, ...)` in [src/Nextion.cpp](src/Nextion.cpp) to match.
- **Power**: the NX4827P043-011C can draw up to ~250 mA. Power it from the board's 5 V rail (USB-fed), not from a 3.3 V regulator.
- **Logic level**: ESP32-C6 GPIOs are 3.3 V. The Nextion's UART is 3.3 V tolerant, so no level shifter is needed.
