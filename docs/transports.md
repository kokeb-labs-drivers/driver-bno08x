# Transports

One class, four ways to reach the hub. The port you write names its transport; everything above it (operations,
events, calibration) is identical. The strap pins PS1 and PS0 choose the transport in the chip, so the port's
`kTransport` must match the wiring.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    - Protocol selection (PS1, PS0): CEVA 1000-3927 1.17, 1.2 BNO08X Connectivity, [p. 9](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf#page=9)
    - I2C: address, clock stretching, framing: CEVA 1000-3927 1.17, 1.2.2 I2C interface, [p. 12](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf#page=12)
    - UART-SHTP: baud rate, framing, byte gap: CEVA 1000-3927 1.17, 1.2.3 UART-SHTP interface, [p. 15](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf#page=15)
    - SPI: mode, WAKE, H_INTN handshake: CEVA 1000-3927 1.17, 1.2.4 SPI Interface, [p. 17](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf#page=17)
    - UART-RVC: the 19-byte frame: CEVA 1000-3927 1.17, 1.2.5 UART-RVC interface, [p. 20](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf#page=20)
    - SHTP header, channels and cargo reads: CEVA 1000-3927 1.17, 1.3.1 SHTP, [p. 22](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf#page=22)
    - SPI timing: CEVA 1000-3927 1.17, 6.5.2 SPI timing, [p. 46](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf#page=46)
    - Interrupt timing: host must answer H_INTN: CEVA 1000-3927 1.17, 6.5.4 Interrupt timing, [p. 48](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf#page=48)
    - UART inputs of the RVC mode: CEVA 1000-3625 1.9, 3.1.2 UART Inputs, [p. 12](https://www.ceva-ip.com/wp-content/uploads/SH-2-Reference-Manual.pdf#page=13)
    - Header, channels and the rules of a cargo: CEVA 1000-3535 1.10, 2.2.1 SHTP Header, [p. 4](https://www.ceva-ip.com/wp-content/uploads/Sensor-Hub-Transport-Protocol.pdf#page=5)
    - Reading cargoes, continuations and interrupts: CEVA 1000-3535 1.10, 2.5 Reading Cargoes, [p. 6](https://www.ceva-ip.com/wp-content/uploads/Sensor-Hub-Transport-Protocol.pdf#page=7)
    - Synchronous protocols: I2C and SPI handshake: CEVA 1000-3535 1.10, 3 Synchronous protocols (SPI/I2C), [p. 7](https://www.ceva-ip.com/wp-content/uploads/Sensor-Hub-Transport-Protocol.pdf#page=8)
    - SHTP over UART: control characters, BSQ and BSN: CEVA 1000-3535 1.10, 4 SHTP over UART, [p. 10](https://www.ceva-ip.com/wp-content/uploads/Sensor-Hub-Transport-Protocol.pdf#page=11)
    - Command channel and the error list: CEVA 1000-3535 1.10, 5.1 Command Channel, [p. 13](https://www.ceva-ip.com/wp-content/uploads/Sensor-Hub-Transport-Protocol.pdf#page=14)
    - Channel advertisement: CEVA 1000-3535 1.10, 5.2 Channel Advertisement, [p. 15](https://www.ceva-ip.com/wp-content/uploads/Sensor-Hub-Transport-Protocol.pdf#page=16)
    - SH-2 channel usage and report lengths: CEVA 1000-3600 1.6, 2.2 SH-2 Channel Usage, [p. 4](https://www.ceva-ip.com/wp-content/uploads/SH-2-SHTP-Reference-Manual.pdf#page=5)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

| Transport | `kTransport` | PS1 | PS0 | Speed | Hooks you write | Needs H_INTN |
| --- | --- | --- | --- | --- | --- | --- |
| I2C (SHTP) | `Transport::I2c` | 0 | 0 | 100 or 400 kbit/s | `I2cWrite`, `I2cRead`, `IntAsserted` | yes |
| SPI (SHTP) | `Transport::Spi` | 1 | 1 | up to 3 MHz | `SpiTransfer`, `SetWake`, `IntAsserted` | yes |
| UART (SHTP) | `Transport::UartShtp` | 1 | 0 | 3 Mbit/s | `UartRead`, `UartWrite` or `UartWriteByte` | optional |
| UART-RVC | `Transport::UartRvc` | 0 | 1 | 115200 baud | `UartRead` | no |

Every port also provides `EnsureInitialized()` and `NowUs()`. [Porting](porting.md) shows each one in code.

## SHTP in one page

Every message is a cargo with a 4-byte header: a 15-bit length (the top bit continues a cargo), a channel and a
per-channel sequence number. The driver uses channel 0 (the advertisement and the error list), 1 (executable
commands), 2 (SH-2 control), 3 and 4 (input reports, normal and wake-up) and 5 (the gyro-integrated rotation
vector). Cargoes can be fragmented by the hub; the driver reassembles them and never fragments what it sends: a
request that does not fit one write is refused with `Oversize`. The advertisement at start-up carries the limits;
without a limit tag the driver uses its `Limits` sizes (A12).

The framing engine is strict. Every rule it enforces has a counter in `Stats` (short transfers, a length of 0xFFFF,
a null header, a length of 1 to 3, an oversize cargo, a bad channel, lost or orphaned continuations, wrong
continuation lengths, duplicate or skipped sequence numbers, unknown or cut-short reports). `Config::resync_limit`
violations within `Config::resync_window_ms` make the driver abandon the running operations with `Framing`, count
a resynchronisation and reset the hub, instead of guessing at the bytes.

## I2C

The hub is a slave at `0x4A` (SA0 low) or `0x4B` (SA0 high). The driver reads only after H_INTN went low: first the
4-byte header, then exactly the rest of the cargo as a continuation, each as a separate transaction that ends with a
STOP (no repeated start). Reading without H_INTN is possible with `Config::poll_without_int`, which is guarded:
a BNO085 stretches the clock when it has nothing to say, a BNO086 answers with a zero-length packet (A2), so the port
documents its stretch timeout. Keep the bus at 400 kbit/s or less (A39).

## SPI

Mode 3 (clock idles high, data sampled on the second edge), most significant bit first, at most 3 MHz, full duplex.
H_INTN tells the host a cargo is waiting; the host then does one duplex transfer, which may be longer than the
cargo (the hub pads with zeros and a null header). To write, the driver drives WAKE low if H_INTN is not already
asserted, waits for H_INTN up to `Config::wake_timeout_us`, does the transfer and releases WAKE. PS0 is both a strap
and the WAKE line, so it must be a GPIO that is high while NRST is released: the driver drives it high before every
NRST pulse and releases it on every exit (operation time-out, `HardReset()`, resync, hub restart), DS p18.

## UART with SHTP

Frames start and end with `0x7E`; inside a frame `0x7E` and `0x7D` are escaped (`0x7D`, then the byte XOR `0x20`).
The byte after the start flag is the protocol ID: 1 for SHTP data, 0 for the hub's control frames (the buffer status
query, BSQ, and the buffer status notification, BSN, in which the hub says how many bytes it can take). The BSN is a
token: it holds until the next host write or until the advertised UART timeout (tag 0x81) expires, and the driver
sends only while it is valid and large enough. The SHTP gives no answer time for a BSQ, so a query whose BSN has not
arrived after `Config::uart_bsq_retry_ms` is sent again (`Stats::bsq_retries`) until the operation's own limit ends
it; a lost query or notification therefore costs one retry, never the link. A write that times out half-sent is
closed with a flag byte and never finished later, and a reset or resync drops a frame in progress. The hub needs at
least 100 microseconds between bytes it receives, so the port either sends byte by byte through `UartWriteByte` (the
driver paces it) or declares `kPortPacesUartTx = true` and implements `UartWrite`. Both UART modes need the
32.768 kHz clock on the board: the internal oscillator is not accurate enough at these rates (A10).

## UART-RVC

The hub streams a 19-byte frame at 100 Hz and takes no commands: `0xAA 0xAA`, an index that increases by one,
yaw, pitch and roll in 0.01 degree, acceleration in milli-g, the BNO086's MI and MR bytes, reserved bytes and a
checksum (the low byte of the sum of bytes 2 to 17, A6). `Poll()` delivers each valid frame as an `EventKind::Rvc`
event whose `v` holds `index, yaw, pitch, roll, ax, ay, az, mi, mr, mi_mr_valid`. An ASCII banner at power-up is
skipped (A9); a frame with a wrong checksum is counted and the scanner slides one byte. The `$MIB` text inputs
the manual describes (reset, intent, dynamic calibration) are refused with `Unsupported` unless
`rvc_inputs_experimental` is set, because the manual's own example checksum does not match its text (A8).

Next: [Sensors](sensors.md).
