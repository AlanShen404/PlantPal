#include <Arduino.h>
#include <DHT.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include "controller.h"
#include "cloud_certificate.h"
#include "runtime_key.h"

using namespace plantpal;
constexpr int SOIL_PIN = 34, TANK_PIN = 35, LIGHT_PIN = 32, DHT_PIN = 16;
constexpr int PUMP_PIN = 26, RED_PIN = 18, GREEN_PIN = 19, BLUE_PIN = 21;
constexpr int BUZZER_PIN = 23, RESET_PIN = 27, FAULT_PIN = 14;
constexpr uint32_t UPLOAD_MS = 20000;
// Wokwi pump output is an LED. A real pump needs a suitable driver/relay.
DHT dht(DHT_PIN, DHT22);
Controller controller;
Inputs sensors;
QueueHandle_t telemetryQueue = nullptr;
struct Telemetry {
  Inputs sensors;
  State state;
  Fault fault;
  float threshold;
  bool pump;
  uint32_t totalPulses;
};
uint32_t lastAnalog = 0, lastSoil = 0, lastDht = 0, lastLog = 0;
bool dhtValid = false, initialized = false;

// Cloud I/O can block. A separate task keeps DNS/TLS/HTTP waits out of the
// control loop. Only this task owns WiFi.
void cloudTask(void*) {
  RuntimeKey runtimeKey;
  Serial.println("CLOUD waiting: enter KEY <Write API Key> in serial input, then Enter.");
  Serial.println("CLOUD key is RAM-only; firmware will not echo it. Keep input off recordings.");
  while (!runtimeKey.ready()) {
    // This runs in the cloud task, not in the time-critical control loop.
    for (int count = 0; count < 32 && Serial.available() && !runtimeKey.ready(); ++count) {
      RuntimeKey::Result result = runtimeKey.feed(static_cast<char>(Serial.read()));
      if (result == RuntimeKey::Result::ACCEPTED)
        Serial.println("CLOUD key received (RAM only); connecting. Server acceptance is not yet confirmed.");
      else if (result == RuntimeKey::Result::REJECTED)
        Serial.println("CLOUD input rejected. Use KEY followed by one space and the alphanumeric Write API Key.");
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin("Wokwi-GUEST", ""); // Simulation network; no personal Wi-Fi password.
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  uint32_t lastAttempt = millis(), lastReconnect = millis();
  for (;;) {
    uint32_t now = millis();
    if (WiFi.status() != WL_CONNECTED) {
      if (elapsed(now, lastReconnect) >= 15000) {
        lastReconnect = now;
        WiFi.reconnect();
        Serial.println("CLOUD offline; local control continues.");
      }
    } else if (time(nullptr) > 1700000000 && elapsed(now, lastAttempt) >= UPLOAD_MS) {
      lastAttempt = now;
      Telemetry t;
      if (xQueuePeek(telemetryQueue, &t, 0) == pdPASS) {
        WiFiClientSecure client;
        client.setCACert(THINGSPEAK_ROOT_CA); // never use setInsecure()
        client.setHandshakeTimeout(5);
        HTTPClient http;
        http.setConnectTimeout(3000);
        http.setTimeout(3000);
        if (http.begin(client, "https://api.thingspeak.com/update")) {
          http.addHeader("Content-Type", "application/x-www-form-urlencoded");
          http.addHeader("THINGSPEAKAPIKEY", runtimeKey.value());
          String body;
          // Invalid sensor fields are omitted, never uploaded as plausible zeros.
          if (validInputs(t.sensors)) {
            body = "field1=" + String(t.sensors.soil, 1) +
              "&field2=" + String(t.sensors.temperature, 1) +
              "&field3=" + String(t.sensors.humidity, 1) +
              "&field4=" + String(t.sensors.light, 1) +
              "&field5=" + String(t.sensors.tank, 1) + "&";
          }
          body += "field6=" + String(t.pump ? 1 : 0) +
            "&field7=" + String(static_cast<int>(t.state)) +
            "&field8=" + String(t.threshold, 1) +
            "&status=" + String(stateName(t.state)) + "_" + faultName(t.fault) +
            "_TOTAL_PULSES_" + String(t.totalPulses);
          int code = http.POST(body);
          String result = code > 0 ? http.getString() : "";
          result.trim();
          if (code == 200 && result.toInt() > 0) {
            Serial.printf("CLOUD accepted entry=%ld\n", result.toInt());
          } else {
            Serial.printf("CLOUD upload failed HTTP=%d; local control unaffected.\n", code);
          }
          http.end();
        } else Serial.println("CLOUD HTTPS initialization failed.");
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

bool acknowledgePressed(uint32_t now) {
  static bool previousRaw = false, stable = false;
  static uint32_t changedAt = 0;
  bool raw = digitalRead(RESET_PIN) == LOW;
  if (raw != previousRaw) { previousRaw = raw; changedAt = now; }
  if (elapsed(now, changedAt) >= 40 && raw != stable) {
    stable = raw;
    return stable; // one event per press, not continuous while held
  }
  return false;
}

void setMood(int r, int g, int b) {
  analogWrite(RED_PIN, r); analogWrite(GREEN_PIN, g); analogWrite(BLUE_PIN, b);
}
void updateOutputs(uint32_t now) {
  digitalWrite(PUMP_PIN, controller.pumpOn() ? HIGH : LOW);
  switch (controller.state) {
    case State::HEALTHY: setMood(0, 255, 0); break;
    case State::THIRSTY: setMood(255, 255, 0); break;
    case State::WATERING: setMood(0, 0, 255); break;
    case State::SOAKING: setMood(128, 0, 255); break;
    case State::WARNING: setMood(255, 80, 0); break;
    case State::FAILSAFE: setMood((now / 250) % 2 ? 255 : 0, 0, 0); break;
  }
  bool sounding = (controller.state == State::WARNING && now % 2000 < 150) ||
      (controller.state == State::FAILSAFE && now % 600 < 300);
  static bool wasSounding = false;
  if (sounding != wasSounding) {
    if (sounding) tone(BUZZER_PIN, 2000); else noTone(BUZZER_PIN);
    wasSounding = sounding;
  }
}

void setup() {
  pinMode(PUMP_PIN, OUTPUT); digitalWrite(PUMP_PIN, LOW);
  pinMode(RED_PIN, OUTPUT); pinMode(GREEN_PIN, OUTPUT); pinMode(BLUE_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RESET_PIN, INPUT_PULLUP); pinMode(FAULT_PIN, INPUT_PULLUP);
  Serial.begin(115200);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  dht.begin();
  setMood(0, 0, 0);
  Serial.println("PlantPal boot: pump OFF; waiting 2 seconds for first DHT sample.");
  Serial.println("SOIL/TANK knobs: clockwise raises percent. Red button injects a sensor fault; green acknowledges it.");
  telemetryQueue = xQueueCreate(1, sizeof(Telemetry));
  if (telemetryQueue == nullptr ||
      xTaskCreatePinnedToCore(cloudTask, "cloud", 12288, nullptr, 1, nullptr, 0) != pdPASS) {
    Serial.println("CLOUD task unavailable; local control remains enabled.");
  }
}

void loop() {
  uint32_t now = millis();
  bool newSoil = false;
  if (elapsed(now, lastAnalog) >= 500) {
    lastAnalog = now;
    sensors.tank = analogRead(TANK_PIN) * (100.0f / 4095.0f);
    // Brightness is a normalized inverse ADC reading, not calibrated lux.
    sensors.light = 100 - analogRead(LIGHT_PIN) * (100.0f / 4095.0f);
  }
  uint32_t soilPeriod = controller.state == State::HEALTHY ? 2000 : 500;
  if (elapsed(now, lastSoil) >= soilPeriod) {
    lastSoil = now; newSoil = true;
    sensors.soil = analogRead(SOIL_PIN) * (100.0f / 4095.0f);
  }
  if (elapsed(now, lastDht) >= 2000) {
    lastDht = now;
    sensors.temperature = dht.readTemperature();
    sensors.humidity = dht.readHumidity();
    dhtValid = std::isfinite(sensors.temperature) && std::isfinite(sensors.humidity);
    initialized = true;
  }
  bool ack = acknowledgePressed(now);
  if (initialized) {
    now = millis(); // include the time spent reading sensors in pump deadlines
    sensors.valid = dhtValid && elapsed(now, lastDht) <= 5000 &&
        elapsed(now, lastAnalog) <= 1500 && elapsed(now, lastSoil) <= 5000 &&
        digitalRead(FAULT_PIN) != LOW;
    State before = controller.state;
    controller.tick(now, sensors, newSoil, ack);
    updateOutputs(now);
    if (before != controller.state || elapsed(now, lastLog) >= 2000) {
      lastLog = now;
      Serial.printf("t=%lu state=%s soil=%.1f T=%.1f RH=%.1f light=%.1f tank=%.1f threshold=%.1f pump=%d pulses=%u fault=%s\n",
        static_cast<unsigned long>(now), stateName(controller.state), sensors.soil,
        sensors.temperature, sensors.humidity, sensors.light, sensors.tank,
        controller.threshold, controller.pumpOn(), controller.pulses, faultName(controller.fault));
    }
    if (telemetryQueue != nullptr) {
      Telemetry t = {sensors, controller.state, controller.fault, controller.threshold,
        controller.pumpOn(), controller.totalPulses};
      xQueueOverwrite(telemetryQueue, &t);
    }
  }
  // Yield one RTOS tick; all control deadlines use millis(), not blocking waits.
  vTaskDelay(1);
}
