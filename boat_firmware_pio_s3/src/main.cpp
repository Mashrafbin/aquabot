// ============================================================
// Autonomous Fish-Farm Patrol Boat — ESP32-S3-N16R8 firmware
//
// - Polls the boat-server for the current mission (waypoints + mode)
// - Navigates waypoint-to-waypoint using GPS position + compass heading
// - Supports two mission modes:
//     "once"      -> follow waypoints in order, then stop
//     "ping-pong" -> follow forward, then reverse back through them,
//                    repeating indefinitely until the mission is aborted
// - Sends telemetry (position, heading, pH, temperature, battery) back
//   to the server every second
//
// Libraries required (declared in platformio.ini, auto-installed):
//   ArduinoJson, TinyGPSPlus, MPU6050_light, QMC5883LCompass
//
// Board note: targets ESP32-S3-...-N16R8 (16MB flash / 8MB Octal PSRAM).
// All pin numbers live in config.h and are chosen to avoid this chip's
// reserved flash/PSRAM/USB/strapping pins — see the comments there.
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <TinyGPSPlus.h>
#include <MPU6050_light.h>
#include <QMC5883LCompass.h>

#include "config.h"

// ---- Globals ----
HardwareSerial gpsSerial(2);
TinyGPSPlus gps;
MPU6050 mpu(Wire);
QMC5883LCompass compass;

struct Waypoint { double lat; double lng; };
Waypoint missionWaypoints[30];
int missionWaypointCount = 0;
String missionMode = "once";       // "once" | "ping-pong"
String missionStatus = "idle";     // mirrors the server's status field
int currentWaypointIndex = 0;
int waypointDirection = 1;         // +1 forward through the list, -1 reverse
bool navigating = false;

unsigned long lastTelemetryAt = 0;
unsigned long lastMissionPollAt = 0;

// PlatformIO compiles .cpp files as plain C++ (unlike Arduino IDE's .ino
// handling, which auto-generates these) — so every function needs to be
// forward-declared before it's used above its own definition.
void connectWiFi();
String serverUrl(const char* path);
void pollMission();
void reportMissionStatus(const char* status);
void navigateStep();
void advanceWaypoint();
void driveTowardHeadingError(double errorDeg);
double normalizeAngle(double angle);
double haversineDistanceM(double lat1, double lon1, double lat2, double lon2);
double bearingDegrees(double lat1, double lon1, double lat2, double lon2);
void setupMotorPWM();
void setMotor(int chanForward, int chanReverse, int speed);
void stopMotors();
double readHeading();
float readPH();
int readAveragedThermistorADC();
float readTemperatureC();
float readBatteryPercent();
void sendTelemetry();

// ============================================================
// Setup
// ============================================================
void setup() {
  Serial.begin(115200);

  Wire.begin(PIN_SDA, PIN_SCL);

  mpu.begin();
  Serial.println("Calibrating MPU6050, keep the boat still...");
  mpu.calcOffsets(true, true);

  compass.init();
  // If your boat consistently reads a heading offset from true north,
  // set a calibration/declination correction here once you've bench
  // tested it, e.g. compass.setCalibration(...) or add a fixed offset
  // in readHeading() below.

  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  analogReadResolution(12);       // explicit, though 12-bit is default
  analogSetAttenuation(ADC_11db); // allows full 0-3.3V range (default is ~2.6V max otherwise)

  setupMotorPWM();
  stopMotors();

  connectWiFi();
}

// ============================================================
// Main loop
// ============================================================
void loop() {
  // Keep GPS parser fed continuously — TinyGPS++ parses NMEA sentences
  // as bytes arrive, so this needs to run every loop iteration.
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  unsigned long now = millis();

  if (now - lastMissionPollAt >= MISSION_POLL_INTERVAL_MS) {
    lastMissionPollAt = now;
    pollMission();
  }

  if (missionStatus == "aborted") {
    stopMotors();
    navigating = false;
  } else if (navigating && missionWaypointCount > 0) {
    navigateStep();
  } else {
    stopMotors();
  }

  if (now - lastTelemetryAt >= TELEMETRY_INTERVAL_MS) {
    lastTelemetryAt = now;
    sendTelemetry();
  }
}

// ============================================================
// WiFi
// ============================================================
void connectWiFi() {
  Serial.printf("Connecting to WiFi \"%s\"...\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(300);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi connection failed — will retry.");
  }
}

String serverUrl(const char* path) {
  return "http://" + String(SERVER_HOST) + ":" + String(SERVER_PORT) + path;
}

