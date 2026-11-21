# Ambiguities and decisions

The CEVA documents disagree with each other in places, are silent in others, and describe chips that differ in
small ways. This page lists every case the driver had to decide, what it decided, and the switch that changes it.
Nothing here is verified on hardware: the driver is tested against an independent model of the hub whose
`Knobs` reproduce each reading ([Testing and CI](testing.md)), so a hub that behaves differently is a bench result to
report, not a surprise in the field.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    - Features, variants and what differs: CEVA 1000-3927 1.17, Features (front page), [p. 1](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf#page=1)
    - Motion, orientation and environmental outputs: CEVA 1000-3927 1.17, 2 Sensor data processing, [p. 30](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf#page=30)
    - Interactive calibration (BNO086 only): CEVA 1000-3927 1.17, 3.3 Interactive Calibration (BNO086 only), [p. 39](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf#page=39)
    - Version history: BNO080 removed, polling note: CEVA 1000-3927 1.17, 8 Version history, [p. 56](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf#page=56)
    - UART inputs of the RVC mode: CEVA 1000-3625 1.9, 3.1.2 UART Inputs, [p. 12](https://www.ceva-ip.com/wp-content/uploads/SH-2-Reference-Manual.pdf#page=13)
    - Pin and interface differences from the BNO070: CEVA (Hillcrest Labs) none printed 1.6, 2.1 Pinout Comparisons, [p. 7](https://www.ceva-ip.com/wp-content/uploads/BNO080-BNO085-Migration-Guide.pdf#page=7)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

Document codes: DS is the data sheet (revision 1.17 unless named), RM the SH-2 reference manual, SHTP the transport
protocol document, TARE and CAL the tare and calibration guides, MIG the older migration guide. LIB is CEVA's
reference C library, read only as a non-normative cross-check. Page numbers are printed pages
([Datasheet map](datasheet.md)). "Model" names a knob of the test model, not of the driver; a driver setting is a
`Config` field.

## Variants

`Config::variant` selects the chip. It is never detected: no document maps the product ID part numbers to the three
names (A1), and the response is recorded in `Product()` but not used to choose behaviour.

| Behaviour | BNO080 | BNO085 | BNO086 |
| --- | --- | --- | --- |
| Interactive calibration, motion request 0x2B, command 0x0E | refused with `Variant` | refused with `Variant` | allowed |
| UART-RVC MI and MR bytes decoded | no | no | yes |
| I2C read when no data is waiting | not used: reads follow H_INTN | clock stretch | zero-length packet |
| Everything else | same driver paths | same | same |

With `strict_variant = false` the driver sends the request and lets the hub answer (A3, A4).

## The decisions

| Id | What the documents say | Decision and knob |
| --- | --- | --- |
| A1 | No document maps product ID part numbers to BNO080, BNO085 or BNO086 | `Config::variant` required (default `Bno085`); model `product_part_number`; the Product ID response is recorded, never used to choose behaviour |
| A2 | I2C read with no data: DS 1.17 says BNO085 stretches the clock, BNO086 sends zero-length packets; history p56: rev 1.15 "BNO086 does not support I2C polling", rev 1.17 "polling behavior for BNO086" | gate reads on H_INTN; accept zero-length headers; port documents its stretch timeout; model `i2c_empty_poll` in {stretch, zero_length, nack}; `poll_without_int` guarded |
| A3 | DS p1: BNO086 has an "identical ... software feature set" and also interactive calibration | `strict_variant` (default true) refuses 0x2B and command 0x0E on non-BNO086 with `Variant`; false sends and lets the hub decide; model `bno086_features_on_085` |
| A4 | SH-2 manual S5.6 and S6.4.12 describe interactive calibration generally; datasheets say BNO086 only | as A3 |
| A5 | RVC tail: DS p21 "one (BNO086) or three (otherwise) reserved bytes" after MI and MR | same 19-byte frame; `mi`/`mr` decoded only for `Bno086`; model `rvc_mi_mr` |
| A6 | RVC checksum list omits MI and MR (RM S3.1.1 "sequence number, yaw, pitch, roll, acceleration and reserved bytes") while DS says "Index, yaw, pitch, roll, acceleration and reserved data bytes"; the example has MI = MR = 0 and cannot decide | accept the sum over bytes 2 to 17 (MI/MR included) first, else over bytes 2 to 14 plus byte 17; count which matched in `Stats`; model `rvc_csum_includes_mi_mr` |
| A7 | RVC angle ranges differ per axis (yaw +/-180, pitch +/-90, roll +/-180) and there is no valid or status field | range check counted (`rvc_out_of_range`), not an error; model `rvc_angle_noise` |
| A8 | RM S3.1.2 lists `$MIB,RESET*87`, `$MIB,INTENT*0n`, `$MIB,DYNCAL*nn`, `$MIB,CLRDCD*00`; "*87" is not the XOR of the text (0x3F, recomputed here; INTENT 0x66, CLRDCD 0x74, DYNCAL 0x77) and the datasheet documents no RVC input | guarded, default `Unsupported`; model implements `$MIB` with `rvc_input_cs` in {xor, literal87, none} |
| A9 | RVC start prints an ASCII banner before frames (DS p43) | scanner skips non-0xAA bytes; a false `0xAA 0xAA` inside a frame is rejected by checksum and index continuity; model `rvc_banner` |
| A10 | RVC and UART-SHTP need a 32.768 kHz crystal or clock; internal clock "not accurate enough" (DS p15, p20, rev 1.8 note) | documented in hardware, not a code knob; model flags a UART error rate when `clock_internal` |
| A11 | Start-up order: advertisement, then executable reset-complete, then SH-2 initialise (DS p43); SHTP only requires the advertisement first | any order after the advertisement within `startup_timeout_ms`; `require_init_response`; an initialise response in `Ready` is a restart only when the session already had one; model `startup_order`, `omit_init_response` |
| A12 | The Max* advertisement values of the BNO08x are not documented (SHTP S5.2 shows an example only) | use advertised values; absent tags fall back to `kMaxTxCargo`/`kMaxRxCargo`; model `advert_limits`, `advert_without_limits` |
| A13 | SHTP allows fragmented host writes, the BNO08x does not (DS p22; SHTP errors 5, 6) | never fragment; `Oversize`; model `accept_fragmented_writes` off |
| A14 | Continuation reads: header with MSB set, length = remaining + 4 (SHTP S2.3.1); the BNO08x behaviour on a header-only first read is described only by the datasheet's "partial reads" sentence (DS p22) | `strict_cont_len` default true; model `cont_header_style` in {spec, no_header} (the second fails the engine into resync) |
| A15 | Sequence numbers after reset or at the first cargo on a channel are not specified | baseline from the first observed value after (re)start; gaps counted; model `seq_start`, `seq_skip_rate` |
| A16 | SH-2 per-report sequence "increments once for each report sent" (RM S6.5.1): per sensor or global is not stated | tracked per report ID; gaps counted only; model `report_seq_scope` |
| A17 | Base timestamp 0x7FFFFFFF means overflow (RM S7.2.1) and rebase accumulates (S7.2.2); nothing says what a host does on overflow | overflow disables the base for that cargo (time = `t_int - delay`) and counts `base_overflow`; model `base_overflow` |
| A18 | Step counter steps: RM Figure 102 shows a 16-bit field (bytes 8 to 9, two reserved), LIB reads a 32-bit value at bytes 8 to 11 | decode 16-bit (document normative); model `step_counter_upper_bytes` nonzero to show the difference; recorded in docs |
| A19 | Event payload widths: RM shows "LSB" for sigmotion, shake, flip, pickup, stability detector, tilt, pocket, circle (uint16) and single bytes for tap, stability classifier, sleep; LIB agrees | decode as the manual shows; upper byte nonzero counted `payload_upper_nonzero` |
| A20 | Environmental sensors depend on parts on the secondary I2C bus (BME280/BMP280, CM36686, DS p33) and may be absent | enabling a report the hub cannot serve times out; reported as `Timeout` with `HubError` detail when present; model `env_sensors` mask |
| A21 | Pin 10 is CLKSEL0 in DS 1.3 and later, EXTSYNC "future feature" in MIG 1.6 | DS wins; MIG kept for BNO070 history only |
| A22 | MIG notes "32 kHz crystal required" (22 pF); DS allows internal oscillator, crystal 12.5 pF | DS wins; hardware docs list both |
| A23 | VDDIO 1.65 V (pin table, DS p10) vs 1.7 V (operating conditions DS p45) | 1.7 V used as the specification; note on the chip card |
| A24 | ENV_SCL and ENV_SDA are pins 15 and 16 in DS 1.17 (corrected in rev 1.2) and swapped in MIG Figure 3 | DS 1.17 |
| A25 | RM Figure 32 summary table lists direction "W" for responses (0xFC, 0xF5) and the product ID section titles say "FSP3xx"; BNO08x byte layouts are the same as DS Figures 1-28 to 1-30 | layouts per the byte tables; directions per DS Figure 1-30 |
| A26 | RM Figure 37 lists status 12 twice ("unable to write record. FRS memory is full" and "12-255 reserved") | 12 treated as full; others above 12 as unknown failure; model `frs_status_ge12` |
| A27 | FRS write completion: statuses 8 (valid) and 3 (completed) both appear | success = 3; 8 is recorded as an intermediate; 9, 5, 7, 11, 12, 10 fail; model `frs_final_status` in {3_only, 8_then_3} |
| A28 | FRS read requests: bytes after the type are "reserved"; block reads were removed in RM 1.3 | send zero; model rejects non-zero reserved |
| A29 | FRS read busy: "responded to with a Read Response indicating BUSY and then it completes the in-progress read" | one operation at a time in the driver; model `frs_busy_interleave` |
| A30 | Metadata revision 3 vs 4 (word 9) and the unknown vendor ID length alignment | parse both; model `metadata_revision` per sensor |
| A31 | The BNO08x supports only a subset of the SH-2 reports | datasheet-listed set is "D" in 1.8; the rest "M" (manual only): allowed, timeout means unsupported; `Capabilities()` says which are D; model `sensor_mask` |
| A32 | Datasheet metadata example (Figure 1-32) shows rotation vector revision 4 with Q point 3 = 13; RM S6.5.18 says change sensitivity Q13 | consistent; metadata wins when present |
| A33 | Product ID: DS 1.3.2 (p23) and RM S6.3.2 show one response layout and do not say how many come; the vendor library expects 4 (5 for three part numbers) | a request completes after `product_id_entries` responses (default 1); `Product()` is entry 0, `ProductEntries()` holds the entries of the request; later entries are counted in `Stats::product_id_extra` and dropped; the model sends 4 with different part numbers (`product_id_entries`) |
| A34 | RM S6.5.29 says the step counter wakes the host before the steps field wraps; DS p35 says the 16-bit counter | 16-bit; nothing to do in the driver beyond the wake flag |
| A35 | Periodic DCD save parameter: 0 enables, 1 disables (RM S6.4.7; LIB `enabled ? 0 : 1`) | the API is a bool; model verifies both polarities |
| A36 | TARE cites RM S6.4.4.1 and S6.4.4.2 for tare now and persist; RM 1.9 numbers them S6.4.3.1, S6.4.3.2 | RM 1.9 numbers used |
| A37 | Tare Now supports axes 0x07 and 0x04 (RM S6.4.3.1 prose) although P1 is a bitmap | allowed masks 0x07 and 0x04; other masks `InvalidArgument` (knob `tare_any_mask` in the model) |
| A38 | DS 1.2.4.1 p19: H_INTN should be handled within 1/10 of the fastest sensor period; hub times out after about 10 ms | `Poll` guidance in docs; model `hint_host_timeout_ms` retries H_INTN |
| A39 | DS 1.2.2.1 says I2C "up to 400 kb/s"; the SPI limit is 3 MHz; UART 3 Mbit/s; no minimum host SPI clock is given | constants only |
| A40 | CEVA's tare guide is titled for BNO085/BNO086; the calibration guide 1.3 for BNO080/BNO085 | recorded, no behaviour |
| A41 | Pickup detector 0x1B: RM Figure 107 (p79) shows 6 bytes, the vendor library's length table 8 | built-in length 6; an advertised ReportLengths entry wins; model `pickup_report_len`, `advertise_report_lengths` |
| A42 | Wheel encoder 0x2E: the RM has no report layout, the vendor library's length table says 12 | no layout, `SetFeature` returns `Unsupported`; with an advertised length a report is skipped and counted in `unknown_report`, without one the rest of its cargo is skipped; model `wheel_encoder_len` |

Not exposed: command 0x10 (dead-reckoning calibration save, `sh2_saveDeadReckoningCalNow` in the vendor library) is
not in the RM command list (Figure 42, p44 and p45, IDs 1 to 14, plus 0x0F in S6.4.13), so the driver does not send it.

## Corrections found while checking the references

Thirty-odd page and section references were compared with the PDFs when the driver was written. These are the ones
that were wrong or imprecise in the working notes; the driver and `docs/datasheets.yml` use the corrected values.

| Reference as first noted | Corrected |
| --- | --- |
| Reports 0x13 and 0x1F are 5 bytes long | 6 bytes (RM report-length table) |
| One metadata record ID "0xE300" for all sensors | One per sensor, 0xE301 to 0xE325; 0x2C and 0x2D have none |
| 36 sensor input report IDs | 39 (40 counting the reserved 0x17) |
| DS p40, DS p18 | DS p41, DS p19 |
| RM section 6.5.2 p34, section 6.5 p42, Figure 75 p41 | RM p63, p62, p62 to 63 |
| RM section 7 p11, section 6.4.12.1 p12, Figure 111 p70 | RM p91, p60, p81 to 82 |
| SHTP Figure 16 p11, Figure 17 p12 | SHTP p15, p17 |
| DS section 1.4 p2 | DS p1 to p2 |
| One page offset for all documents | PDF page = printed page + 1 for RM, SHTP, the SH-2 update guide and the calibration |
| | guides 1.3 and later; 0 for the data sheet, the tare guide and the migration guide |
| "Accuracy under 10 degrees" from the data sheet | The tare guide, page 2 |
| Pin 1 marker described as the package feature | The marker belongs to the land pattern drawing |

The tare and calibration guides carry typos the driver does not copy: the update guide's command byte `0xF2` where
`0xFC` is meant, and a tag number 8 where 9 is meant. The driver's sensor-to-report-ID table is internal; it is
checked against the manual's figures, not against a library.

## Accuracy of the Euler helper

`ToEulerCentiDeg()` converts a Q14 quaternion with integer arithmetic only. It is accurate to a few centidegrees in
general; very close to a pitch of plus or minus 90 degrees yaw and roll are ill-defined and the error reaches about
80 centidegrees (the tests bound it there). Use the quaternion itself for control and Euler angles for display.

## Open questions for the vendor

A2 (I2C polling by variant), A3 (BNO086 features), A6 (the RVC checksum range), A8 (the RVC inputs), A12 (the
advertised limits), A14 (continuation reads), A15 (sequence numbers after reset), A17 (timestamp overflow), A27
(the FRS completion status), and whether any chip implements the manual-only sensors. Firmware update (the DFU
protocol is not public, DS p29) is not supported.
