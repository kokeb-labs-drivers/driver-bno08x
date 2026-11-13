# Calibration and orientation

How to make the hub's output accurate and keep it that way: the motion engine's calibration, saving it, tare and
reorientation, and the flash records behind them. Every call returns an `OpId` and finishes with an `OpDone`
event ([API guide](api-guide.md)).

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    - Sensor metadata and the FRS record list: CEVA 1000-3927 1.17, 1.3.4 Sensor Metadata, [p. 25](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf#page=25)
    - Calibration and its accuracy: CEVA 1000-3927 1.17, 3.1 Calibration Effects, [p. 37](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf#page=37)
    - Interactive calibration (BNO086 only): CEVA 1000-3927 1.17, 3.3 Interactive Calibration (BNO086 only), [p. 39](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf#page=39)
    - Orientation and tare: CEVA 1000-3927 1.17, 4 BNO08X orientation, [p. 40](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf#page=40)
    - Configuration records: CEVA 1000-3625 1.9, 4.3 Records, [p. 14](https://www.ceva-ip.com/wp-content/uploads/SH-2-Reference-Manual.pdf#page=15)
    - Sensor metadata record layout: CEVA 1000-3625 1.9, 5.1 Sensor Metadata, [p. 30](https://www.ceva-ip.com/wp-content/uploads/SH-2-Reference-Manual.pdf#page=31)
    - Interactive calibration: CEVA 1000-3625 1.9, 5.6 Interactive Calibration, [p. 36](https://www.ceva-ip.com/wp-content/uploads/SH-2-Reference-Manual.pdf#page=37)
    - FRS write and read: CEVA 1000-3625 1.9, 6.3.3 FRS Write Request (0xF7), [p. 40](https://www.ceva-ip.com/wp-content/uploads/SH-2-Reference-Manual.pdf#page=41)
    - Tare now and persist tare: CEVA 1000-3625 1.9, 6.4.3 Tare (0x03), [p. 47](https://www.ceva-ip.com/wp-content/uploads/SH-2-Reference-Manual.pdf#page=48)
    - Dynamic calibration data and ME calibration: CEVA 1000-3625 1.9, 6.4.5 Save DCD (0x06), [p. 51](https://www.ceva-ip.com/wp-content/uploads/SH-2-Reference-Manual.pdf#page=52)
    - Simple calibration, interactive calibration, wheel: CEVA 1000-3625 1.9, 6.4.10 Simple Calibration Commands (0x0C), [p. 55](https://www.ceva-ip.com/wp-content/uploads/SH-2-Reference-Manual.pdf#page=56)
    - Calibration procedure and user calibration: CEVA 1000-4044 1.6, 3.0 User Calibration, [p. 5](https://www.ceva-ip.com/wp-content/uploads/BNO080-BNO085-Sesnor-Calibration-Procedure.pdf#page=6)
    - Tare options and procedure: CEVA 1000-4045 1.3, Tare Overview, [p. 1](https://www.ceva-ip.com/wp-content/uploads/BNO080-BNO085-Tare-Function-Usage-Guide.pdf#page=1)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## Dynamic calibration

The motion engine calibrates the accelerometer, gyroscope and magnetometer while the board moves. Each sensor
reports its state as an accuracy (`Accuracy::Unreliable` to `High`) on every sample, and a rotation vector also
carries an estimated angular error. The calibration guides describe the movements that bring each sensor to high
accuracy; nothing in the driver speeds them up.

`ConfigureMeCal(MeCal)` chooses which sensors calibrate (`accel`, `gyro`, `mag`, and the `planar` and `on_table`
variants); `GetMeCal()` returns the current enables in the `OpDone` values. `SaveDcd()` writes the calibration
data to flash so the next start begins calibrated, `SetPeriodicDcd(true)` has the hub do that on its own, and
`ClearDcdAndReset()` throws it away and restarts the hub. The periodic-save flag is inverted in the protocol
(0 enables); the API takes a plain `bool` (A35).

```cpp
--8<-- "tests/docs/api_tasks.cpp:calibration"
```

## Tare and reorientation

A tare declares the current pose to be the reference. `Tare(AxisMask, TareBasis)` takes the axes (`All` or `ZOnly`;
the other masks are refused unless `Config::tare_any_mask` is set, A37) and the rotation vector the tare is based on.
A tare on the game rotation vector reorients only that vector and is not persistent. `PersistTare()` keeps the last
tare over a restart. `SetReorientation(Quat14)` and `ClearReorientation()` set the mounting orientation, a fixed
rotation applied to every output.

```cpp
--8<-- "tests/docs/api_tasks.cpp:tare"
```

## Interactive calibration (BNO086)

The BNO086 can be told what the host knows about its motion: `SetMotionIntent(MotionIntent)` with the states
unknown, stationary (with or without vibration), in motion and in motion accelerating. The hub asks for the
information through sensor 0x2B (`MotionRequest`), and `StartSimpleCal()` and `FinishSimpleCal()` run the simple
calibration, whose result is in the finish operation's `detail` (`SimpleCalStatus`). On a BNO080 or BNO085 the driver
refuses these with `Error::Variant` while `Config::strict_variant` is set (A3, A4).

## Records in flash (FRS)

The hub keeps its configuration in numbered records. `ReadFrs(record, FrsBuffer&)` fills a caller-owned buffer of up
to `kFrsMaxWords` words, which must stay valid until the `OpDone` event; `WriteFrs()` writes words and, with
`Config::frs_verify`, reads them back; `EraseFrs()` erases. One FRS operation runs at a time (A29). A write is
complete at status 3; status 12 means the memory is full (A26, A27). Reads send zero in the reserved bytes
(A28). Sensor metadata (`ReadMetadata`, `GetMetadata`) lives in per-sensor records and is parsed in both layout
revisions (A30).

```cpp
--8<-- "tests/docs/api_tasks.cpp:frs"
```

Writing a record the driver does not describe can change how the hub behaves in ways only a restart undoes. Use
the typed calls above where one exists.

Next: [Ambiguities](ambiguities.md).
