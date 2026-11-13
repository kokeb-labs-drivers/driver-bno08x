# Contract

What the BNO08x driver promises, what it needs, and the rules a caller follows.

The driver inherits the [Kokeb Labs driver
contract](https://github.com/kokeb-labs/.github/blob/main/standards/driver.md); this page states only what is specific
to it. A change to anything here is a breaking change, listed under **Breaking** in the [changelog](changelog.md).

## Promises

- The constructor is `noexcept`, stores the port and does no I/O.
- `Begin(Config)` validates the configuration (`InvalidArgument`, nothing sent), brings the port up and starts the
  reset; it returns at once. `Poll(now_us)` does everything else: the reset wait, the advertisement, the initialise
  handshake. `Ready()` turns true when the session is up. Every other call returns `Error::NotInitialized` until
  then, and again after a start-up step timed out (`State::Faulted`, which only `Begin()` and `HardReset()` leave);
  the stop requests below are the exception. `Begin()` again, in any state, starts a new session: every operation
  still waiting ends with `NotInitialized` (its `OpDone` is queued with the events already waiting, which stay for
  `PopEvent()`).
- No call blocks. A call that needs the hub queues an operation and returns its `OpId`; the outcome arrives as an
  `OpDone` event. Every operation ends with `Timeout` at worst: its limit (`Config::op_timeout_ms`, an FRS write's
  answer `frs_write_timeout_ms`) starts when it reaches the head of the queue, so one queued behind others may wait up
  to the queue length times that. A stop queued before the session is up waits for it, which `startup_timeout_ms`
  bounds. The limits are elapsed times: a long gap between `Poll()` calls ends an operation at the next one.
- Every failed port write ends its operation with `Error::Bus`. A failed read in `Poll()` is counted in
  `Stats::bus_errors` and the session stays `Ready`; it is never turned into a sample, and no event reports it, so
  watch the counter when the link may fail.
- The driver never fragments a write and never sends a request larger than one transfer (`Oversize`).
- The framing engine follows the SHTP rules strictly, counts every violation in `Stats` and resets the hub after
  `resync_limit` of them within `resync_window_ms` (with NRST through it, without through the reset command, which
  the driver writes itself). Operations in flight end with `Framing`. A duplicate SHTP sequence number drops the
  transfer, except a new cargo on channel 0 or 1 (the advertisement, command responses, the reset-complete): after a
  restart every channel numbers from 0 again.
- After the hub restarts by itself the driver reports a `Reset` event, abandons running operations with `HubReset`,
  starts the host sequence numbers again and, with `Config::auto_restore`, enables the sensors again. A restart is
  seen through its advertisement, or through a reset-complete or initialise response the session already had (the
  boot's own may follow an answered advertisement within `startup_timeout_ms` of `Ready`). Only an enable the
  driver saw finish with `Ok` is remembered for restoring, and the newest request for a sensor has the last word: a
  failed or abandoned one is not sent again. A restore write that fails is tried three more times, then counted in
  `Stats::restore_failed`. `Ok` means written when `set_feature_confirm` is false and accepted when it is true.
- Stop requests (switching a sensor off with `SetFeature` and `interval_us == 0`, `Reset()`, `HubSleep()`) always act
  once `Begin()` has run, in every state and whatever the other fields hold: before the session is ready they wait
  for it (without NRST the driver's request for the advertisement goes ahead of them and may use the kept slot; a
  queue already full of stops leaves it no room, so the start-up times out); in `Faulted` they are written at once
  and complete when written; a stop queued when a start-up step times out still goes out. The last queue slot is kept
  for them; when the queue is full, the newest operation that has not started ends with `Busy` to make room; the same
  stop asked again while it waits returns the operation it
  already has, but only while it is still the newest request for its target (the sensor's feature, or the hub's
  power state: `Reset()`, `HubSleep()`, `HubOn()`), so off, on, off ends off. A stop gets `Busy` only when every
  queued operation is a stop, an internal request or the one being sent. A sensor switched off is forgotten by the
  restore list at once. Entering `Faulted` releases WAKE and closes a half-sent UART frame first, so the stops that
  follow reach the hub; a written `Reset()` restarts the sequence numbers, in `Faulted` too.
- Reset pins that cannot be driven (`HardReset()`, the NRST release of a reset, the reset of a resynchronisation)
  leave the session `Faulted` with `Error::Bus`; no `Reset` event is reported for a restart that did not happen.
- An advertised report length shorter than the 4-byte report header is refused (the built-in length is kept) and
  counted in `Stats::bad_advert`.
- A feature the configured chip lacks returns `Error::Variant` without sending anything (unless
  `strict_variant` is false). The UART-RVC transport accepts no command and returns `Unsupported` for them.
- An unknown `SensorId` returns `InvalidArgument`; no input crashes the driver. The fuzz target feeds arbitrary bytes
  on every transport, including UART frames longer than the receive buffer, which are dropped and counted in
  `Stats::oversize_frame`.
- A missing mandatory hook is a compile error; a missing optional hook (`SetReset`, `SetBootn`,
  `TakeIntTimestamp`, `Log`) changes a documented behaviour and never compiles away silently. A misdeclared hook is
  a compile error that names it.
- Firmware update and the bootloader are not supported.

## Depends on

| Dependency | Version | Why |
| --- | --- | --- |
| A C++20 compiler | GCC 10, Clang 13 or later | Concepts constrain the port |
| CMake | 3.16 or later | Generates `bno08x_version.h` |
| Your port | | The bus, a clock, H_INTN, and optionally NRST, BOOTN and logging ([Porting](porting.md)) |

Nothing else: no RTOS, no SDK, no other repository of the org.

## Threading

The driver is not thread-safe and takes no lock. Call `Poll()` and every other method from one task, or guard every
call on one object with a mutex; the layer that owns the driver owns that lock, and the bus lock when other chips
share the bus. A port's `TakeIntTimestamp()` may read a value an interrupt wrote, so make that access atomic. Nothing
else may run in an interrupt. Separate objects share no state.

## Errors

`bno08x::Error` inside `bno08x::Result<T>`. Values 0 to 9 are the shared base set; values from 16 are specific to
this driver.

| `Error` | Value | Returned when |
| --- | --- | --- |
| `Ok` | 0 | The call succeeded |
| `NotInitialized` | 1 | `Begin()` has not run, or the session is not ready, or it faulted |
| `InvalidArgument` | 2 | An argument or a `Config` field is out of range; nothing was sent |
| `Bus` | 3 | A port hook returned `false` |
| `Timeout` | 4 | The hub did not answer in the allowed time |
| `DeviceIdMismatch` | 5 | Never returned: the chip has no ID register |
| `Crc` | 6 | Never returned: no link of this chip carries a CRC |
| `Busy` | 7 | The operation queue is full (a stop request may displace a waiting operation, which then ends with `Busy`) |
| `DeviceFault` | 8 | The hub reported a failure in a response |
| `Unsupported` | 9 | The hook or feature the call needs is absent (a command on UART-RVC, `HardReset()` without `SetReset`) |
| `Framing` | 16 | A frame rule was broken and the engine resynchronised |
| `HubReset` | 17 | The hub restarted; the operation was abandoned |
| `Overflow` | 18 | The event queue or a cargo buffer was full |
| `Refused` | 19 | The hub answered with a failure status |
| `Oversize` | 20 | A request does not fit one write; nothing was sent |
| `NotAdvertised` | 21 | The channel or report was never advertised by the hub |
| `Variant` | 22 | The feature is not available on the configured chip |

The [API guide](api-guide.md#handle-errors) says what to do about each.

## Memory

No heap, no exceptions, no RTTI, no `std::function`, no `std::string`, no `printf` and no static mutable state.
Every buffer is a member of the driver object, sized by `Limits` ([Configuration](configuration.md)); the largest are
the receive cargo and the event queue. The UART-SHTP transport adds frame buffers of twice the cargo sizes for the
escaped bytes, and one chunk the UART transports read into (a member, so a DMA-filled read never lands in a dead
stack frame). Nothing is allocated after construction.

## Platform

Platform-free: the same code runs on any target with a C++20 compiler, including the host tests. The ESP-IDF port
in [`examples/esp32`](examples.md) is an example, not part of the driver.

## Deviations from the standard

None.