// ============================================================
// Mission polling — pulls waypoints + mode from the dashboard/server
// ============================================================
void pollMission() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(serverUrl("/api/mission"));
  int code = http.GET();
  if (code != 200) {
    http.end();
    return;
  }

  String body = http.getString();
  http.end();

  // Sized generously for up to ~30 waypoints; increase if you plan
  // longer missions. This board's ample RAM/PSRAM gives plenty of
  // headroom if you need to raise this further.
  StaticJsonDocument<4096> doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.println("Mission JSON parse failed: " + String(err.c_str()));
    return;
  }

  String newStatus = doc["status"] | "idle";
  String newMode = doc["mode"] | "once";
  JsonArray wps = doc["waypoints"].as<JsonArray>();

  bool isNewAssignment = (newStatus == "assigned" && missionStatus != "assigned" && missionStatus != "running");

  missionStatus = newStatus;
  missionMode = newMode;

  missionWaypointCount = 0;
  for (JsonObject wp : wps) {
    if (missionWaypointCount >= 30) break;
    missionWaypoints[missionWaypointCount].lat = wp["lat"] | 0.0;
    missionWaypoints[missionWaypointCount].lng = wp["lng"] | 0.0;
    missionWaypointCount++;
  }

  if (missionStatus == "aborted") {
    navigating = false;
    return;
  }

  if (isNewAssignment && missionWaypointCount > 0) {
    currentWaypointIndex = 0;
    waypointDirection = 1;
    navigating = true;
    reportMissionStatus("running");
  } else if (missionStatus == "running" && missionWaypointCount > 0) {
    navigating = true;
  }
}

void reportMissionStatus(const char* status) {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.begin(serverUrl("/api/mission/status"));
  http.addHeader("Content-Type", "application/json");
  String payload = String("{\"status\":\"") + status + "\"}";
  http.POST(payload);
  http.end();
  missionStatus = status;
}

// ============================================================
// Navigation — drive toward the current waypoint; advance/bounce
// through the list depending on mission mode
// ============================================================
void navigateStep() {
  if (!gps.location.isValid() || !gps.location.isUpdated()) {
    // No GPS fix yet — hold position rather than drive blind.
    stopMotors();
    return;
  }

  double curLat = gps.location.lat();
  double curLng = gps.location.lng();
  Waypoint target = missionWaypoints[currentWaypointIndex];

  double distance = haversineDistanceM(curLat, curLng, target.lat, target.lng);
  double bearingToTarget = bearingDegrees(curLat, curLng, target.lat, target.lng);

  if (distance <= WAYPOINT_REACH_RADIUS_M) {
    advanceWaypoint();
    return;
  }

  double heading = readHeading();
  double error = normalizeAngle(bearingToTarget - heading);
  driveTowardHeadingError(error);
}

void advanceWaypoint() {
  currentWaypointIndex += waypointDirection;

  bool pastEnd = currentWaypointIndex >= missionWaypointCount;
  bool beforeStart = currentWaypointIndex < 0;

  if (pastEnd || beforeStart) {
    if (missionMode == "ping-pong") {
      // Bounce: reverse direction and step back onto the last valid
      // index, so we head back the way we came instead of stopping.
      waypointDirection *= -1;
      currentWaypointIndex += waypointDirection;
      currentWaypointIndex = constrain(currentWaypointIndex, 0, missionWaypointCount - 1);
      // Mission stays "running" — this is the loop, not a completion.
    } else {
      navigating = false;
      stopMotors();
      reportMissionStatus("completed");
    }
  }
}

// ---- Steering: proportional control on heading error, differential drive ----
void driveTowardHeadingError(double errorDeg) {
  // errorDeg in [-180, 180]; positive = target is to the right, so
  // speed up the left motor / slow the right motor to turn right.
  int turn = (int)(STEERING_KP * errorDeg);
  turn = constrain(turn, -BASE_SPEED, BASE_SPEED);

  int leftSpeed = BASE_SPEED + turn;
  int rightSpeed = BASE_SPEED - turn;

  leftSpeed = constrain(leftSpeed, MIN_TURN_SPEED, MAX_PWM);
  rightSpeed = constrain(rightSpeed, MIN_TURN_SPEED, MAX_PWM);

  setMotor(PWM_CH_L1, PWM_CH_L2, leftSpeed);
  setMotor(PWM_CH_R1, PWM_CH_R2, rightSpeed);
}

double normalizeAngle(double angle) {
  while (angle > 180) angle -= 360;
  while (angle < -180) angle += 360;
  return angle;
}

double haversineDistanceM(double lat1, double lon1, double lat2, double lon2) {
  const double R = 6371000.0; // Earth radius, meters
  double dLat = radians(lat2 - lat1);
  double dLon = radians(lon2 - lon1);
  double a = sin(dLat / 2) * sin(dLat / 2) +
             cos(radians(lat1)) * cos(radians(lat2)) * sin(dLon / 2) * sin(dLon / 2);
  double c = 2 * atan2(sqrt(a), sqrt(1 - a));
  return R * c;
}

