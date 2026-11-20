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
