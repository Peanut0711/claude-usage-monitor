# Claude Usage Monitor — LilyGo T-Display S3 Pro

Firmware that polls the Anthropic API rate-limit headers and shows your Claude
usage (5-hour / 7-day windows), reset countdowns, WiFi signal, and battery on a
2.33" IPS display.

> **Status: Stage 1** — display bring-up only. The ST7796 panel is initialised
> through LovyanGFX and a self-test screen (title + a colored utilization bar)
> is drawn to confirm the hardware works. API polling, WiFi provisioning, and
> the full UI come in later stages.

## Hardware

| Part         | Detail                                                       |
|--------------|--------------------------------------------------------------|
| Board        | LilyGo T-Display S3 Pro (K231 / XY231020, no camera)         |
| MCU          | ESP32-S3R8 — 16 MB Flash, 8 MB OPI (octal) PSRAM             |
| Display      | 2.33" IPS TFT, 222×480, ST7796 driver, SPI                  |
| Touch        | CST226SE (I²C) — *not used yet*                              |
| Light sensor | LTR-553ALS-01 (I²C) — *not used yet*                         |
| PMU          | SY6970, 470 mAh battery — *not used yet*                     |

GPIO pin map lives in [`src/board_pins.h`](src/board_pins.h) and is taken
verbatim from the official LilyGo repo
([`utilities.h`](https://github.com/Xinyuan-LilyGO/T-Display-S3-Pro)) — no pins
are guessed.

## Project layout

```
platformio.ini                       # env: tdisplay-s3-pro (board/PSRAM/lib_deps)
src/
  main.cpp                           # setup(): init display, draw test screen
  board_pins.h                       # verified GPIO map
  display/
    LGFX_TDisplayS3Pro.hpp           # LovyanGFX device (bus/panel/backlight)
    DisplayHAL.h / DisplayHAL.cpp    # drawing layer (LVGL-ready seam)
```

The drawing code is deliberately split: `LGFX_TDisplayS3Pro.hpp` is the raw
LovyanGFX device, and `DisplayHAL` is the only thing UI code talks to. A future
LVGL layer can reuse the same device via `display::gfx()`.

## Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or the VS Code extension)
- A USB-C cable

Install the CLI if needed:

```bash
pip install -U platformio
```

## Build

```bash
pio run -e tdisplay-s3-pro
```

The first build downloads the ESP32 platform and LovyanGFX, so it takes a while.

## Upload

Connect the board over USB-C, then:

```bash
pio run -e tdisplay-s3-pro -t upload
```

If the port isn't auto-detected, list ports with `pio device list` and pass it
explicitly: `pio run -e tdisplay-s3-pro -t upload --upload-port COM5`.

> **Flashing trouble?** Hold **Boot**, tap **Reset**, release **Boot** to force
> the bootloader, then re-run the upload.

## Serial monitor

```bash
pio device monitor -e tdisplay-s3-pro
```

Baud is 115200. On success you should see:

```
[Claude Usage Monitor] boot
[display] init OK
[display] test screen drawn
```

…and the screen shows **"Claude Usage Monitor"** with a coral progress bar at
65%.

## Build + upload + monitor in one go

```bash
pio run -e tdisplay-s3-pro -t upload -t monitor
```

## Notes

- `board_build.arduino.memory_type = qio_opi` matches the ESP32-S3R8's QIO
  flash + **octal** PSRAM. Using the wrong PSRAM mode causes boot crashes.
- If the image is shifted left/right, adjust `offset_x` (currently 49) in
  `src/display/LGFX_TDisplayS3Pro.hpp`.
