# API guide

The driver's API by task, with code that compiles: start a session, run it, enable sensors, read events, handle
errors, and use the operations.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    - Report structure, product ID, executable commands: CEVA 1000-3927 1.17, 1.3.2 Report Structure, [p. 23](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf#page=23)
    - Configuration and the commands list: CEVA 1000-3927 1.17, 1.3.3 BNO08X Configuration, [p. 24](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf#page=24)
    - Feature reports, rate and batch: CEVA 1000-3625 1.9, 5.4 Feature Reports, [p. 34](https://www.ceva-ip.com/wp-content/uploads/SH-2-Reference-Manual.pdf#page=35)
    - Product ID request and response: CEVA 1000-3625 1.9, 6.3.1 Product ID Request (0xF9), [p. 39](https://www.ceva-ip.com/wp-content/uploads/SH-2-Reference-Manual.pdf#page=40)
    - Command request and response: CEVA 1000-3625 1.9, 6.3.8 Command Request (0xF2), [p. 43](https://www.ceva-ip.com/wp-content/uploads/SH-2-Reference-Manual.pdf#page=44)
    - Report errors, counters, initialise: CEVA 1000-3625 1.9, 6.4.1 Report Errors (0x01), [p. 45](https://www.ceva-ip.com/wp-content/uploads/SH-2-Reference-Manual.pdf#page=46)
    - Simple calibration, interactive calibration, wheel: CEVA 1000-3625 1.9, 6.4.10 Simple Calibration Commands (0x0C), [p. 55](https://www.ceva-ip.com/wp-content/uploads/SH-2-Reference-Manual.pdf#page=56)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## The shape of the API

| Group | Calls |
| --- | --- |
| Session | `Begin()`, `Poll()`, `GetState()`, `Ready()`, `HardReset()`, `Reset()`, `HubOn()`, `HubSleep()` |
| Sensors | `SetFeature()`, `GetFeature()`, `Feature()`, `Flush()`, `ReadMetadata()`, `GetMetadata()` |
| Events | `PopEvent()`, `Latest()`, `AgeUs()`, `GetStats()` |
| Identification | `ReadProductId()`, `Product()`, `Advertised()`, `bno08x::GetDriverVersion()` |
| Calibration and orientation | `ConfigureMeCal()`, `GetMeCal()`, `SaveDcd()`, `SetPeriodicDcd()`, `ClearDcdAndReset()`, `Tare()`, `PersistTare()`, `SetReorientation()`, `ClearReorientation()`, `StartSimpleCal()`, `FinishSimpleCal()`, `SetMotionIntent()` |
| Records | `ReadFrs()`, `WriteFrs()`, `EraseFrs()` |
| Diagnostics | `GetOscillator()`, `Initialize()`, `GetCounts()`, `ClearCounts()`, `ReportErrors()`, `SendWheel()` |
| UART-RVC | `RvcSend()` (guarded) |

A call that needs the hub returns `Result<OpId>`: check `ok()` and keep the id, or read `error`. The outcome arrives
later as an `OpDone` event carrying that id. `Result` is `[[nodiscard]]`, so an unchecked call is a compiler
warning. Calls that only look at what the driver already holds (`Feature()`, `Latest()`, `GetMetadata()`,
`Product()`) return at once.

## Start a session

```cpp
--8<-- "tests/docs/api_tasks.cpp:lifecycle"
```

`Begin()` validates the configuration, brings the port up and starts the reset; it does not wait. Call `Poll()` from
your loop with a microsecond time (the same clock as the port's `NowUs()`): it runs the reset wait, reads the
advertisement and completes the initialise handshake, and `Ready()` turns true. `GetState()` says where it is;
a step that exceeds `startup_timeout_ms` leaves the session `Faulted`, and `Begin()` (or `HardReset()`) starts it
again; a stop request (sensor off, `Reset()`, `HubSleep()`) is still written while it is `Faulted`. The
[Quickstart](quickstart.md) has the whole loop.

How often to call `Poll()`: at least once per tenth of the fastest report period. The hub gives up on a host that
answers H_INTN much later (A38), and a slow `Poll()` adds to every sample's latency.

## Enable sensors and read events

```cpp
--8<-- "tests/docs/api_tasks.cpp:sensors"
```

```cpp
--8<-- "tests/docs/api_tasks.cpp:events"
```

Events are the one output of the driver. `PopEvent()` returns false when none is waiting. The kinds:

| `EventKind` | Meaning |
| --- | --- |
| `Sensor` | A sample: `sensor`, `accuracy`, `t_us` (host time), `v[]` in the report's units |
| `OpDone` | An operation finished: `op`, `error`, `detail`, response values in `v[]` |
| `FeatureChanged` | The hub changed a sensor's rate by itself |
| `Reset` | The hub restarted; features are sent again with `auto_restore` |
| `FlushDone` | A `Flush()` completed for `sensor` |
| `HubError` | The SHTP error list: `detail` is the first code, `v[]` all of them |
| `Dropped` | The event queue was full; `detail` is how many events were lost |
| `Rvc` | A UART-RVC frame |
| `Sh2Error` | An entry of the SH-2 error queue (`ReportErrors()`) |

Drain the queue at least as often as it fills: the default holds 32 events, so a 100 Hz rotation vector plus a
100 Hz gyroscope fills it in 160 ms. A full queue drops new events and reports how many with one `Dropped` event
rather than overwriting or blocking.

[Sensors](sensors.md) lists every report and its units; [Calibration](calibration.md) covers tare, calibration and
records.

## Operations

```cpp
--8<-- "tests/docs/api_tasks.cpp:calibration"
```

One operation runs on the wire at a time; up to `Limits::kOpQueue` wait behind it, and one more returns `Busy`. An
operation that gets no answer ends with `Timeout` after `op_timeout_ms`; a hub restart ends it with `HubReset`.
`Reset()` restarts the hub's software, `HardReset()` pulses NRST (it needs the `SetReset` hook), `HubOn()` and
`HubSleep()` change the hub's power state, and `Initialize()` re-initialises the sensor hub.

## Handle errors

```cpp
--8<-- "tests/docs/api_tasks.cpp:errors"
```

| Signal | Meaning | What to do |
| --- | --- | --- |
| `Error::Bus` | A port hook returned `false` | Check the port, the address and the bus speed |
| `Error::Timeout` | The hub did not answer | Check power, NRST and H_INTN; a sensor the board lacks never answers |
| `Error::NotInitialized` | `Begin()` has not run or the session is not ready | Wait for `Ready()`; after `Faulted` call `Begin()` |
| `Error::Variant` | The configured chip lacks the feature | Check `Config::variant`; see [Ambiguities](ambiguities.md) |
| `Error::Busy` | The operation queue is full | Drain events and wait for an `OpDone` |
| `Error::Framing`, `HubReset` | Bad bytes or a restart ended the operation | Repeat it; if it recurs read `GetStats()` |
| `Error::Refused`, `DeviceFault` | The hub's status says no | The `detail` word holds the status |
| `Error::Oversize` | The request does not fit one write | Raise `Limits::kTxCargo` or send less |
| `Error::Unsupported` | The hook or transport cannot do it | Add the hook, or use another transport |

Every value, with the calls that return it, is on the [Contract](contract.md#errors) page. `bno08x::ToString()` names
an error for a log.

## Diagnostics

`GetStats()` returns counters of everything the driver dropped, repaired or noticed: each framing rule has one, and
so do resynchronisations, hub restarts, bus errors, dropped events, and the UART-RVC checksum results. They are never
reset by the driver. `Advertised()` holds what the hub advertised, `Product()` the product ID after
`ReadProductId()`, and `GetMetadata()` a sensor's range, resolution and power after `ReadMetadata()`.

`TraceLastTransfer()` copies the first 32 bytes of the most recent transfer the hub sent (the SHTP header first) and
`GetLastTransferLength()` gives its full length, both without any bus traffic. The driver offers no raw bus read: it
would race the framing engine.

Next: [Configuration](configuration.md).
