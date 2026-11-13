# Quickstart

Run the driver on your PC first, with no hardware; then read the rotation vector from your MCU: wire the chip,
write a small port class, call `Begin()` and `Poll()`.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From CEVA 1000-3927 1.17:

    - Getting started: establishing contact and reading: 5 Getting started with BNO08X, [p. 43](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf#page=43)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## Try it on your PC

[`examples/host/`](../examples/host/README.md) runs the real driver against a model of the hub, on every
transport. From the repository root, with CMake and a C++20 compiler:

```bash
cmake -S examples/host -B build-example
cmake --build build-example
./build-example/bno08x_example_i2c_quickstart
```

```text
enable rotation vector: ok
yaw 71.87 pitch 2.70 roll 22.56 deg (accuracy 3)
```

The model produces arbitrary data, so your angles differ. The calls are the ones below; only the port differs on an
MCU. CI builds and runs every example.

## 1. Wire it

For I2C at address `0x4A`, with the hub as a slave on your MCU's I2C bus:

| MCU | BNO08x pin | Notes |
| --- | --- | --- |
| I2C SCL | 19 H_SCL | Pull-up to VDDIO |
| I2C SDA | 20 H_SDA | Pull-up to VDDIO |
| GPIO input | 14 H_INTN | Active low: a message is waiting. Required for I2C and SPI |
| GPIO output | 11 NRST | Optional but recommended: the driver resets the hub with it |
| GND | 5 PS1, 6 PS0, 17 SA0 | Strap pins, sampled at reset: I2C, address `0x4A` |
| 3.3 V | 3 VDD, 28 VDDIO | 100 nF each; 100 nF on pin 9 (CAP) to GND |
| 3.3 V through 10 k | 4 BOOTN | A low level at reset starts the bootloader |

The [Hardware](hardware.md) page has the full pin list, the other three transports and the reference circuits.

## 2. Add the driver

Add the repository as a submodule and link the `bno08x::bno08x` CMake target. [Installation](installation.md)
covers CMake on any MCU, the installed package and ESP-IDF.

```bash
git submodule add https://github.com/kokeb-labs-drivers/driver-bno08x.git external/driver-bno08x
```

## 3. Write the port

The port is the only platform code: it brings the bus up, moves bytes, reads H_INTN and gives the time. Replace the
`platform_*` calls with your SDK's.

```cpp title="my_port.hpp"
--8<-- "tests/docs/my_port.hpp:port"
```

[Porting](porting.md) explains each hook, the optional ones and the other transports.

## 4. Run it

```cpp title="main.cpp"
--8<-- "tests/docs/quickstart.cpp:quickstart"
```

What happens:

1. `Begin()` checks the configuration and starts the session. It returns at once.
2. Each `Poll()` does a little: it pulses NRST, waits for the hub's advertisement and its start-up messages, and
   then reads whatever the hub has queued. `Ready()` turns true when the hub is up.
3. `SetFeature()` asks for a report; the hub then sends it at the interval you gave.
4. Reports arrive as `EventKind::Sensor` events with the values in the report's own units, a timestamp and the
   accuracy. `ToEulerCentiDeg()` turns a rotation vector into angles with integer arithmetic.

## If it does not work

| Symptom | First thing to check |
| --- | --- |
| `Ready()` never turns true; state `Faulted` | NRST, power and H_INTN ([Troubleshooting](troubleshooting.md)) |
| `Begin()` returns `InvalidArgument` | A `Config` field is out of range; `Config::Valid()` says |
| `SetFeature()` returns `Variant` | You asked for a feature the configured chip lacks |
| No sensor events | The interval, and that you call `Poll()` often enough |

## Next

- [API guide](api-guide.md): operations, events, errors.
- [Sensors](sensors.md): every report and its units.
- [Configuration](configuration.md): every `Config` field, buffer limits, Kconfig.
- [Examples](examples.md): the host examples and the MCU example.
