# Configuration

`Config` holds everything you choose about a session; `Limits` sizes the buffers. A `Config{}` is the recommended
setup for a BNO085 on I2C address 0x4A. The chip is never detected, so say which one you have.

## Fields

Fields with a unit carry it in the name. `Config::Valid()` checks a configuration without touching the chip, and
`Begin()` calls it first and returns `InvalidArgument` with nothing sent.

| Field | Default | Meaning |
| --- | --- | --- |
| `variant` | `Bno085` | The chip: `Bno080`, `Bno085` or `Bno086`; selects behaviour (A1) |
| `read_policy` | `HeaderThenRest` | I2C and SPI: read the header, then exactly the rest; or `Fixed` bytes per read |
| `i2c_address` | `0x4A` | `0x4A` (SA0 low) or `0x4B` (SA0 high) |
| `fixed_read_bytes` | 64 | Bytes per read for `Fixed`, 4 to 1024 |
| `strict_variant` | true | Refuse features the chip lacks with `Variant` (A3) |
| `auto_restore` | true | Send the enabled features again after the hub restarts |
| `poll_without_int` | false | Guarded, I2C only: read on a cadence without H_INTN (A2) |
| `require_init_response` | true | Wait for the hub's initialise response at start (A11) |
| `frs_verify` | true | Read an FRS record back after writing it |
| `set_feature_confirm` | false | Finish `SetFeature` on the hub's reply, not when written |
| `strict_raw` | false | Refuse a raw sensor while its base sensor is off |
| `strict_cont_len` | true | Require continuation length = remaining + 4 (A14) |
| `strict_padding` | false | Count non-zero padding bytes |
| `tare_any_mask` | false | Allow tare axis masks other than all axes and Z only (A37) |
| `rvc_strict` | false | UART-RVC: also require the index to increment by one |
| `rvc_inputs_experimental` | false | Guarded: allow the unverified `$MIB` text inputs (A8) |
| `startup_timeout_ms` | 500 | Limit for each start-up wait |
| `op_timeout_ms` | 1000 | Limit for one operation |
| `frs_write_timeout_ms` | 5000 | Limit for one FRS write step |
| `wake_timeout_us` | 10000 | SPI: limit for H_INTN after WAKE |
| `reset_pulse_us` | 1000 | NRST low time; the datasheet minimum is 10 ns |
| `advert_retry_ms` | 100 | Without NRST: wait before asking for the advertisement again |
| `uart_token_timeout_ms` | 1000 | UART-SHTP: BSN validity when the hub did not advertise one |
| `uart_bsq_retry_ms` | 20 | UART-SHTP: wait for a BSN before the query is sent again |
| `poll_interval_ms` | 10 | `poll_without_int`: read cadence |
| `resync_limit` | 4 | Framing violations within the window that force a recovery |
| `resync_window_ms` | 100 | The window of `resync_limit` |
| `product_id_entries` | 1 | Product ID responses that complete a request, 1 to 5 (A33); `Product()` is entry 0 |

`Config::Valid()` is false when an enumeration holds a value that is not an enumerator, the address is neither
`0x4A` nor `0x4B`, `fixed_read_bytes` is outside 4 to 1024, a timeout, `reset_pulse_us`, `uart_bsq_retry_ms` or the
resync window is zero, `wake_timeout_us` is not shorter than `op_timeout_ms` (so WAKE is always released before an
operation ends), or `product_id_entries` is outside 1 to 5.

```cpp
--8<-- "tests/docs/configuration.cpp:custom"
```

## Limits

`Bno08x<Port, Limits>` keeps every buffer inside the object; `Limits` gives the sizes at compile time and a
`static_assert` rejects values the protocol cannot work with.

| Member | Default | Minimum | Meaning |
| --- | --- | --- | --- |
| `kRxCargo` | 1024 | 256 | Largest cargo received, in bytes |
| `kTxCargo` | 128 | 24 | Largest cargo sent, header included |
| `kOpQueue` | 8 | 2 | Operations pending at once; one more returns `Busy` |
| `kEventQueue` | 32 | 4 | Events waiting for `PopEvent()`; a full queue drops and counts (`Dropped`) |

## Kconfig

Under ESP-IDF, `idf.py menuconfig` shows **BNO08x sensor hub** when the driver is a component of the project
([Installation](installation.md)); the menu is [`_config/Kconfig`](../_config/Kconfig). The options reach the code
only through [`inc/bno08x_kconfig.hpp`](../inc/bno08x_kconfig.hpp), never through a driver signature:

```cpp
--8<-- "tests/docs/configuration.cpp:kconfig"
```

| Option | Default | Effect |
| --- | --- | --- |
| `CONFIG_BNO08X_VARIANT_*` | BNO085 | `Config::variant` |
| `CONFIG_BNO08X_I2C_ADDR_HIGH` | n | `i2c_address` 0x4B |
| `CONFIG_BNO08X_NOT_STRICT_VARIANT` | n | `strict_variant` false |
| `CONFIG_BNO08X_NO_AUTO_RESTORE` | n | `auto_restore` false |
| `CONFIG_BNO08X_SET_FEATURE_CONFIRM` | n | `set_feature_confirm` |
| `CONFIG_BNO08X_RX_CARGO`, `_TX_CARGO` | 1024, 128 | `KconfigLimits` |
| `CONFIG_BNO08X_OP_QUEUE`, `_EVENT_QUEUE` | 8, 32 | `KconfigLimits` |
| `CONFIG_BNO08X_STARTUP_TIMEOUT_MS`, `_OP_TIMEOUT_MS` | 500, 1000 | the two timeouts |

Without Kconfig (plain CMake, other platforms) `KconfigDefaults()` returns `Config{}`. Both cases are host tests.

Next: [Porting](porting.md).
