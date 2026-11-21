# Figures

<!-- Written by kokeb-docs figures from docs/diagrams/figures/*.py. Do not edit by hand. -->

Every figure on this site is drawn by a Python module from facts extracted from the code, so it is rebuilt,
not redrawn, when the code changes. CI runs `kokeb-docs figures --check` and fails when a
committed figure no longer matches the code. Each figure below lists the files it is drawn from.

## Inside the BNO08x

<!-- figure: chip-blocks -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-blocks.dark.svg">
  <img src="chip-blocks.svg"
    alt="Block diagram of the BNO08x: its function blocks, how they connect and which package pins reach each
    block.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/bno08x_interface.hpp`](../../inc/bno08x_interface.hpp)

## What the BNO08x does

<!-- figure: chip-capabilities -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-capabilities.dark.svg">
  <img src="chip-capabilities.svg"
    alt="Capability map of the BNO08x: Orientation (supported): Rotation vector, game, geomagnetic, AR/VR
    stabilised and gyro-integrated quaternions, with accuracy; Motion outputs (supported): Acceleration,
    linear acceleration, gravity, angular rate and magnetic field, calibrated or raw; Activity and
    gestures (supported): Step counter, tap, shake, stability, significant motion and activity
    classifier as events; Rates, batching and timestamps (supported): A report rate per sensor, batched
    reports flushed on request, each with its own timestamp; Calibration (supported): Dynamic
    calibration on or off per sensor, saved to flash on request or periodically; Tare and reorientation
    (supported): Zeroes the heading or the full orientation, or rotates every output to the way the
    board is mounted; Configuration records (supported): Reads, writes and erases the records in the
    hub's flash, and each sensor's metadata; Reset, sleep and identity (supported): Resets the hub by
    pin or command, puts it to sleep and wakes it, and reads the product ID; Firmware update (not
    covered): The bootloader that writes new hub firmware; the driver never starts it.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/bno08x_interface.hpp`](../../inc/bno08x_interface.hpp)

## BNO08x: I2C

<!-- figure: chip-circuit-i2c -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-circuit-i2c.dark.svg">
  <img src="chip-circuit-i2c.svg"
    alt="Schematic of the BNO08x i2c: the chip with U2 MCU, C1 capacitor 100 nF, C2 capacitor 100 nF, C3
    capacitor 100 nF, R1 resistor 2.2 kΩ, R2 resistor 2.2 kΩ, R3 resistor 10 kΩ, R4 resistor 4.7 kΩ, R5
    resistor 4.7 kΩ, Y1 crystal 32.768 kHz; the MCU wires carry the port hooks IntAsserted(),
    TakeIntTimestamp(), SetBootn(), SetReset(), SetWake(), transport hooks.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/bno08x_interface.hpp`](../../inc/bno08x_interface.hpp)

## BNO08x: SPI

<!-- figure: chip-circuit-spi -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-circuit-spi.dark.svg">
  <img src="chip-circuit-spi.svg"
    alt="Schematic of the BNO08x spi: the chip with U2 MCU, C1 capacitor 100 nF, C2 capacitor 100 nF, C3
    capacitor 100 nF, R3 resistor 10 kΩ, R4 resistor 4.7 kΩ, R5 resistor 4.7 kΩ, Y1 crystal 32.768 kHz;
    the MCU wires carry the port hooks IntAsserted(), TakeIntTimestamp(), SetBootn(), SetReset(),
    SetWake(), SpiTransfer(), transport hooks.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/bno08x_interface.hpp`](../../inc/bno08x_interface.hpp)

## BNO08x: UART-RVC

<!-- figure: chip-circuit-uart-rvc -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-circuit-uart-rvc.dark.svg">
  <img src="chip-circuit-uart-rvc.svg"
    alt="Schematic of the BNO08x uart-rvc: the chip with U2 MCU, C1 capacitor 100 nF, C2 capacitor 100 nF, C3
    capacitor 100 nF, R3 resistor 10 kΩ, R4 resistor 4.7 kΩ, R5 resistor 4.7 kΩ, Y1 crystal 32.768 kHz;
    the MCU wires carry the port hooks SetBootn(), SetReset(), SetWake(), transport hooks.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/bno08x_interface.hpp`](../../inc/bno08x_interface.hpp)

## BNO08x: UART-SHTP

<!-- figure: chip-circuit-uart-shtp -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-circuit-uart-shtp.dark.svg">
  <img src="chip-circuit-uart-shtp.svg"
    alt="Schematic of the BNO08x uart-shtp: the chip with U2 MCU, C1 capacitor 100 nF, C2 capacitor 100 nF,
    C3 capacitor 100 nF, R3 resistor 10 kΩ, R4 resistor 4.7 kΩ, R5 resistor 4.7 kΩ, Y1 crystal 32.768
    kHz; the MCU wires carry the port hooks IntAsserted(), TakeIntTimestamp(), SetBootn(), SetReset(),
    SetWake(), transport hooks.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/bno08x_interface.hpp`](../../inc/bno08x_interface.hpp)

## BNO08x

<!-- figure: chip-hero -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-hero.dark.svg">
  <img src="chip-hero.svg"
    alt="Overview of the BNO08x, a 9-axis sensor hub in one package: an accelerometer, a gyroscope and a
    magnetometer with a Cortex-M0+ that runs the sensor fusion and reports finished orientation, motion
    and activity results to the host. A MCU talks to it over I2C, SPI or UART; it drives H_INTN line; it
    reads back Motion and magnetic field. Headline figures: 9 axes accelerometer, gyroscope,
    magnetometer; 1000 Hz gyro rotation vector; 400 Hz rotation vector; 3.7 ms rotation vector latency
    at 200 Hz; 3 MHz SPI; I2C 400 kHz, UART 3 Mbit/s; 2.4 to 3.6 V VDD; VDDIO 1.7 to 3.6 V; 5.2 x 3.8 mm
    LGA-28 body, 1.1 mm high.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/bno08x_interface.hpp`](../../inc/bno08x_interface.hpp)

## BNO08x package, LGA-28

<!-- figure: chip-outline -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-outline.dark.svg">
  <img src="chip-outline.svg"
    alt="Outline of the BNO08x in LGA-28 drawn to scale from above: body 5.2 x 3.8 x 1.1 mm, 28 pins.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/bno08x_interface.hpp`](../../inc/bno08x_interface.hpp)

## BNO08x pinout, LGA-28

<!-- figure: chip-pinout -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-pinout.dark.svg">
  <img src="chip-pinout.svg"
    alt="Pinout of the BNO08x in LGA-28, top view: 28 pins, each with its name, its electrical type as a
    colour and the driver hook that uses it.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/bno08x_interface.hpp`](../../inc/bno08x_interface.hpp)

## Wiring: I2C

<!-- figure: chip-wiring-i2c -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-wiring-i2c.dark.svg">
  <img src="chip-wiring-i2c.svg"
    alt="Wiring of the BNO08x for the I2C: MCU I2C SCL to H_SCL/SCK/RX (transport hooks); MCU I2C SDA to
    H_SDA/H_MISO/TX (transport hooks); MCU GPIO (IRQ) to H_INTN (IntAsserted(), TakeIntTimestamp()); MCU
    GPIO to NRST (SetReset()); MCU GPIO to BOOTN (SetBootn()). Supplies, grounds and the support parts
    are listed under the drawing.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/bno08x_interface.hpp`](../../inc/bno08x_interface.hpp)

## Wiring: SPI

<!-- figure: chip-wiring-spi -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-wiring-spi.dark.svg">
  <img src="chip-wiring-spi.svg"
    alt="Wiring of the BNO08x for the SPI: MCU SPI SCK to H_SCL/SCK/RX (transport hooks); MCU SPI MOSI to
    SA0/H_MOSI (transport hooks); MCU SPI MISO to H_SDA/H_MISO/TX (transport hooks); MCU GPIO / CS to
    H_CSN (SpiTransfer()); MCU GPIO to PS0/WAKE (SetWake()); MCU GPIO (IRQ) to H_INTN (IntAsserted(),
    TakeIntTimestamp()); MCU GPIO to NRST (SetReset()); MCU GPIO to BOOTN (SetBootn()). Supplies,
    grounds and the support parts are listed under the drawing.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/bno08x_interface.hpp`](../../inc/bno08x_interface.hpp)

## Wiring: UART-RVC

<!-- figure: chip-wiring-uart-rvc -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-wiring-uart-rvc.dark.svg">
  <img src="chip-wiring-uart-rvc.svg"
    alt="Wiring of the BNO08x for the UART-RVC: MCU UART RX to H_SDA/H_MISO/TX (transport hooks); MCU GPIO
    (high at reset) to PS0/WAKE (SetWake()); MCU GPIO to NRST (SetReset()); MCU GPIO to BOOTN
    (SetBootn()). Supplies, grounds and the support parts are listed under the drawing.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/bno08x_interface.hpp`](../../inc/bno08x_interface.hpp)

## Wiring: UART-SHTP

<!-- figure: chip-wiring-uart-shtp -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-wiring-uart-shtp.dark.svg">
  <img src="chip-wiring-uart-shtp.svg"
    alt="Wiring of the BNO08x for the UART-SHTP: MCU UART TX to H_SCL/SCK/RX (transport hooks); MCU UART RX
    to H_SDA/H_MISO/TX (transport hooks); MCU GPIO (IRQ) to H_INTN (IntAsserted(), TakeIntTimestamp());
    MCU GPIO to NRST (SetReset()); MCU GPIO to BOOTN (SetBootn()). Supplies, grounds and the support
    parts are listed under the drawing.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/bno08x_interface.hpp`](../../inc/bno08x_interface.hpp)

## The layers, one of them yours

<!-- figure: driver-layers -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="driver-layers.dark.svg">
  <img src="driver-layers.svg"
    alt="Layer diagram: your application calls the Bno08x core, which sends register reads and writes through
    the I2C, SPI or UART port contract to the port class you write, which drives the chip; the core and
    the contract are the same on every MCU.">
</picture>
<!-- /figure -->

Figure module: [`docs/diagrams/figures/driver-layers.py`](../../docs/diagrams/figures/driver-layers.py)

Drawn from:

- `public calls` in [`inc/bno08x.hpp`](../../inc/bno08x.hpp)
- `HubInterface` in [`inc/bno08x_interface.hpp`](../../inc/bno08x_interface.hpp)

## I2C timing of a read

<!-- figure: i2c-timing -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="i2c-timing.dark.svg">
  <img src="i2c-timing.svg"
    alt="I2C timing of a BNO08x read: H_INTN low asks for attention, the host sends a start, the address with
    the read bit and gets the acknowledge, H_INTN rises after the address, and the read ends with a NAK
    and a stop.">
</picture>
<!-- /figure -->

Hardware description: [`docs/model/i2c-timing.timing.yml`](../model/i2c-timing.timing.yml)

Drawn from:

- [`docs/model/i2c-timing.timing.yml`](../model/i2c-timing.timing.yml)

## Two BNO08x on one I2C bus

<!-- figure: i2c-topology -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="i2c-topology.dark.svg">
  <img src="i2c-topology.svg"
    alt="An I2C bus on which an MCU and two BNO08x hubs share SDA and SCL, one at address 0x4A with SA0 low
    and one at 0x4B with SA0 high, with one pull-up resistor per line.">
</picture>
<!-- /figure -->

Hardware description: [`docs/model/i2c-topology.topology.yml`](../model/i2c-topology.topology.yml)

Drawn from:

- [`docs/model/i2c-topology.topology.yml`](../model/i2c-topology.topology.yml)

## One sample through the layers

<!-- figure: one-sample-through-the-layers -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="one-sample-through-the-layers.dark.svg">
  <img src="one-sample-through-the-layers.svg"
    alt="One sensor sample: your code enables a feature, the core writes it through your port, the hub pulls
    its interrupt line low when data is ready, and the next Poll reads, reassembles and decodes the
    report into an event.">
</picture>
<!-- /figure -->

Hardware description: [`docs/model/one-sample-through-the-layers.seq.yml`](../model/one-sample-through-the-layers.seq.yml)

Drawn from:

- [`docs/model/one-sample-through-the-layers.seq.yml`](../model/one-sample-through-the-layers.seq.yml)

## Session states of the driver

<!-- figure: session-states -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="session-states.dark.svg">
  <img src="session-states.svg"
    alt="State machine of the BNO08x session with the states Idle, Resetting, WaitAdvert, WaitReady, Ready
    and Faulted, and the calls and events that move it between them.">
</picture>
<!-- /figure -->

Hardware description: [`docs/model/session-states.state.yml`](../model/session-states.state.yml)

Drawn from:

- [`docs/model/session-states.state.yml`](../model/session-states.state.yml)

## SPI timing with the wake handshake

<!-- figure: spi-timing -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="spi-timing.dark.svg">
  <img src="spi-timing.svg"
    alt="SPI timing of the BNO08x in mode 3: the host pulls WAKE low, the hub answers with H_INTN low, the
    host selects it with H_CSN and clocks bytes MSB first on CLK, and H_INTN rises once the select is
    seen.">
</picture>
<!-- /figure -->

Hardware description: [`docs/model/spi-timing.timing.yml`](../model/spi-timing.timing.yml)

Drawn from:

- [`docs/model/spi-timing.timing.yml`](../model/spi-timing.timing.yml)

## UART-SHTP character

<!-- figure: uart-timing -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="uart-timing.dark.svg">
  <img src="uart-timing.svg"
    alt="One UART-SHTP character from the BNO08x: H_INTN falls shortly before the start bit, eight data bits
    follow least significant first and the stop bit, and H_INTN rises before the transmission ends.">
</picture>
<!-- /figure -->

Hardware description: [`docs/model/uart-timing.timing.yml`](../model/uart-timing.timing.yml)

Drawn from:

- [`docs/model/uart-timing.timing.yml`](../model/uart-timing.timing.yml)
