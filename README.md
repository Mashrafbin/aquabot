# Boat Firmware — Setup Guide (PlatformIO)

## 1. Project layout

```
boat_firmware_pio/
├── platformio.ini      <- board + library config, already set up
├── include/config.h    <- CONFIGURE: WiFi, server IP, pins
└── src/main.cpp         <- the firmware itself
```

Libraries are declared in `platformio.ini` under `lib_deps` — PlatformIO downloads them automatically on first build, no manual library manager steps needed.

```ini
lib_deps =
    bblanchon/ArduinoJson @ ^6.21.5
    mikalhart/TinyGPSPlus @ ^1.0.3
    rfetick/MPU6050_light @ ^1.1.0
    mprograms/QMC5883LCompass @ ^1.1.1
```

## 2. Board selection

`platformio.ini` defaults to `board = esp32dev` (generic ESP32 dev module — works for most ESP32 DevKit boards). If your board is something more specific (ESP32-S3, Wemos D1 Mini32, etc.), change the `board` line — run `pio boards espressif32` (CLI) or check the PlatformIO IDE board list to find the exact ID for yours.

## 3. Build and upload

CLI:
```
pio run                  # build
pio run --target upload  # build + flash
pio device monitor       # serial monitor (115200 baud, matches platformio.ini)
```

Or in VS Code with the PlatformIO extension: use the checkmark (Build), right-arrow (Upload), and plug (Monitor) icons in the status bar.

## 4. Edit `include/config.h` before flashing

- `WIFI_SSID` / `WIFI_PASSWORD` — your local network
- `SERVER_HOST` — the local IP of the machine running `boat-server` (find with `hostname -I` on the Pi/PC)
- Pin numbers — verify against your actual wiring; the defaults assume the pin layout described in the comments

## 5. Compass chip — verify before relying on headings

Most GY-273 modules sold today (including many labeled "HMC5883L") actually contain the **QMC5883L** chip. The firmware assumes QMC5883L. To check yours:

1. Flash a simple I2C scanner sketch (search "ESP32 I2C scanner" — same idea as the one used earlier for the MPU6050 on ESP8266, just with `Wire.begin(21, 22)`)
2. Look for the address:
   - `0x0D` → QMC5883L (firmware is already correct)
   - `0x1E` → genuine HMC5883L (swap `QMC5883LCompass` for an HMC5883L-specific library, e.g. `DFRobot_QMC5883` isn't it — use `Adafruit_HMC5883_Unified`)

## 6. Calibration you need to do before trusting readings

**Compass** — metal in the boat hull, the motors, and the battery all distort the magnetic field. Do a figure-8 calibration routine (`QMC5883LCompass` has a `calibrate()` helper) with the compass mounted in its final position on the boat, motors powered on, to get accurate headings.

**pH sensor** — the placeholder formula in `readPH()` is a generic starting point, not accurate for your specific probe. Calibrate with pH 4.0 and pH 7.0 buffer solution:
1. Dip the probe in pH 7.0 buffer, note the voltage (print `raw`/`voltage` over Serial)
2. Dip in pH 4.0 buffer, note the voltage
3. Solve the line: `slope = (4.0 - 7.0) / (v_ph4 - v_ph7)`, `offset = 7.0 - slope * v_ph7`
4. Replace the `-5.70` and `21.34` constants in `readPH()`

**Battery voltage divider** — the code assumes a 100kΩ/56kΩ divider. If you use different resistor values, update `BATTERY_DIVIDER_RATIO` in `config.h` accordingly, and double check the ADC pin never sees more than 3.3V even at full 8.4V charge — a wiring mistake here can damage the ESP32's ADC input.

**Thermistor** — the default constants (10kΩ series resistor, 10kΩ nominal resistance at 25°C, B-coefficient 3435) match a common generic NTC thermistor. Check your specific thermistor's datasheet for its actual nominal resistance and B-coefficient values, and update `THERMISTOR_NOMINAL_RESISTANCE` / `THERMISTOR_BCOEFFICIENT` in `config.h` if they differ — using the wrong constants won't break anything, but the temperature readings will be off.

## 7. How mission mode works

The firmware polls `GET /api/mission` every 2 seconds. When it sees a new mission (`status: "assigned"`), it starts navigating from waypoint 0 and reports back `status: "running"`.

- **`mode: "once"`** — on reaching the last waypoint, the boat stops and reports `status: "completed"`.
- **`mode: "ping-pong"`** — on reaching the last waypoint, the boat reverses direction and heads back through the waypoints in reverse order, then forward again, repeating indefinitely. It keeps doing this until the dashboard's **Abort mission** button sets `status: "aborted"`, which the firmware checks every poll and responds to by stopping immediately.

## 8. Steering tuning

`STEERING_KP` in `config.h` controls how aggressively the boat turns to correct heading error — too low and it won't track waypoints tightly, too high and it'll oscillate/zigzag. Start at the default (2.2) and adjust based on how it behaves on the water. `BASE_SPEED` and `MIN_TURN_SPEED` control overall speed and how much the inner motor slows during a turn.

## 9. Known simplification, worth knowing

This uses simple proportional steering (heading error → differential motor speed), not a full PID controller — it should be perfectly adequate for a slow patrol boat, but if you find it oscillating around the target heading on the water, adding a derivative term (damping) is the natural next step. Happy to add that once you've tested the basic version.
