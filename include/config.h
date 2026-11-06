#ifndef CONFIG_H
#define CONFIG_H

// ============================================================
// CONFIGURE: WiFi + server
// ============================================================
#define WIFI_SSID     "Z@ne"
#define WIFI_PASSWORD "z@ne!98!"

// The local IP address of the machine running boat-server (your PC/Pi).
// Find it with `hostname -I` (Pi/Linux) or `ipconfig` (Windows).
#define SERVER_HOST   "192.168.50.229"
#define SERVER_PORT   3000

// ============================================================
// CONFIGURE: Pin mapping — verify against your actual wiring
// ============================================================

// I2C bus (MPU6050 + compass share this)
#define PIN_SDA 21
#define PIN_SCL 22

// GPS on hardware Serial2 (NEO-7M)
#define GPS_RX_PIN 16   // ESP32 RX2 <- GPS TX
#define GPS_TX_PIN 17   // ESP32 TX2 -> GPS RX
#define GPS_BAUD   9600

// AT8833 (DRV8833-style) dual H-bridge motor driver.
// Each motor uses two pins; PWM one side high while the other is low
// to set direction, with PWM duty cycle controlling speed.
#define MOTOR_L_IN1 27
#define MOTOR_L_IN2 26
#define MOTOR_R_IN1 25
#define MOTOR_R_IN2 33

#define PWM_FREQ      5000
#define PWM_RESOLUTION 8      // 0-255
#define PWM_CH_L1 0
#define PWM_CH_L2 1
#define PWM_CH_R1 2
#define PWM_CH_R2 3

// pH sensor analog output -> ESP32 ADC (input-only pin, good for analog)
#define PIN_PH_SENSOR 34

// Battery voltage sense via a resistor divider (REQUIRED — do not wire
// the raw 2S battery, up to 8.4V, directly into the ESP32 ADC, which
// tops out at ~3.3V). Example divider: R1=100k (battery+ side) to the
// ADC pin, R2=56k from the ADC pin to GND. That gives ~3.02V at the ADC
// when the battery is at 8.4V full charge — adjust BATTERY_DIVIDER_RATIO
// below to match whatever resistors you actually use.
#define PIN_BATTERY_SENSE 35
#define BATTERY_DIVIDER_RATIO ((100.0 + 56.0) / 56.0)  // (R1+R2)/R2
#define BATTERY_MIN_V 6.0   // 2S Li-ion empty (3.0V/cell)
#define BATTERY_MAX_V 8.4   // 2S Li-ion full  (4.2V/cell)

// Temperature — analog NTC thermistor (voltage divider + Steinhart-Hart).
// Uses a different ADC1 pin than the pH sensor (34) and battery sense (35).
#define PIN_THERMISTOR 32
#define THERMISTOR_SERIES_RESISTOR    10000.0
#define THERMISTOR_NOMINAL_RESISTANCE 10000.0  // check your thermistor's datasheet
#define THERMISTOR_NOMINAL_TEMP       25.0
#define THERMISTOR_BCOEFFICIENT       3435.0
#define ADC_MAX                       4095.0   // ESP32 ADC is 12-bit

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
