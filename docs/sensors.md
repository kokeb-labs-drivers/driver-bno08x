# Sensors

Every sensor report ID that a CEVA document defines has a typed event. The chip itself implements a subset; the
rest is described in the reference manual only and is allowed but may never answer (A31).

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    - Configuration and the commands list: CEVA 1000-3927 1.17, 1.3.3 BNO08X Configuration, [p. 24](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf#page=24)
    - Sensor reports, timestamps and batching: CEVA 1000-3927 1.17, 1.3.5 Sensor Reports, [p. 26](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf#page=26)
    - Motion, orientation and environmental outputs: CEVA 1000-3927 1.17, 2 Sensor data processing, [p. 30](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf#page=30)
    - Report rates and latency: CEVA 1000-3927 1.17, 6.9 Report Rates, [p. 50](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf#page=50)
    - Feature reports, rate and batch: CEVA 1000-3625 1.9, 5.4 Feature Reports, [p. 34](https://www.ceva-ip.com/wp-content/uploads/SH-2-Reference-Manual.pdf#page=35)
    - Sensor report common fields and status: CEVA 1000-3625 1.9, 6.5.1 Common Fields, [p. 62](https://www.ceva-ip.com/wp-content/uploads/SH-2-Reference-Manual.pdf#page=63)
    - Common dynamic feature report: CEVA 1000-3625 1.9, 6.5.2 Common Dynamic Feature Report, [p. 63](https://www.ceva-ip.com/wp-content/uploads/SH-2-Reference-Manual.pdf#page=64)
    - Batching and batch timestamps: CEVA 1000-3625 1.9, 7 Batching, [p. 91](https://www.ceva-ip.com/wp-content/uploads/SH-2-Reference-Manual.pdf#page=92)
    - SH-2 channel usage and report lengths: CEVA 1000-3600 1.6, 2.2 SH-2 Channel Usage, [p. 4](https://www.ceva-ip.com/wp-content/uploads/SH-2-SHTP-Reference-Manual.pdf#page=5)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## Turn a sensor on and read it

`SetFeature(id, FeatureConfig)` enables a report at `interval_us`; zero turns it off. The call returns an `OpId` and
the sample events arrive through `PopEvent()`. With `Config::set_feature_confirm` the operation finishes when the
hub's own feature response arrives, otherwise when the request has been written. A sensor the hub cannot serve
(the environmental sensors without their parts, A20) never produces samples; use `GetFeature()` to see what the hub
actually runs.

```cpp
--8<-- "tests/docs/api_tasks.cpp:sensors"
```

`FeatureConfig` also carries `batch_us` (the hub collects samples and delivers them in groups), `change_enable`
and the change sensitivity (`relative` selects relative or absolute), `wake` (the sensor wakes the host) and the
sensor-specific word. `Flush(id)` makes the hub deliver what it holds; `FlushDone` says it did. `Feature(id)` returns
the last configuration the hub confirmed and `Latest(id)` the last sample.

## What an event holds

An `Event` of kind `Sensor` has the `sensor`, its `accuracy`, the host time `t_us`, the report's own sequence number
and `v[]`, the report's fields as signed 32-bit integers in the report's own unit. The Q point is the position of
the binary point: a Q8 value is the integer divided by 256. The helpers `ToMmPerS2`, `ToMilliRadPerS`, `ToNanoTesla`,
`ToMicroRad` and `ToPpm` give whole units with integer arithmetic, and `ToEulerCentiDeg()` turns a rotation vector
into yaw, pitch and roll in centidegrees ([Ambiguities](ambiguities.md) says how accurate it is).

Sample times follow the hub's own delay fields: the host time of the interrupt that announced the cargo, minus the
delay the cargo reports (base timestamp plus the report's delay). A port that implements `TakeIntTimestamp()` gives
the exact edge time; without it the time of the `Poll()` that saw H_INTN is used, which is coarser. A base
timestamp that signals overflow is counted (`base_overflow`) and the interrupt time is used (A17).

| ID | Sensor | `v[]` | Unit (Q) |
| --- | --- | --- | --- |
| 0x01 | Accelerometer | x, y, z | m/s2 (Q8) |
| 0x02 | Gyroscope, calibrated | x, y, z | rad/s (Q9) |
| 0x03 | Magnetic field, calibrated | x, y, z | uT (Q4) |
| 0x04 | Linear acceleration | x, y, z | m/s2 (Q8) |
| 0x05 | Rotation vector | i, j, k, real, accuracy | unit (Q14), rad (Q12) |
| 0x06 | Gravity | x, y, z | m/s2 (Q8) |
| 0x07 | Gyroscope, uncalibrated | x, y, z, bias x, y, z | rad/s (Q9) |
| 0x08 | Game rotation vector | i, j, k, real | unit (Q14) |
| 0x09 | Geomagnetic rotation vector | i, j, k, real, accuracy | unit (Q14), rad (Q12) |
| 0x0A | Pressure | value | hPa (Q20) |
| 0x0B | Ambient light | value | lux (Q8) |
| 0x0C | Humidity | value | percent (Q8) |
| 0x0D | Proximity | value | cm (Q4) |
| 0x0E | Temperature | value | degrees C (Q7) |
| 0x0F | Magnetic field, uncalibrated | x, y, z, bias x, y, z | uT (Q4) |
| 0x10 | Tap detector | flags | bits |
| 0x11 | Step counter | latency, steps | 100 us ticks, count (16 bit, A18) |
| 0x12 | Significant motion | flag | event |
| 0x13 | Stability classifier | class | enumeration |
| 0x14 | Raw accelerometer | x, y, z, hub timestamp | ADC counts, us |
| 0x15 | Raw gyroscope | x, y, z, temperature, hub timestamp | ADC counts, us |
| 0x16 | Raw magnetometer | x, y, z, hub timestamp | ADC counts, us |
| 0x18 | Step detector | latency | 100 us ticks |
| 0x19 | Shake detector | axis bits | bits |
| 0x1A | Flip detector | flag | event (manual only) |
| 0x1B | Pickup detector | flag | event (manual only) |
| 0x1C | Stability detector | flag | event |
| 0x1E | Personal activity classifier | page, most likely, 10 confidences | enumeration, percent |
| 0x1F | Sleep detector | state | enumeration (manual only) |
| 0x20 to 0x22 | Tilt, pocket and circle detectors | flag | event (manual only) |
| 0x23 | Heart rate monitor | rate | beats per minute (manual only) |
| 0x28 | AR/VR rotation vector | i, j, k, real, accuracy | unit (Q14), rad (Q12) |
| 0x29 | AR/VR game rotation vector | i, j, k, real | unit (Q14) |
| 0x2A | Gyro-integrated rotation vector | i, j, k, real, rate x, y, z | quaternion Q14; rates as in the manual (channel 5, no report ID) |
| 0x2B | Motion request | intent, request | BNO086 interactive calibration |
| 0x2C | Optical flow | documented fields only | manual only; several fields undocumented |
| 0x2D | Dead reckoning pose | hub timestamp and 13 words | Q15 and others, manual only |
| 0x2E | Wheel encoder input | none | named by one library only, no layout; never decoded |

The reports of 0x2C and 0x2D, and the detectors marked manual only, are implemented from the reference manual and
have never been seen from a chip; see [Ambiguities](ambiguities.md).

## Variants and the report set

The data sheet lists the reports the hubs implement (marked "D" in the working notes); the manual adds more.
`Config::strict_raw` refuses a raw sensor while its base sensor is off. An environmental sensor needs the parts on
the hub's secondary I2C bus (a pressure sensor, a light and proximity sensor), which are optional on the board.

## Rates and latency

Report intervals are in microseconds. The hub rounds to what its sensors can do and answers with the rate it chose;
read it with `GetFeature()`. Batching trades latency for fewer interrupts, and `Poll()` should run at least once
per tenth of the fastest report period, because the hub gives up on a host that answers H_INTN much later than
that (A38).

Next: [Calibration](calibration.md).
