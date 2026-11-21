# On the bus

What the BNO08x needs from each bus it speaks, signal by signal, with every time and rate taken from the CEVA data
sheet 1000-3927 revision 1.17. The SHTP framing on top of the bus is on the [Transports](transports.md) page; this
page is the waveform under it.

## I2C

The hub is an I2C slave at up to 400 kbit/s. H_INTN low tells the host a cargo is waiting; the host reads it with a
plain read (no repeated start) and the hub releases H_INTN once it has recognised its address. The host must allow
clock stretching.

<!-- figure: i2c-timing -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="diagrams/i2c-timing.dark.svg">
  <img src="diagrams/i2c-timing.svg"
    alt="I2C timing of a BNO08x read: H_INTN low asks for attention, the host sends a start, the address with
    the read bit and gets the acknowledge, H_INTN rises after the address, and the read ends with a NAK
    and a stop.">
</picture>
<!-- /figure -->

## SPI

The hub uses SPI mode 3 (CPOL 1, CPHA 1) at up to 3 MHz. When the host wants to talk first it pulls WAKE (the PS0
pin after reset) low and waits for H_INTN; the hub releases H_INTN as soon as it sees H_CSN low.

<!-- figure: spi-timing -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="diagrams/spi-timing.dark.svg">
  <img src="diagrams/spi-timing.svg"
    alt="SPI timing of the BNO08x in mode 3: the host pulls WAKE low, the hub answers with H_INTN low, the
    host selects it with H_CSN and clocks bytes MSB first on CLK, and H_INTN rises once the select is
    seen.">
</picture>
<!-- /figure -->

## UART

UART-SHTP runs at 3 Mbit/s, 8 data bits, one stop bit, no parity. The hub pulls H_INTN low shortly before it starts
to send, so the host can time-stamp the message; bytes from the host must be at least 100 µs apart. UART-RVC uses
the same character format at 115200 bit/s and only the hub's TX line.

<!-- figure: uart-timing -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="diagrams/uart-timing.dark.svg">
  <img src="diagrams/uart-timing.svg"
    alt="One UART-SHTP character from the BNO08x: H_INTN falls shortly before the start bit, eight data bits
    follow least significant first and the stop bit, and H_INTN rises before the transmission ends.">
</picture>
<!-- /figure -->

## Several devices

The SA0 pin sets the lowest bit of the 7-bit I2C address at reset, so two hubs can share one I2C bus, at 0x4A and
0x4B. The data sheet describes no shared SPI or UART arrangement beyond the usual one chip select per SPI device.

<!-- figure: i2c-topology -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="diagrams/i2c-topology.dark.svg">
  <img src="diagrams/i2c-topology.svg"
    alt="An I2C bus on which an MCU and two BNO08x hubs share SDA and SCL, one at address 0x4A with SA0 low
    and one at 0x4B with SA0 high, with one pull-up resistor per line.">
</picture>
<!-- /figure -->

## How these figures are made

Each figure is a model in `docs/model/` (`i2c-timing.timing.yml`, `spi-timing.timing.yml`, `uart-timing.timing.yml`
and `i2c-topology.topology.yml`) that cites the datasheet section and page of every number; `kokeb-docs figures`
draws it and the build fails on a number without a citation. The values table under each waveform is also written as
`docs/diagrams/<id>.table.html`.
