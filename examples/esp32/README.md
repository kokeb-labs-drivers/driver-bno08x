# BNO08x ESP32 examples

ESP-IDF applications that run the BNO08x driver on an ESP32-C6 over I2C, built by CI on every pull request. They
have not been run on a board here (not verified on hardware): the bench step is in the last section.

## Wiring (ESP32-C6 DevKitC-1)

| ESP32-C6 | BNO08x | Notes |
| --- | --- | --- |
| GPIO6 | 20 H_SDA | I2C0, 400 kHz; add 2 k to 4 k pull-ups to 3.3 V |
| GPIO7 | 19 H_SCL | |
| GPIO10 | 14 H_INTN | Input; the port timestamps its falling edge |
| GPIO11 | 11 NRST | Reset; when NRST is tied high use `Esp32Bno08xPortNoReset` (no reset hook) |
| GND | 5 PS1, 6 PS0 | Straps: I2C |
| GND | 17 SA0 | Address `0x4A` |
| 3V3 through 10 k | 4 BOOTN | Must be high at reset, or the bootloader starts |
| 3V3 | 3 VDD, 28 VDDIO | 100 nF each; 100 nF from pin 9 (CAP) to GND |
| GND | GND | |

## Build and flash

```bash
git submodule update --init scripts      # the ESP-IDF build scripts
. $IDF_PATH/export.sh                     # ESP-IDF v5.5
./scripts/build_app.sh list
./scripts/build_app.sh bno08x_basic_example Debug
./scripts/flash_app.sh flash_monitor bno08x_basic_example Debug
```

`idf.py menuconfig` shows "BNO08x sensor hub" under Component config: the chip, the I2C address, the buffer sizes
and the timeouts that `bno08x::KconfigDefaults()` and `bno08x::KconfigLimits` return.

## Expected output

```text
I (312) bno08x_example: BNO08x driver 0.1.0
I (480) bno08x_example: ready; enable rotation vector: Ok
I (500) bno08x_example: yaw 7187 pitch 270 roll 2256 (centidegrees), accuracy 3
```

If it stays silent, read `GetStats()` and check H_INTN first; a `Faulted` state means the start-up timed out
(power, NRST, the strap pins and BOOTN).

## Files

| File | What it is |
| --- | --- |
| [`main/bno08x_basic_example.cpp`](main/bno08x_basic_example.cpp) | The example app |
| [`main/esp32_bno08x_port.hpp`](main/esp32_bno08x_port.hpp) | The ESP-IDF port: bus, NRST, H_INTN timestamp, log |
| [`CMakeLists.txt`](CMakeLists.txt) | The ESP-IDF project; `EXTRA_COMPONENT_DIRS` points at the repository root, which is the component |
| [`app_config.yml`](app_config.yml) | The app matrix for the scripts, CI and the docs |

## Bench step

Flash the app, tilt the board by hand and confirm the angles follow within a second with accuracy rising to 3, and
that `GetStats()` shows no `seq_gap` or `bus_errors`. Until someone has done this the port is untested on silicon.
