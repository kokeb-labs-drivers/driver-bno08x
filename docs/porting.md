# Porting

Run the driver on a new MCU by writing one class: the port.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From CEVA 1000-3927 1.17:

    - Pins, straps and the clock source: 1.2.1 Pin Descriptions, [p. 10](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf#page=10)
    - I2C: address, clock stretching, framing: 1.2.2 I2C interface, [p. 12](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf#page=12)
    - UART-SHTP: baud rate, framing, byte gap: 1.2.3 UART-SHTP interface, [p. 15](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf#page=15)
    - SPI: mode, WAKE, H_INTN handshake: 1.2.4 SPI Interface, [p. 17](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf#page=17)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

The driver never calls your SDK. It calls hooks on a class you derive from `bno08x::HubInterface<Self>` (in
[`inc/bno08x_interface.hpp`](../inc/bno08x_interface.hpp)). The class names its link with
`static constexpr bno08x::Transport kTransport`, and the driver template is constrained by the `HubPort` concept, so
calls resolve at compile time (no virtual functions, nothing to allocate). A missing hook, or one with a different
signature or without `noexcept`, is a compile error at the point of use.

## What you implement

| Hook | Transports | Required | Contract |
| --- | --- | --- | --- |
| `bool EnsureInitialized() noexcept` | all | yes | Bring the bus up on the first call; `true` at once afterwards. `false` makes `Begin()` return `Bus` |
| `uint32_t NowUs() noexcept` | all | yes | Free-running microsecond counter; the driver compares elapsed times, so the wrap at 2^32 is harmless as long as `Poll()` runs at least every 35 minutes (an interval measured across one longer pause can read short) |
| `bool I2cWrite(uint8_t addr, const uint8_t* data, std::size_t len) noexcept` | I2C | yes | START, `addr` + W, `len` bytes, STOP. `false` on a NACK or bus error |
| `bool I2cRead(uint8_t addr, uint8_t* data, std::size_t len) noexcept` | I2C | yes | START, `addr` + R, `len` bytes, NAK on the last, STOP; never a repeated start |
| `bool IntAsserted() noexcept` | I2C, SPI | yes | H_INTN level: `true` while it is low. Optional for the UART transports |
| `bool SpiTransfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept` | SPI | yes | One chip-select assertion for the whole call; mode 3, MSB first, at most 3 MHz, full duplex |
| `bool SetWake(bool level) noexcept` | SPI | yes | Drives PS0/WAKE: `true` high (idle), `false` low (wake request) |
| `std::size_t UartRead(uint8_t* data, std::size_t cap) noexcept` | UART | yes | The bytes that have arrived, at most `cap`; return 0 when there are none; never wait |
| `bool UartWriteByte(uint8_t) noexcept` | UART-SHTP | one of the two | Queue one byte, `false` when the transmitter is busy; the driver paces the bytes 100 us apart |
| `bool UartWrite(const uint8_t*, std::size_t) noexcept` | UART-SHTP | one of the two | A block, with `static constexpr bool kPortPacesUartTx = true`: your hardware keeps the 100 us gap |
| `bool SetReset(bool level) noexcept` | all but RVC | no | NRST, `true` high. Without it `HardReset()` returns `Unsupported` and `Begin()` joins a running hub |
| `bool SetBootn(bool level) noexcept` | all but RVC | no | BOOTN, held high around a reset so a stray low level cannot start the bootloader |
| `bool TakeIntTimestamp(uint32_t& us) noexcept` | I2C, SPI | no | The `NowUs()` time of the latest H_INTN falling edge, taken in an interrupt, consumed once |
| `void Log(LogLevel, const char*) noexcept` | all | no | Print one line; the message is plain text, never a format string |

`addr` is the 7-bit address from `Config::i2c_address`; shift it yourself if your SDK wants the 8-bit form. A hook
that returns `false` makes the driver return `Error::Bus` and, while reading, count a `bus_errors`. No hook may
block longer than one bus transfer.

## An I2C port

```cpp title="my_port.hpp"
--8<-- "tests/docs/my_port.hpp:port"
```

Rules the transfers must follow:

- **One call, one transaction.** The driver sends a complete SHTP cargo in one `I2cWrite()`, and reads a header and
  then the rest of the cargo in two `I2cRead()` calls. Never split or merge them.
- **No repeated start.** Every transfer ends with a STOP.
- **Read only when asked.** The driver calls `I2cRead()` after `IntAsserted()` returned `true`. A BNO085 stretches
  the clock when polled with nothing to say, so set your bus timeout above the worst stretch if you use
  `poll_without_int` (A2).
- **Raw bytes only.** Do not retry; the driver owns the framing.
- **Return `false` for a NACK or a bus error.**

## An SPI port

```cpp
--8<-- "tests/docs/ports.cpp:spi"
```

PS0 is the WAKE line: it must be high from before reset until after the first H_INTN, so configure the pin as an
output driven high before `Begin()`. The driver drives it low only around a write that found H_INTN deasserted. Keep
chip select low for the whole of one `SpiTransfer()` call and never in between.

## A UART-SHTP port

```cpp
--8<-- "tests/docs/ports.cpp:uart"
```

`UartRead()` returns what the receiver holds, without waiting. The hub's start-up traffic begins before the host
speaks, so the receiver must already be running when `Begin()` returns; size its buffer for the burst after a
reset. H_INTN is optional here (`IntAsserted()` and `TakeIntTimestamp()` give exact sample times when present).

## A UART-RVC port

```cpp
--8<-- "tests/docs/ports.cpp:rvc"
```

Receive only: 115200 baud, 8N1. A port without a transmit hook cannot send the `$MIB` inputs, which are refused in
any case (A8).

## Control pins and start-up

`Begin()` pulses NRST for `Config::reset_pulse_us`, then waits for the hub's start-up time (at least 90 ms plus the
configuration time) before expecting the advertisement. Drive the strap pins before the pulse: PS1 and PS0 select the
transport at reset and must stay put until the hub is up, and BOOTN must be high (pull it up with 10 k, or provide
`SetBootn`). Without `SetReset` wire NRST to a pull-up or to your supply reset; the driver then joins a hub that is
already running (it asks for the advertisement every `advert_retry_ms`); `Reset()` restarts it over the
executable channel.

## Check your port

1. Build it: the ports on this page are compiled by the host CI from [`tests/docs/`](../tests/docs/my_port.hpp),
   with the driver instantiated over them.
2. Run the [Quickstart](quickstart.md): `Ready()` turns true within the start-up timeout, and `Product()` holds the
   part number after `ReadProductId()`.
3. Enable the rotation vector at 100 Hz and tilt the board: the angles follow within a second, and `GetStats()`
   shows no `seq_gap`, `resyncs` or `bus_errors`.

## Reference implementations

Every port lives with its example, in `examples/<platform>/`; the driver has none. [Platforms](platforms.md) lists
them and how to add one.

- [`tests/model/bno08x_rig.hpp`](../tests/model/bno08x_rig.hpp): any PC, against a model of the hub, on every
  transport. The ports in it are the smallest complete ones.
- [`examples/esp32/main/esp32_bno08x_port.hpp`](../examples/esp32/main/esp32_bno08x_port.hpp): ESP-IDF, with
  every hook. Start from it for any ESP-IDF target.

## Threads

The driver is not thread-safe. Give each driver object one owner task, or guard every call with one mutex. When
several chips share a bus, the port must also serialise the bus. See [Contract](contract.md#threading).

## Editor setup

The clang-format and clang-tidy configuration is in `_config/`, and the repository root has no copy of it. Tools
that take a path use it directly; CI does the same:

| Tool | Setting |
| --- | --- |
| Command line | `clang-format --style=file:_config/.clang-format`, `clang-tidy --config-file=_config/.clang-tidy -p build` |
| VS Code (C/C++ extension) | `"C_Cpp.clang_format_style": "file:${workspaceFolder}/_config/.clang-format"` and `"C_Cpp.codeAnalysis.clangTidy.args": ["--config-file=${workspaceFolder}/_config/.clang-tidy"]` |
| clangd (VS Code clangd, Neovim, Emacs), CLion | These find `.clang-format` and `.clang-tidy` only by name, from the file upwards: copy both to the root once (`cp _config/.clang-format _config/.clang-tidy .`). `.gitignore` lists the copies, so they never reach a commit; copy again after `_config/` changes |

Configure with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` (clangd: `--compile-commands-dir=build`) so the editor sees the
same flags as the build.
