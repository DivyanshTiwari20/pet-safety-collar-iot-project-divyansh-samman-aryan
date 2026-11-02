#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <DHT.h>

// ---------- EDIT THESE ----------
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";
#define BOT_TOKEN "YOUR_BOT_TOKEN"
#define CHAT_ID   "YOUR_CHAT_ID"

const float HOT_C = 35.0;               // too hot
const float HOT_HUMID_C = 32.0;         // hot + humid combo
const float HUMID_WITH_HEAT = 70.0;
const float COLD_C = 10.0;              // too cold
const float HUMID_MAX = 80.0;           // too humid
const unsigned long INACTIVE_SEC = 20;  // demo: 20 sec. Real use: 7200 (2 hours)
const float SHAKE_G = 2.0;              // sudden hit or fall (in g)
const float MOVE_DELTA = 0.10;          // smaller = more sensitive movement detection
const unsigned long COOLDOWN_MS = 60000; // same alert repeats only after 1 minute
// --------------------------------

#define DHTPIN D5
DHT dht(DHTPIN, DHT11);
const int MPU = 0x68;

WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);

float temp = NAN, hum = NAN, accG = 1.0, prevG = 1.0;
bool mpuOk = false;
unsigned long lastMove = 0, lastDht = 0, lastMpu = 0, lastBot = 0, lastWifi = 0;
unsigned long lastAlert[5] = {0, 0, 0, 0, 0};  // 0 hot, 1 cold, 2 humid, 3 inactive, 4 shake

int16_t rd16() {
  uint8_t h = Wire.read();
  uint8_t l = Wire.read();
  return (h << 8) | l;
}

bool readMPU() {
  Wire.beginTransmission(MPU);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)MPU, (uint8_t)6) != 6) return false;
  float ax = rd16() / 16384.0;
  float ay = rd16() / 16384.0;
  float az = rd16() / 16384.0;
  accG = sqrt(ax * ax + ay * ay + az * az);
  return true;
}

void sendAlert(int type, String msg) {
  unsigned long now = millis();
  if (lastAlert[type] != 0 && now - lastAlert[type] < COOLDOWN_MS) return;
  lastAlert[type] = now;
  bot.sendMessage(CHAT_ID, msg, "");
  Serial.println("Alert sent: " + msg);
}

String statusText() {
  String s = "Collar status\n";
  s += isnan(temp) ? "Temperature: no reading\n" : "Temperature: " + String(temp, 1) + " C\n";
  s += isnan(hum) ? "Humidity: no reading\n" : "Humidity: " + String(hum, 0) + " %\n";
  unsigned long still = (millis() - lastMove) / 1000;
  s += "Last movement: " + String(still) + " sec ago\n";
  s += mpuOk ? "Motion sensor: OK" : "Motion sensor: NOT FOUND";
  return s;
}

void checkAlerts() {
  if (!isnan(temp)) {
    if (temp >= HOT_C) {
      sendAlert(0, "ALERT: Too hot! " + String(temp, 1) + " C. Please check on your dog.");
    } else if (temp >= HOT_HUMID_C && !isnan(hum) && hum >= HUMID_WITH_HEAT) {