double bearingDegrees(double lat1, double lon1, double lat2, double lon2) {
  double dLon = radians(lon2 - lon1);
  double y = sin(dLon) * cos(radians(lat2));
  double x = cos(radians(lat1)) * sin(radians(lat2)) -
             sin(radians(lat1)) * cos(radians(lat2)) * cos(dLon);
  double brng = degrees(atan2(y, x));
  return fmod((brng + 360.0), 360.0);
}

// ============================================================
// Motor control (AT8833 / DRV8833-style dual H-bridge)
// ============================================================
void setupMotorPWM() {
  ledcSetup(PWM_CH_L1, PWM_FREQ, PWM_RESOLUTION);
  ledcSetup(PWM_CH_L2, PWM_FREQ, PWM_RESOLUTION);
  ledcSetup(PWM_CH_R1, PWM_FREQ, PWM_RESOLUTION);
  ledcSetup(PWM_CH_R2, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(MOTOR_L_IN1, PWM_CH_L1);
  ledcAttachPin(MOTOR_L_IN2, PWM_CH_L2);
  ledcAttachPin(MOTOR_R_IN1, PWM_CH_R1);
  ledcAttachPin(MOTOR_R_IN2, PWM_CH_R2);
}

// speed: -255..255. Positive = forward. Drives one IN pin with PWM
// and holds the other low, which is the standard DRV8833 speed+direction
// pattern (fast-decay mode).
void setMotor(int chanForward, int chanReverse, int speed) {
  speed = constrain(speed, -MAX_PWM, MAX_PWM);
  if (speed >= 0) {
    ledcWrite(chanForward, speed);
    ledcWrite(chanReverse, 0);
  } else {
    ledcWrite(chanForward, 0);
    ledcWrite(chanReverse, -speed);
  }
}

void stopMotors() {
  ledcWrite(PWM_CH_L1, 0);
  ledcWrite(PWM_CH_L2, 0);
  ledcWrite(PWM_CH_R1, 0);
  ledcWrite(PWM_CH_R2, 0);
}

// ============================================================
// Sensors
// ============================================================
double readHeading() {
  compass.read();
  int a = compass.getAzimuth(); // 0-359, 0 = north
  return (double)a;
}

float readPH() {
  int raw = analogRead(PIN_PH_SENSOR);
  float voltage = (raw / 4095.0) * 3.3;
  // Single-point calibration (probe shorted -> ~pH 7 reference) plus a
  // generic slope. Replace both constants once you have real buffer
  // solution for a proper 2-point calibration — see project notes.
  float ph = -5.70 * voltage + 21.34;
  return ph;
}

int readAveragedThermistorADC() {
  long sum = 0;
  const int samples = 20;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(PIN_THERMISTOR);
    delay(2);
  }
  return sum / samples;
}

float readTemperatureC() {
  int adcValue = readAveragedThermistorADC();
  if (adcValue <= 0) return -127.0; // avoid divide-by-zero if disconnected

  float resistance = THERMISTOR_SERIES_RESISTOR * ((ADC_MAX / adcValue) - 1.0);

  float steinhart = resistance / THERMISTOR_NOMINAL_RESISTANCE;
  steinhart = log(steinhart);
  steinhart /= THERMISTOR_BCOEFFICIENT;
  steinhart += 1.0 / (THERMISTOR_NOMINAL_TEMP + 273.15);
  steinhart = 1.0 / steinhart;
  steinhart -= 273.15;

  return steinhart;
}

float readBatteryPercent() {
  int raw = analogRead(PIN_BATTERY_SENSE);
  float adcVoltage = (raw / 4095.0) * 3.3;
  float batteryVoltage = adcVoltage * BATTERY_DIVIDER_RATIO;
  float pct = (batteryVoltage - BATTERY_MIN_V) / (BATTERY_MAX_V - BATTERY_MIN_V) * 100.0;
  return constrain(pct, 0.0, 100.0);
}

// ============================================================
// Telemetry
// ============================================================
void sendTelemetry() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!gps.location.isValid()) return; // don't send garbage (0,0) before first fix

  StaticJsonDocument<512> doc;
  doc["lat"] = gps.location.lat();
  doc["lng"] = gps.location.lng();
  doc["heading"] = readHeading();
  doc["speed"] = gps.speed.isValid() ? gps.speed.mps() : 0.0;
  doc["ph"] = readPH();
  doc["temperature"] = readTemperatureC();
  doc["battery"] = readBatteryPercent();

  String payload;
  serializeJson(doc, payload);

  HTTPClient http;
  http.begin(serverUrl("/api/telemetry"));
  http.addHeader("Content-Type", "application/json");
  http.POST(payload);
  http.end();
}
