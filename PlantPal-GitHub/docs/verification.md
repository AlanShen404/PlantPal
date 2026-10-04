# Validation Record

This English summary distinguishes desktop checks from Wokwi observations and user-provided evidence. Dates refer to the October 3-4, 2026 development sessions. The project is simulation-only.

## Recorded development checks

- ESP32 cross-compilation passed using PlatformIO espressif32 6.10.0, Arduino ESP32 2.0.17, DHT 1.4.6, and Adafruit Unified Sensor 1.1.15. The recorded runtime-key build used 47,288 bytes of RAM and 940,825 bytes of flash. These are historical build results, not a fresh build of every subsequent revision.
- The desktop controller test passed 12 scenario groups, including environment threshold combinations, independent low samples, pulse and soak timing, cooldown, hysteresis, fault recovery, invalid input, retry limits, and timer wraparound.
- Runtime-key tests passed input rejection, length boundaries, fragmented input, CRLF, and one-key-per-boot behaviour.
- The circuit check passed supply/ground connectivity, breadboard strip connectivity, GPIO separation, LED resistor paths, and the DHT pull-up. This is a structural check, not an analogue electrical model.

## User-provided simulation evidence

These observations were supplied by the user during testing. They were not independently rerun for this documentation update.

| Scenario | Recorded result | Limit |
| --- | --- | --- |
| Normal watering and recovery | `WATERING` at t=4011 ms; `SOAKING` at t=7011 ms; `HEALTHY` at t=27011 ms. Soil changed from 24.8% to 63.0%; threshold was 50%. | The operator changed the soil input. No physical absorption was measured. |
| Adaptive threshold | At 31.1 degrees Celsius, 39.0% RH, and 75.0% relative light, the threshold was 50%, compared with 35% under default conditions. | Individual environmental changes were covered by desktop logic checks, not separately captured in the supplied screenshots. |
| Low-water interruption | Within one reported run, `WATERING` at t=4011 ms with tank=83.4%; `WARNING` and pump=0 at t=6896 ms with tank=11.4%. | A 2,885 ms pulse shows early interruption. The exact input-change time is unknown, so response latency cannot be calculated. |
| No moisture response | At t=103011 ms and t=175011 ms, `FAILSAFE`, pump=0, pulses=3, and `NO_MOISTURE_RESPONSE` remained present. | The observations show continued lockout over 72 seconds, not indefinite reliability. |
| Cloud telemetry | The user supplied `CLOUD accepted entry=1` and screenshots of all eight ThingSpeak fields. | No raw CSV-to-serial timestamp reconciliation or long-duration upload test was performed. |

Earlier development observations also recorded warning hysteresis and a sensor-injection fault that remained latched until manual acknowledgement. Not every observation has a separate screenshot in this repository.

## Not yet established

- End-to-end protection latency while a network request is active.
- Network outage/recovery behaviour and incorrect-key rejection by the server.
- Long-term upload stability, complete historical buffering, or guaranteed delivery.
- Physical plant growth, water savings, physical pump operation, or real sensor calibration.

The source includes protective logic, but implementation alone is not experimental proof. The report and demonstration should keep these limits explicit.
