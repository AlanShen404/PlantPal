#include "../controller.h"
#include <cassert>
#include <iostream>
#include <limits>
using namespace plantpal;
static void start(Controller& c, Inputs& s, uint32_t t = 0) {
  s.soil = 25;
  c.tick(t, s, true);
  assert(!c.pumpOn());
  c.tick(t + 500, s, true);
  assert(c.state == State::THIRSTY);
  c.tick(t + 501, s, false);
  assert(c.pumpOn());
}
int main() {
  int checks = 0;
  {
    Inputs s;
    for (int mask = 0; mask < 8; ++mask) {
      s.temperature = mask & 1 ? 31 : 30;
      s.humidity = mask & 2 ? 39 : 40;
      s.light = mask & 4 ? 71 : 70;
      const int n = !!(mask & 1) + !!(mask & 2) + !!(mask & 4);
      assert(adaptiveThreshold(s) == 35 + 5 * n);
    }
    ++checks;
  }
  {
    Controller c; Inputs s; s.soil = 25;
    c.tick(0, s, true);
    for (int i = 1; i < 100; ++i) c.tick(i, s, false);
    assert(c.state == State::HEALTHY); // cached samples must not confirm dryness
    s.soil = 36; c.tick(100, s, true);
    s.soil = 25; c.tick(200, s, true);
    assert(c.state == State::HEALTHY);
    c.tick(300, s, true); assert(c.state == State::THIRSTY);
    ++checks;
  }
  {
    Controller c; Inputs s; start(c, s);
    c.tick(3500, s, true); assert(c.pumpOn());
    c.tick(3501, s, true); assert(c.state == State::SOAKING);
    s.soil = 45;
    c.tick(23500, s, true); assert(c.state == State::SOAKING);
    c.tick(23501, s, true); assert(c.state == State::HEALTHY);
    s.soil = 25;
    c.tick(24001, s, true); c.tick(24501, s, true);
    c.tick(83500, s, true); assert(!c.pumpOn());
    c.tick(83501, s, true); assert(c.pumpOn());
    ++checks;
  }
  {
    Controller c; Inputs s; start(c, s);
    s.tank = 19; c.tick(600, s, true);
    assert(c.state == State::WARNING && !c.pumpOn());
    s.tank = 24; c.tick(700, s, true); assert(c.state == State::WARNING);
    s.tank = 25; c.tick(800, s, true); assert(c.state == State::THIRSTY);
    c.tick(900, s, true); c.tick(1000, s, true); assert(!c.pumpOn());
    c.tick(60600, s, true); assert(c.pumpOn());
    ++checks;
  }
  {
    Controller c; Inputs s; start(c, s);
    s.temperature = 36; c.tick(600, s, true);
    assert(c.state == State::WARNING && c.heatWarning);
    s.temperature = 34; c.tick(700, s, true); assert(c.state == State::WARNING);
    s.temperature = 33; c.tick(800, s, true); assert(c.state == State::THIRSTY);
    ++checks;
  }
  {
    Controller c; Inputs s; start(c, s);
    s.valid = false; c.tick(700, s, true, true);
    assert(c.fault == Fault::SENSOR && !c.pumpOn());
    s.valid = true; c.tick(800, s, true); assert(c.state == State::FAILSAFE);
    s.tank = 10; c.tick(900, s, true, true); assert(c.state == State::FAILSAFE);
    s.tank = 80; c.tick(1000, s, true, true); assert(c.state == State::THIRSTY);
    ++checks;
  }
  {
    Controller c; Inputs s; start(c, s);
    s.temperature = std::numeric_limits<float>::quiet_NaN();
    c.tick(700, s, true); assert(c.fault == Fault::SENSOR);
    ++checks;
  }
  {
    Controller c; Inputs s; start(c, s);
    c.tick(4001, s, true); assert(c.fault == Fault::PUMP_TIMEOUT && !c.pumpOn());
    ++checks;
  }
  for (bool someResponse : {false, true}) {
    Controller c; Inputs s; start(c, s);
    uint32_t t = 501;
    for (int pulse = 1; pulse <= 3; ++pulse) {
      t += 3000; c.tick(t, s, true); assert(c.state == State::SOAKING);
      if (someResponse) s.soil += 1.5f;
      t += 20000; c.tick(t, s, true);
      if (pulse < 3) assert(c.pumpOn());
    }
    assert(c.state == State::FAILSAFE && c.totalPulses == 3);
    assert(c.fault == (someResponse ? Fault::PULSE_LIMIT : Fault::NO_RESPONSE));
    c.tick(t + 100000, s, true); assert(!c.pumpOn());
    ++checks;
  }
  {
    Controller c; Inputs s; s.soil = 38;
    c.tick(0, s, true); assert(c.state == State::HEALTHY);
    start(c, s); s.soil = 40;
    c.tick(600, s, true); assert(c.state == State::SOAKING && !c.pumpOn());
    ++checks;
  }
  {
    Controller c; Inputs s; uint32_t t = UINT32_MAX - 2000;
    start(c, s, t);
    c.tick(t + 501 + 3000, s, true); assert(c.state == State::SOAKING);
    s.soil = 60;
    c.tick(t + 501 + 23000, s, true); assert(c.state == State::HEALTHY);
    ++checks;
  }
  std::cout << "PASS: " << checks << " control scenario groups\n";
}
