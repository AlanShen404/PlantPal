#pragma once
#include <cmath>
#include <cstdint>

// Pure control logic: the same code runs on ESP32 and in the desktop tests.
namespace plantpal {
enum class State : uint8_t { HEALTHY, THIRSTY, WATERING, SOAKING, WARNING, FAILSAFE };
enum class Fault : uint8_t { NONE, SENSOR, PUMP_TIMEOUT, NO_RESPONSE, PULSE_LIMIT };
struct Inputs {
  float soil = 60, temperature = 25, humidity = 60, light = 30, tank = 80;
  bool valid = true;
};
inline const char* stateName(State s) {
  switch (s) {
    case State::HEALTHY: return "HEALTHY";
    case State::THIRSTY: return "THIRSTY";
    case State::WATERING: return "WATERING";
    case State::SOAKING: return "SOAKING";
    case State::WARNING: return "WARNING";
    default: return "FAILSAFE";
  }
}
inline const char* faultName(Fault f) {
  switch (f) {
    case Fault::NONE: return "NONE";
    case Fault::SENSOR: return "SENSOR_INVALID_OR_STALE";
    case Fault::PUMP_TIMEOUT: return "PUMP_TIMEOUT";
    case Fault::NO_RESPONSE: return "NO_MOISTURE_RESPONSE";
    default: return "THREE_PULSE_LIMIT";
  }
}
inline uint32_t elapsed(uint32_t now, uint32_t then) { return now - then; }
inline bool inRange(float v, float low, float high) {
  return std::isfinite(v) && v >= low && v <= high;
}
inline bool validInputs(const Inputs& s) {
  return s.valid && inRange(s.soil, 0, 100) && inRange(s.tank, 0, 100) &&
      inRange(s.temperature, -40, 80) && inRange(s.humidity, 0, 100) &&
      inRange(s.light, 0, 100);
}
inline float adaptiveThreshold(const Inputs& s) {
  // Percentages added here are percentage points, not relative percentages.
  return 35.0f + (s.temperature > 30 ? 5 : 0) +
      (s.humidity < 40 ? 5 : 0) + (s.light > 70 ? 5 : 0);
}
class Controller {
 public:
  static constexpr uint32_t PUMP_MS = 3000;
  static constexpr uint32_t PUMP_LIMIT_MS = 3500;
  static constexpr uint32_t SOAK_MS = 20000;
  static constexpr uint32_t COOLDOWN_MS = 60000;
  State state = State::HEALTHY;
  Fault fault = Fault::NONE;
  float threshold = 35, cycleTarget = 40;
  uint8_t pulses = 0;
  uint32_t totalPulses = 0;
  bool tankWarning = false, heatWarning = false;
  bool pumpOn() const { return state == State::WATERING; }

  // newSoilReading prevents a fast main loop from counting cached samples twice.
  void tick(uint32_t now, const Inputs& s, bool newSoilReading, bool acknowledge = false) {
    if (!validInputs(s)) { trip(Fault::SENSOR); return; }
    threshold = adaptiveThreshold(s);
    if (s.tank < 20) tankWarning = true;
    else if (s.tank >= 25) tankWarning = false;
    if (s.temperature > 35 && s.soil < threshold) heatWarning = true;
    else if (s.temperature <= 33 || s.soil >= threshold + 5) heatWarning = false;

    // Faults latch until a new acknowledgement arrives under safe conditions.
    if (state == State::FAILSAFE) {
      if (acknowledge && !tankWarning && !heatWarning) {
        fault = Fault::NONE;
        pulses = dryReadings = 0;
        cool(now);
        state = s.soil >= threshold + 5 ? State::HEALTHY : State::THIRSTY;
      }
      return;
    }
    if (tankWarning || heatWarning) {
      if (state != State::WARNING) cool(now);
      state = State::WARNING; // pump OFF, even in the middle of a pulse
      dryReadings = 0;
      return;
    }
    if (state == State::WARNING) {
      pulses = dryReadings = 0;
      state = s.soil >= threshold + 5 ? State::HEALTHY : State::THIRSTY;
      return;
    }

    if (state == State::WATERING) {
      if (elapsed(now, enteredAt) >= PUMP_LIMIT_MS) { trip(Fault::PUMP_TIMEOUT); return; }
      if (elapsed(now, enteredAt) >= PUMP_MS || s.soil >= cycleTarget) {
        state = State::SOAKING;
        enteredAt = now;
      }
      return;
    }
    if (state == State::SOAKING) {
      if (elapsed(now, enteredAt) < SOAK_MS) return;
      if (s.soil >= cycleTarget) {
        cool(now);
        dryReadings = 0;
        state = s.soil >= threshold + 5 ? State::HEALTHY : State::THIRSTY;
      } else if (pulses >= 3) {
        trip(s.soil - cycleBaseline < 3 ? Fault::NO_RESPONSE : Fault::PULSE_LIMIT);
      } else {
        startPulse(now);
      }
      return;
    }

    if (s.soil >= threshold + 5) {
      state = State::HEALTHY;
      dryReadings = 0;
      return;
    }
    if (newSoilReading) {
      if (s.soil < threshold) { if (dryReadings < 2) ++dryReadings; }
      else dryReadings = 0;
    }
    if (state == State::HEALTHY && dryReadings >= 2) {
      state = State::THIRSTY; // a distinct, observable decision state
      return;
    }
    if (state == State::THIRSTY && dryReadings >= 2 &&
        (!cooling || elapsed(now, cooldownAt) >= COOLDOWN_MS)) {
      pulses = 0;
      cycleBaseline = s.soil;
      cycleTarget = threshold + 5; // freeze the target for this bounded cycle
      startPulse(now);
    }
  }

 private:
  uint8_t dryReadings = 0;
  uint32_t enteredAt = 0, cooldownAt = 0;
  float cycleBaseline = 0;
  bool cooling = false;
  void cool(uint32_t now) { cooling = true; cooldownAt = now; }
  void trip(Fault reason) { state = State::FAILSAFE; fault = reason; }
  void startPulse(uint32_t now) {
    ++pulses;
    ++totalPulses;
    state = State::WATERING;
    enteredAt = now;
  }
};
}  // namespace plantpal
