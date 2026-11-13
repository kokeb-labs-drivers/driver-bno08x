# Troubleshooting

What to check when a session does not start, a call fails, or the data looks wrong. Nothing here has been confirmed on a
chip: the checks come from the documents and from the faults the test model injects ([Testing and CI](testing.md)).
Start with `GetState()` and `GetStats()`: they narrow most problems to a line of this page.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    - Interrupt timing: host must answer H_INTN: CEVA 1000-3927 1.17, 6.5.4 Interrupt timing, [p. 48](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf#page=48)
    - Command channel and the error list: CEVA 1000-3535 1.10, 5.1 Command Channel, [p. 13](https://www.ceva-ip.com/wp-content/uploads/Sensor-Hub-Transport-Protocol.pdf#page=14)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## `Begin()` returns `InvalidArgument`

A `Config` field is out of range: call `Config::Valid()` on it. The usual cause is an address other than `0x4A` or
`0x4B`, or a timeout set to zero.

## `Begin()` returns `Bus`

The port's `EnsureInitialized()` returned false, or on SPI the first `SetWake()` failed. Check the bus bring-up, the
pins and the pull-ups before anything else.

## The state stays `WaitAdvert`, then `Faulted`

The hub never sent its advertisement within `startup_timeout_ms`. Check, in this order:

1. Power and decoupling: 3.3 V on VDD and VDDIO, 100 nF on each and on CAP.
2. The strap pins: PS1 and PS0 must match the transport you configured and stay put until the hub is up.
3. BOOTN: it must be high at reset. A low level starts the bootloader, which speaks a different protocol.
4. NRST: without a `SetReset` hook the driver cannot pulse it; either wire it to a pull-up or provide the hook.
5. H_INTN: on I2C and SPI the driver reads only after H_INTN went low. A line that never moves means nothing is read.
   Make sure `IntAsserted()` returns true while the line is low.
6. The start-up time: after a reset the hub needs at least 90 ms before it asserts H_INTN.

## `Ready()` never turns true although the advertisement arrived

The driver also waits for the hub's reset-complete and initialise reports (A11). Set `require_init_response` to false
if your hub does not send the second one; read `Stats` for `early_cargo` and `bad_advert`.

## Operations end with `Timeout`

The hub did not answer within `op_timeout_ms` (`frs_write_timeout_ms` for a write). A sensor that is not on the board
(pressure, light, proximity, humidity, temperature need the optional parts on the secondary bus) never reports, and a
report the chip does not implement may never answer (A20, A31). Raise the timeout only after checking H_INTN.

## `Error::Variant`

The configured chip lacks the feature. Interactive calibration and the motion request exist on the BNO086 only; check
`Config::variant`, or set `strict_variant` to false to let the hub decide (A3).

## `Error::Framing`, many `resyncs`, `seq_gap` or `bad_cont_len`

The link is corrupting bytes or the hub is not following the rules the driver expects.

- I2C: lower the clock to 100 kbit/s; check pull-ups and wiring length. Make sure the port ends each transfer with a
  STOP and never uses a repeated start.
- SPI: mode 3, at most 3 MHz, chip select held for the whole transfer.
- UART-SHTP: 3 Mbit/s needs the 32.768 kHz clock source; the port must keep 100 microseconds between bytes. Without
  it a frame is cut and counted in `bad_escape`, `bad_protocol_id` or `oversize_frame` (A10).
- `bad_cont_len` with a healthy link: the hub answers a continuation differently from the specification. Set
  `strict_cont_len` to false and tell the maintainers (A14).

## The hub restarts again and again

`hub_resets` rises and `Reset` events appear. A watchdog or a brown-out is the usual cause: check the supply and
whether the host answers H_INTN in time, because the hub gives up on a host that is too slow (A38). Call `Poll()` at
least once per tenth of the fastest report period.

## Events are `Dropped`

The event queue filled. Pop events more often, raise `Limits::kEventQueue`, or batch (`FeatureConfig::batch_us`) and
poll less often with a larger queue.

## The angles jump or drift

Check `accuracy` on each sample: values below `High` mean the sensor is still calibrating
([Calibration](calibration.md)). Near a pitch of plus or minus 90 degrees yaw and roll are ill-defined by nature of
Euler angles; use the quaternion. After a tare, `PersistTare()` keeps it; without it a restart forgets it.

## Sample times are late or uneven

Without `TakeIntTimestamp()` the time of a sample is the time of the `Poll()` that saw H_INTN. Provide the hook (an
interrupt writing `NowUs()`) for exact times ([Porting](porting.md)).

## UART-RVC shows no frames or `rvc_bad_csum`

The baud rate is 115200 8N1 and the hub streams without being asked. A banner at start is skipped (`rvc_banner_bytes`).
A wrong checksum count with good wiring means the checksum range differs from the one assumed; `rvc_csum_with_mi` and
`rvc_csum_without_mi` say which one matches (A6).

Next: [Contract](contract.md).
