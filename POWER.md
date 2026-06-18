# Power Management

How the device manages power, what's implemented, and the light-sleep
investigation (feasibility, hardware constraints, and why deeper sleep is
currently deferred).

## Power model (where the energy goes)

On battery, the dominant draws, largest first:

1. **Display backlight** — by far the biggest while the screen is on. Mitigated
   by the inactivity dim/off (see below).
2. **CPU active-idle** — the main loop runs continuously. At 240 MHz the SoC
   draws tens of mA even when "idle-looping".
3. **WiFi radio** — STA stays associated to poll the API every 60 s.

The screen is already turned off after inactivity, so once it's off the CPU and
WiFi idle current is what's left to optimize.

## Implemented optimizations (safe, no UX regression)

- **Inactivity dim/off** (`applyBacklight`): on battery the screen dims at
  `DIM_MS` (60 s) and turns off at `OFF_MS` (120 s). On USB it dims but never
  sleeps (`SLEEP_WHEN_CHARGING = false`) — a plugged-in desk unit stays
  glanceable. The idle timer is reset on USB plug/unplug so unplugging after a
  long idle doesn't sleep the screen instantly.
- **WiFi modem sleep** (`WiFi.setSleep(true)` in `net::connectMulti`): the radio
  naps between beacons; only adds ~DTIM receive latency. Set explicitly because
  the per-core/per-version default isn't guaranteed.
- **CPU frequency scaling** (`loop()`): the CPU runs at **80 MHz while the
  screen is asleep** and **240 MHz when awake**. `screenAsleep()` is only true on
  battery (USB never sleeps), so animations stay full-speed when the user is
  actually looking. 80 MHz is the floor that still runs WiFi.
- **No polling while asleep**: the Running loop breaks before the poll scheduler
  when the screen is off, so the API isn't polled (and the data is refreshed on
  wake only if it's older than one poll interval — see `wakeShow`).
- **TLS keep-alive** (`api`): the connection is reused between polls, so refresh
  latency drops from a full ~1–3 s handshake to ~0.3 s when the server keeps the
  connection open.

These together cut the screen-off draw meaningfully (CPU 240→80 MHz + modem
sleep) without changing how the device feels.

## Light sleep — investigation & decision

"Light sleep" pauses the CPU (RAM retained) and wakes on configured sources.
It would cut the screen-off idle current the most (roughly active-idle ~tens of
mA → a few mA). Two ways to do it on ESP32-S3:

### Wake-source feasibility (board pin map)

ESP32-S3 RTC GPIOs are **GPIO0–GPIO21**; only those can wake the chip from
sleep. From `board_pins.h`:

| Wake source              | Pin    | RTC-GPIO? | Usable as wake |
| ------------------------ | ------ | --------- | -------------- |
| Button BOOT / IO0        | GPIO0  | yes       | ✅             |
| Button IO12              | GPIO12 | yes       | ✅             |
| Button IO16              | GPIO16 | yes       | ✅             |
| Proximity IRQ (LTR-553)  | GPIO21 | yes       | ✅ (needs the sensor configured to assert INT; it's polled today) |
| Touch (CST226SE)         | —      | —         | ❌ no INT pin in the board map (only RST=GPIO13); touch is read over I2C by polling |

**Consequence:** in true light sleep the CPU can't poll I2C, and there is no
touch INT line, so **touch double-tap wake is lost**. Buttons work; proximity
("hand approaching") can work as a touch replacement, but only after configuring
the LTR-553 to drive its INT line.

### Framework limitation (the blocker)

There are two light-sleep modes:

- **Automatic light sleep** (`esp_pm_configure` with `light_sleep_enable`): the
  RTOS sleeps transparently during idle and WiFi coordinates (stays connected).
  Because the loop still polls every ~30 ms, this would *keep* touch/proximity/
  button wake working and is low-risk. **However** it requires
  `CONFIG_PM_ENABLE` + `CONFIG_FREERTOS_USE_TICKLESS_IDLE` in the SDK, and the
  **stock PlatformIO Arduino-ESP32 build for this board (qio_opi) has neither**
  (verified in `framework-arduinoespressif32/.../esp32s3/qio_opi/include/sdkconfig.h`).
  Enabling them means rebuilding the Arduino SDK, which the Arduino framework
  here can't do cleanly.
- **Manual light sleep** (`esp_light_sleep_start()`): always available, no PM/
  tickless needed. But it fully stops the loop until a hardware wake source, so:
  - **touch double-tap wake is lost** (no I2C polling while asleep);
  - **WiFi drops during the sleep** (it can't service beacons for a long,
    button-bounded sleep) → wake must reconnect (full TLS handshake, defeating
    keep-alive), and the API isn't polled while asleep (data is stale until
    wake — already handled by `wakeShow`, but the wake is slower);
  - **proximity wake** needs the LTR-553 INT configured;
  - hard to validate without a current meter, and a mis-configured wake source
    can leave the device unable to wake (needs a reset).

### Decision

Automatic (safe, touch-preserving, WiFi-keeping) light sleep is **not available**
in this framework. The only option is **manual light sleep**, whose costs are
substantial: lost touch wake, WiFi drop + reconnect on every wake, stale data
while asleep, extra proximity-INT work, and risky-to-validate behavior — for a
device that is frequently on USB (where power doesn't matter).

**Status: deferred.** The implemented optimizations (CPU 80 MHz on screen-off +
WiFi modem sleep) capture most of the practical benefit at zero UX cost. Manual
light sleep can be revisited if extended battery runtime becomes a priority and
the trade-offs (especially losing touch wake and WiFi-reconnect-on-wake) are
acceptable, ideally with a current-measurement setup to validate the gain.

To pursue it later:
1. Configure the LTR-553 to assert its INT (GPIO21) on a proximity event.
2. `esp_sleep_enable_gpio_wakeup()` + `gpio_wakeup_enable()` on GPIO0/12/16 (LOW,
   buttons are active-low with pull-ups) and GPIO21.
3. Replace the asleep busy-loop with `esp_light_sleep_start()`; on wake, restore
   peripherals and reconnect WiFi.
4. Measure idle current before/after; verify every wake source and WiFi recovery.
