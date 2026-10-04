# PlantPal Smart Plant Care System

A simulation-only ESP32 project for **3707ICT Automation and IoT**. PlantPal combines a breadboard circuit, local rule-based control, safety interlocks, and ThingSpeak telemetry.

- [Saved Wokwi simulation](https://wokwi.com/projects/476867926100075521)
- [GitHub repository](https://github.com/AlanShen404/PlantPal)
- [Validation and limitations](docs/verification.md)
- [Screenshot evidence](docs/evidence/README.md)

## Scope

Two potentiometers represent soil moisture and tank level. A DHT22 provides simulated temperature and humidity, and a light sensor module provides relative light level. An RGB LED displays operating state, a blue LED represents the pump, and a buzzer signals warnings or faults. Pushbuttons inject and acknowledge a fault.

**There is no physical pump or plant.** The operator manually increases the soil input to simulate moisture absorption. The project demonstrates feedback-based control logic, not measured plant growth or water savings. Its thresholds are teaching parameters, not horticultural recommendations.

## Run in Wokwi

1. Open the saved simulation above and start it. Alternatively, create an ESP32 Arduino project and copy `sketch.ino`, `controller.h`, `runtime_key.h`, `cloud_certificate.h`, `diagram.json`, and `libraries.txt` into it.
2. Wait for the first valid DHT sample, approximately two seconds after startup.
3. Set tank level to about 80% and soil moisture to about 25%. With default environmental inputs, the watering threshold is 35%.
4. After two independent low soil readings, observe `THIRSTY`, then `WATERING`. A normal pump pulse lasts three seconds.
5. During `SOAKING`, turn the soil potentiometer up to about 60%. After the 20-second soak interval, the system returns to `HEALTHY` if its moisture target is met.
6. Test low tank level and fault injection separately. The green acknowledgement button must remain pressed long enough to pass its 40 ms debounce.

Local control runs without a cloud key. `CLOUD waiting` is expected until a key is supplied.

## Control rules

The watering threshold starts at 35%. Each of these conditions adds **5 percentage points**: temperature above 30 degrees Celsius, humidity below 40%, and relative light above 70%. The maximum resulting threshold is 50%. This is deterministic rule-based intelligence, not a trained machine-learning model.

| State | Behaviour |
| --- | --- |
| `HEALTHY` | Monitor inputs with the pump off; startup may remain here while low readings are being confirmed. |
| `THIRSTY` | Confirmed dryness; wait for any required cooldown before starting a pulse. |
| `WATERING` | Pump indicator on; stop at three seconds or when the fixed cycle target is reached. |
| `SOAKING` | Pump off; wait 20 seconds before evaluating moisture feedback. |
| `WARNING` | Pump off while a low-tank or high-temperature/dry-soil warning is active. |
| `FAILSAFE` | Pump off and fault latched until valid, warning-free inputs and a new manual acknowledgement are present. |

The cycle target is the threshold plus 5 percentage points, captured at the start of a watering cycle. A cycle allows at most three pulses. If the target remains unmet after the third soak, the controller locks out with either `NO_MOISTURE_RESPONSE` (less than 3 percentage points of improvement) or `THREE_PULSE_LIMIT`.

Tank warning enters below 20% and clears at 25% or above. Heat warning enters above 35 degrees Celsius when soil is below the current threshold; it clears at 33 degrees Celsius or below, or when soil reaches threshold plus 5. Warnings override normal watering. Invalid sensor input triggers a latched fault. A 60-second cooldown applies after successful cycles, warning entry, and fault acknowledgement. See `controller.h` for exact priority and recovery logic.

## ThingSpeak setup

Create a channel with eight enabled fields:

| Field | Value |
| --- | --- |
| 1 | Soil moisture (%) |
| 2 | Temperature (degrees Celsius) |
| 3 | Air humidity (%RH) |
| 4 | Relative light level (%; not lux) |
| 5 | Tank level (%) |
| 6 | Pump status: 0 = off, 1 = on |
| 7 | State: 0 = HEALTHY, 1 = THIRSTY, 2 = WATERING, 3 = SOAKING, 4 = WARNING, 5 = FAILSAFE |
| 8 | Adaptive watering threshold (%) |

In the serial input, type `KEY ` followed by your channel's Write API Key, then send a newline. **Do not put the real key in source files, screenshots, recordings, or this repository.** The key is held in RAM only and must be supplied again after a restart. The firmware does not echo it, but the input box and clipboard remain sensitive.

`CLOUD key received` confirms input parsing only. `CLOUD accepted entry=...` with a positive entry ID confirms that ThingSpeak accepted an upload. Restart the simulation to replace an incorrect key.

A separate task attempts HTTPS uploads at 20-second intervals when connected and time-synchronised. It validates the server certificate with the public root certificate in `cloud_certificate.h`. That certificate is public material, not a private key. There is no fallback to unencrypted HTTP. Uploads include state, fault, and cumulative pulse information in the status text.

The dashboard contains periodic snapshots and can miss a three-second pump pulse. Use serial timestamps to assess pump timing. Disconnected history is not fully buffered; reconnection uploads the latest snapshot.

## Repository files

| File or folder | Purpose |
| --- | --- |
| `sketch.ino` | Sensor acquisition, outputs, logging, and cloud task |
| `controller.h` | Shared state machine and control rules |
| `runtime_key.h` | Runtime credential input parser |
| `cloud_certificate.h` | Public CA certificate for HTTPS validation |
| `diagram.json` | Wokwi circuit and breadboard wiring |
| `libraries.txt` | Wokwi library dependencies |
| `platformio.ini`, `wokwi.toml` | Optional local build and Wokwi VS Code configuration |
| `tests/` | Desktop logic, credential-parser, and circuit checks |
| `docs/` | English validation notes and screenshot evidence |
| `secrets.example.h` | Empty legacy template; not read by the current firmware |

Reports, student IDs, build outputs, and real credentials are intentionally excluded from this upload package. The original local project remains separate.

## Local checks

With a C++ compiler and Python 3 installed, run from the repository root:

```sh
mkdir -p build
c++ -std=c++11 -Wall -Wextra -Werror tests/controller_test.cpp -o build/controller_tests
./build/controller_tests
c++ -std=c++11 -Wall -Wextra -Werror tests/runtime_key_test.cpp -o build/runtime_key_tests
./build/runtime_key_tests
python3 tests/check_circuit.py
```

With PlatformIO installed, `pio run` builds the ESP32 firmware. Wokwi's browser simulation does not require a local PlatformIO installation. Desktop checks do not replace end-to-end network or simulation testing.

## Current evidence and remaining work

Recorded evidence supports normal pulse/soak timing, operator-simulated moisture recovery, low-water interruption, three-pulse fault lockout, adaptive threshold behaviour, and a successful ThingSpeak upload with eight displayed fields. Screenshots and serial observations are limited demonstrations, not proof of long-term reliability. Network outage recovery, incorrect-key server rejection, and protection latency during active network requests still need dedicated end-to-end measurement. See the validation notes for provenance and limits.

## Technical references

- [Wokwi ESP32 WiFi networking](https://docs.wokwi.com/guides/esp32-wifi)
- [Wokwi serial monitor](https://docs.wokwi.com/guides/serial-monitor)
- [ThingSpeak write-data API](https://www.mathworks.com/help/thingspeak/writedata.html)
- [ThingSpeak channel data control](https://www.mathworks.com/help/thingspeak/channel-control.html)
