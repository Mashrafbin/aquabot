#ifndef CONFIG_H
#define CONFIG_H

// ============================================================
// CONFIGURE: WiFi + server
// ============================================================
#define WIFI_SSID     "YOUR_WIFI_NAME"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// The local IP address of the machine running boat-server (your PC/Pi,
// or your laptop's hotspot IP at the venue — see earlier notes on that).
#define SERVER_HOST   "192.168.1.42"
#define SERVER_PORT   3000

// ============================================================
// CONFIGURE: Pin mapping for ESP32-S3-...-N16R8 (16MB flash / 8MB
// Octal PSRAM). This is NOT the same safe pin set as a plain ESP32 —
// several pins used on the old board don't exist or mean something
// different here. Reserved/avoided on this specific chip variant:
//   GPIO 26-32  -> always reserved for flash SPI on every S3 module
//   GPIO 33-37  -> additionally reserved for Octal PSRAM (R8 variants only)
//   GPIO 19-20  -> USB D-/D+ (native USB, don't repurpose)
//   GPIO 0,3,45,46 -> strapping pins, risky to drive externally at boot
//   GPIO 22-25  -> don't physically exist on the S3
// Everything below is chosen from what's left, verified against your
// module's datasheet is still worth doing once you have the board in hand.
// ============================================================

// I2C bus (MPU6050 + compass share this)
#define PIN_SDA 8
#define PIN_SCL 9

// GPS on hardware Serial2 (NEO-7M)
#define GPS_RX_PIN 18   // ESP32-S3 RX2 <- GPS TX
#define GPS_TX_PIN 17   // ESP32-S3 TX2 -> GPS RX
#define GPS_BAUD   9600

// AT8833 (DRV8833-style) dual H-bridge motor driver.
// Each motor uses two pins; PWM one side high while the other is low
// to set direction, with PWM duty cycle controlling speed.
#define MOTOR_L_IN1 15
#define MOTOR_L_IN2 16
#define MOTOR_R_IN1 38
#define MOTOR_R_IN2 39

#define PWM_FREQ      5000
#define PWM_RESOLUTION 8      // 0-255
#define PWM_CH_L1 0
#define PWM_CH_L2 1
#define PWM_CH_R1 2
#define PWM_CH_R2 3

// Analog sensors — kept deliberately within GPIO1-10 (ADC1). The S3's
// ADC2 pins (GPIO11-20) can get blocked or return bad readings while
// WiFi is active, since the WiFi driver uses ADC2 internally at times.
// Since this firmware is on WiFi continuously, ADC1 is the safe choice
// for anything you need to read reliably.
#define PIN_PH_SENSOR 4
#define PIN_THERMISTOR 5
#define PIN_BATTERY_SENSE 6

// Battery voltage sense via a resistor divider (REQUIRED — do not wire
// the raw 2S battery, up to 8.4V, directly into an ADC pin, which tops
// out at ~3.3V). Example divider: R1=100k (battery+ side) to the ADC
// pin, R2=56k from the ADC pin to GND. That gives ~3.02V at the ADC
// when the battery is at 8.4V full charge — adjust the ratio below to
// match whatever resistors you actually use.
#define BATTERY_DIVIDER_RATIO ((100.0 + 56.0) / 56.0)  // (R1+R2)/R2
#define BATTERY_MIN_V 6.0   // 2S Li-ion empty (3.0V/cell)
#define BATTERY_MAX_V 8.4   // 2S Li-ion full  (4.2V/cell)

// Thermistor (NTC + series resistor voltage divider), Steinhart-Hart.
#define THERMISTOR_SERIES_RESISTOR    10000.0
#define THERMISTOR_NOMINAL_RESISTANCE 10000.0  // check your thermistor's datasheet
#define THERMISTOR_NOMINAL_TEMP       25.0
#define THERMISTOR_BCOEFFICIENT       3435.0
#define ADC_MAX                       4095.0   // 12-bit ADC

// ============================================================
// Tunable navigation parameters
// ============================================================
#define TELEMETRY_INTERVAL_MS   1000   // how often to POST telemetry
#define MISSION_POLL_INTERVAL_MS 2000  // how often to GET /api/mission

#define WAYPOINT_REACH_RADIUS_M 3.0    // how close counts as "arrived"
#define STEERING_KP             2.2    // proportional steering gain
#define BASE_SPEED              170    // forward PWM speed, 0-255
#define MIN_TURN_SPEED          90     // PWM floor while turning sharply
#define MAX_PWM                 255

#endif
