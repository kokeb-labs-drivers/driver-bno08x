/**
 * @file bno08x_registers.hpp
 * @brief BNO08x protocol constants: SHTP channels, SH-2 report, command and record identifiers, the
 * report length table, Q points and the electrical and timing facts the driver relies on.
 * @copyright Copyright (c) 2026 The driver-bno08x authors. SPDX-License-Identifier: MIT
 *
 * @details
 * The BNO08x has no register map: the host talks SHTP (framing) and SH-2 (reports, commands,
 * records). Section and page numbers are the printed ones of the CEVA documents: DS =
 * BNO080/BNO085/ BNO086 data sheet 1000-3927, RM = SH-2 Reference Manual 1000-3625, SHTP = Sensor
 * Hub Transport Protocol 1000-3535, SHU = SH-2 SHTP Reference Manual 1000-3600. All multi-byte
 * values are little-endian.
 */
#ifndef BNO08X_REGISTERS_HPP
#define BNO08X_REGISTERS_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "bno08x_detail.hpp"

/** @brief The BNO08x protocol constants. */
namespace bno08x::reg {

/**
 * @defgroup bno08x_registers Protocol constants
 * @brief Channels, identifiers, lengths and the datasheet's numbers.
 * @{
 */

// SHTP channels (DS p22, SHU Figure 2 p4).
inline constexpr uint8_t kChanCommand = 0;    ///< SHTP command channel (advertisement, error list).
inline constexpr uint8_t kChanExecutable = 1; ///< Executable ("device") channel: reset, on, sleep.
inline constexpr uint8_t kChanControl =
    2; ///< Sensor hub control: features, product ID, FRS, commands.
inline constexpr uint8_t kChanInputNormal = 3;  ///< Input reports of sensors that are not wake-up.
inline constexpr uint8_t kChanInputWake = 4;    ///< Input reports of wake-up sensors.
inline constexpr uint8_t kChanGyroRv = 5;       ///< Gyro-integrated rotation vector, no report ID.
inline constexpr std::size_t kChannelCount = 6; ///< Number of channels the driver tracks.

// Executable channel (DS p23 Figure 1-27).
inline constexpr uint8_t kExecReset = 1; ///< Host to hub: reset the hub.
inline constexpr uint8_t kExecOn = 2;    ///< Host to hub: leave sleep, enable configured sensors.
inline constexpr uint8_t kExecSleep =
    3; ///< Host to hub: sleep; only always-on and wake sensors run.
inline constexpr uint8_t kExecResetComplete = 1; ///< Hub to host: reset complete.

// SHTP command channel (SHTP S5.1, Figures 12 to 15).
inline constexpr uint8_t kShtpAdvertise = 0;     ///< Command and response 0: advertisement.
inline constexpr uint8_t kShtpErrorList = 1;     ///< Command and response 1: error list.
inline constexpr uint8_t kShtpErrorCodeMax = 12; ///< Highest defined SHTP error code (Figure 15).

// Control report IDs on channel 2 (RM Figure 32 p38).
inline constexpr uint8_t kRepGetFeatureReq = 0xFE;   ///< Get Feature Request (2 bytes).
inline constexpr uint8_t kRepSetFeature = 0xFD;      ///< Set Feature Command (17 bytes).
inline constexpr uint8_t kRepGetFeatureRsp = 0xFC;   ///< Get Feature Response (17 bytes).
inline constexpr uint8_t kRepBaseTimestamp = 0xFB;   ///< Base Timestamp Reference (5 bytes).
inline constexpr uint8_t kRepTimestampRebase = 0xFA; ///< Timestamp Rebase (5 bytes).
inline constexpr uint8_t kRepProductIdReq = 0xF9;    ///< Product ID Request (2 bytes).
inline constexpr uint8_t kRepProductIdRsp = 0xF8;    ///< Product ID Response (16 bytes).
inline constexpr uint8_t kRepFrsWriteReq = 0xF7;     ///< FRS Write Request (6 bytes).
inline constexpr uint8_t kRepFrsWriteData = 0xF6;    ///< FRS Write Data Request (12 bytes).
inline constexpr uint8_t kRepFrsWriteRsp = 0xF5;     ///< FRS Write Response (4 bytes).
inline constexpr uint8_t kRepFrsReadReq = 0xF4;      ///< FRS Read Request (8 bytes).
inline constexpr uint8_t kRepFrsReadRsp = 0xF3;      ///< FRS Read Response (16 bytes).
inline constexpr uint8_t kRepCommandReq = 0xF2;      ///< Command Request (12 bytes).
inline constexpr uint8_t kRepCommandRsp = 0xF1;      ///< Command Response (16 bytes).
inline constexpr uint8_t kRepForceFlush = 0xF0;      ///< Force Sensor Flush (2 bytes).
inline constexpr uint8_t kRepFlushDone = 0xEF;       ///< Flush Completed (2 bytes).

// SH-2 command IDs (RM Figure 42 p44).
inline constexpr uint8_t kCmdErrors = 0x01;        ///< Report errors.
inline constexpr uint8_t kCmdCounters = 0x02;      ///< Counter commands.
inline constexpr uint8_t kCmdTare = 0x03;          ///< Tare commands.
inline constexpr uint8_t kCmdInitialize = 0x04;    ///< Initialise a subsystem.
inline constexpr uint8_t kCmdSaveDcd = 0x06;       ///< Save the dynamic calibration data.
inline constexpr uint8_t kCmdMeCal = 0x07;         ///< Motion engine calibration enables.
inline constexpr uint8_t kCmdPeriodicDcd = 0x09;   ///< Periodic DCD save on or off.
inline constexpr uint8_t kCmdOscillator = 0x0A;    ///< Oscillator type.
inline constexpr uint8_t kCmdClearDcdReset = 0x0B; ///< Clear DCD and reset.
inline constexpr uint8_t kCmdSimpleCal = 0x0C;     ///< Simple calibration start and finish.
inline constexpr uint8_t kCmdBootloader = 0x0D;    ///< Bootloader (FSP200 only; never sent).
inline constexpr uint8_t kCmdInteractiveCal =
    0x0E;                                  ///< Interactive calibration motion intent (BNO086).
inline constexpr uint8_t kCmdWheel = 0x0F; ///< Wheel request (dead reckoning input).
inline constexpr uint8_t kCmdUnsolicited =
    0x80; ///< Bit 7 of a response's command: autonomous response.

// Report lengths in bytes, report ID included (RM Figures 33 to 41, 77 to 81, 124 and 125).
inline constexpr std::size_t kLenGetFeature = 17;   ///< Set Feature and Get Feature Response.
inline constexpr std::size_t kLenProductId = 16;    ///< Product ID Response.
inline constexpr std::size_t kLenCommand = 12;      ///< Command Request.
inline constexpr std::size_t kLenCommandRsp = 16;   ///< Command Response.
inline constexpr std::size_t kLenFrsWriteReq = 6;   ///< FRS Write Request.
inline constexpr std::size_t kLenFrsWriteData = 12; ///< FRS Write Data Request.
inline constexpr std::size_t kLenFrsWriteRsp = 4;   ///< FRS Write Response.
inline constexpr std::size_t kLenFrsReadReq = 8;    ///< FRS Read Request.
inline constexpr std::size_t kLenFrsReadRsp = 16;   ///< FRS Read Response.
inline constexpr std::size_t kLenTimestamp = 5;     ///< Base Timestamp and Rebase.
inline constexpr std::size_t kLenGyroRv = 14;      ///< Gyro-integrated rotation vector (channel 5).
inline constexpr std::size_t kLenSensorHeader = 4; ///< Report ID, sequence, status, delay.
inline constexpr std::size_t kShtpHeaderLen = 4;   ///< SHTP header: length (2), channel, sequence.

// Feature flag bits (RM S6.5.2 p63).
inline constexpr uint8_t kFlagRelative = 0x01;     ///< Bit 0: change sensitivity is relative.
inline constexpr uint8_t kFlagChangeEnable = 0x02; ///< Bit 1: change sensitivity enabled.
inline constexpr uint8_t kFlagWake = 0x04;         ///< Bit 2: wake-up sensor.
inline constexpr uint8_t kFlagAlwaysOn = 0x08;     ///< Bit 3: always-on sensor.

// FRS record types (RM Figure 26 p28 to p29, DS Figure 1-31 p24 to p25).
inline constexpr uint16_t kFrsStaticCalAgm = 0x7979;  ///< Static calibration AGM (proprietary).
inline constexpr uint16_t kFrsNominalCalAgm = 0x4D4D; ///< Nominal calibration AGM (read only).
inline constexpr uint16_t kFrsStaticCalSra = 0x8A8A;  ///< Static calibration SRA (proprietary).
inline constexpr uint16_t kFrsNominalCalSra = 0x4E4E; ///< Nominal calibration SRA (read only).
inline constexpr uint16_t kFrsDynamicCal = 0x1F1F;    ///< Dynamic calibration data (DCD).
inline constexpr uint16_t kFrsPowerManagement =
    0xD3E2; ///< MotionEngine power management, stability.
inline constexpr uint16_t kFrsSystemOrientation = 0x2D3E; ///< System orientation quaternion.
inline constexpr uint16_t kFrsPrimaryAccelOrientation =
    0x2D41; ///< Primary accelerometer orientation.
inline constexpr uint16_t kFrsScreenAccelOrientation =
    0x2D43; ///< Screen rotation accelerometer orientation.
inline constexpr uint16_t kFrsGyroOrientation = 0x2D46; ///< Gyroscope orientation.
inline constexpr uint16_t kFrsMagOrientation = 0x2D4C;  ///< Magnetometer orientation.
inline constexpr uint16_t kFrsArvrRotationVector =
    0x3E2D; ///< AR/VR stabilisation, rotation vector.
inline constexpr uint16_t kFrsArvrGameRotationVector =
    0x3E2E; ///< AR/VR stabilisation, game rotation vector.
inline constexpr uint16_t kFrsSignificantMotion =
    0xC274;                                   ///< Significant motion detector configuration.
inline constexpr uint16_t kFrsShake = 0x7D7D; ///< Shake detector configuration.
inline constexpr uint16_t kFrsMaxFusionPeriod = 0xD7D7; ///< Maximum fusion period.
inline constexpr uint16_t kFrsSerialNumber = 0x4B4B;    ///< Serial number.
inline constexpr uint16_t kFrsEnvPressureCal = 0x39AF;  ///< Environmental calibration: pressure.
inline constexpr uint16_t kFrsEnvTemperatureCal =
    0x4D20;                                            ///< Environmental calibration: temperature.
inline constexpr uint16_t kFrsEnvHumidityCal = 0x1AC9; ///< Environmental calibration: humidity.
inline constexpr uint16_t kFrsEnvAmbientLightCal =
    0x39B1; ///< Environmental calibration: ambient light.
inline constexpr uint16_t kFrsEnvProximityCal = 0x4DA2; ///< Environmental calibration: proximity.
inline constexpr uint16_t kFrsAlsCal = 0xD401;          ///< ALS calibration (device specific).
inline constexpr uint16_t kFrsProximityCal =
    0xD402; ///< Proximity sensor calibration (device specific).
inline constexpr uint16_t kFrsPickupConfig = 0x1B2A; ///< Pickup detector configuration.
inline constexpr uint16_t kFrsFlipConfig = 0xFC94;   ///< Flip detector configuration.
inline constexpr uint16_t kFrsStabilityDetectorConfig =
    0xED85;                                            ///< Stability detector configuration.
inline constexpr uint16_t kFrsActivityConfig = 0xED88; ///< Activity tracker configuration.
inline constexpr uint16_t kFrsSleepConfig = 0xED87;    ///< Sleep detector configuration.
inline constexpr uint16_t kFrsTiltConfig = 0xED89;     ///< Tilt detector configuration.
inline constexpr uint16_t kFrsPocketConfig = 0xEF27;   ///< Pocket detector configuration.
inline constexpr uint16_t kFrsCircleConfig = 0xEE51;   ///< Circle detector configuration.
inline constexpr uint16_t kFrsUserRecord = 0x74B4;     ///< User record, up to 64 words.
inline constexpr uint16_t kFrsTimeSource = 0xD403;     ///< MotionEngine time source selection.
inline constexpr uint16_t kFrsUartFormat = 0xA1A1;     ///< UART output format (RVC).
inline constexpr uint16_t kFrsGyroRvConfig =
    0xA1A2; ///< Gyro-integrated rotation vector configuration.
inline constexpr uint16_t kFrsFusionControl = 0xA1A3; ///< Fusion control flags.
inline constexpr uint16_t kFrsSimpleCalConfig =
    0xA1A4; ///< Simple calibration configuration (proprietary).
inline constexpr uint16_t kFrsSimpleCalNominal =
    0xA1A5; ///< Simple calibration nominal (proprietary).
inline constexpr uint16_t kFrsMetadataFirst =
    0xE301; ///< First sensor metadata record (raw accelerometer).
inline constexpr uint16_t kFrsMetadataLast =
    0xE325; ///< Last sensor metadata record (motion request).
inline constexpr std::size_t kUserRecordWords = 64; ///< Largest user record, in 32-bit words.

// FRS status codes (RM Figures 37 and 39).
inline constexpr uint8_t kFrsWWordsReceived = 0;  ///< Write status: word(s) received.
inline constexpr uint8_t kFrsWUnrecognised = 1;   ///< Write status: unrecognised FRS type.
inline constexpr uint8_t kFrsWBusy = 2;           ///< Write status: busy.
inline constexpr uint8_t kFrsWCompleted = 3;      ///< Write status: write completed.
inline constexpr uint8_t kFrsWReady = 4;          ///< Write status: write mode entered or ready.
inline constexpr uint8_t kFrsWFailed = 5;         ///< Write status: write failed.
inline constexpr uint8_t kFrsWNotInWriteMode = 6; ///< Write status: data while not in write mode.
inline constexpr uint8_t kFrsWInvalidLength = 7;  ///< Write status: invalid length.
inline constexpr uint8_t kFrsWRecordValid = 8;    ///< Write status: record valid.
inline constexpr uint8_t kFrsWRecordInvalid = 9;  ///< Write status: record invalid.
inline constexpr uint8_t kFrsWDeviceError = 10;   ///< Write status: device error (deprecated).
inline constexpr uint8_t kFrsWReadOnly = 11;      ///< Write status: record is read only.
inline constexpr uint8_t kFrsWMemoryFull = 12;    ///< Write status: FRS memory is full.
inline constexpr uint8_t kFrsRNoError = 0;        ///< Read status: no error.
inline constexpr uint8_t kFrsRUnrecognised = 1;   ///< Read status: unrecognised FRS type.
inline constexpr uint8_t kFrsRBusy = 2;           ///< Read status: busy.
inline constexpr uint8_t kFrsRCompleted = 3;      ///< Read status: read record completed.
inline constexpr uint8_t kFrsREmpty = 5;          ///< Read status: record empty.
inline constexpr uint8_t kFrsRDeviceError = 8;    ///< Read status: device error.

// ME calibration enable bits (RM S6.4.6; also the RVC $MIB,DYNCAL mask).
inline constexpr uint8_t kCalAccel = 0x01;   ///< Accelerometer calibration.
inline constexpr uint8_t kCalGyro = 0x02;    ///< Gyroscope calibration.
inline constexpr uint8_t kCalMag = 0x04;     ///< Magnetometer calibration.
inline constexpr uint8_t kCalPlanar = 0x08;  ///< Planar accelerometer calibration.
inline constexpr uint8_t kCalOnTable = 0x10; ///< On-table calibration.

// Electrical and timing facts (DS p9 to p21, p43 to p52).
inline constexpr uint8_t kI2cAddrLow = 0x4A;  ///< 7-bit I2C address with SA0 low (DS p14).
inline constexpr uint8_t kI2cAddrHigh = 0x4B; ///< 7-bit I2C address with SA0 high.
inline constexpr uint8_t kBootI2cAddrLow =
    0x28; ///< Bootloader I2C address, SA0 low (DS p29; unused).
inline constexpr uint8_t kBootI2cAddrHigh = 0x29;    ///< Bootloader I2C address, SA0 high (unused).
inline constexpr uint32_t kI2cMaxHz = 400'000;       ///< Highest I2C clock (DS p46).
inline constexpr uint32_t kSpiMaxHz = 3'000'000;     ///< Highest SPI clock, mode 3 (DS p19, p47).
inline constexpr uint32_t kUartShtpBaud = 3'000'000; ///< UART-SHTP rate, 8N1 (DS p17).
inline constexpr uint32_t kUartRvcBaud = 115'200;    ///< UART-RVC rate, 8N1 (DS p20).
inline constexpr uint32_t kUartHostByteGapUs =
    100; ///< Minimum gap between bytes from the host (SHTP S4.5).
inline constexpr uint32_t kResetPulseMinNs = 10; ///< Shortest NRST pulse (DS p47).
inline constexpr uint32_t kStartupT1Ms =
    90; ///< Internal initialisation after reset, at least (DS p47).
inline constexpr uint32_t kStartupT2Ms = 4;     ///< Internal configuration, about (DS p47).
inline constexpr uint32_t kWakeTypicalUs = 150; ///< WAKE assert to H_INTN assert, typical (DS p48).
inline constexpr uint32_t kHintHostTimeoutMs =
    10; ///< Hub-side wait for the host after H_INTN (DS p19).
inline constexpr std::size_t kRvcFrameLen = 19; ///< UART-RVC frame length (DS p21).
inline constexpr uint32_t kRvcRateHz = 100;     ///< UART-RVC frame rate (DS p20).
inline constexpr uint32_t kReportRateMinPct =
    90; ///< A request is served at 0.9 x or more (DS p50).
inline constexpr uint32_t kReportRateMaxPct = 210; ///< and at 2.1 x or less (DS p50).
inline constexpr uint32_t kDelayTickUs =
    100; ///< Unit of the delay field and the timestamps (RM S7.2).
inline constexpr uint32_t kSupplyVddMinMv = 2400; ///< VDD minimum (DS p45).
inline constexpr uint32_t kSupplyVddMaxMv = 3600; ///< VDD maximum (DS p45).
inline constexpr uint32_t kSupplyVddioMinMv =
    1700; ///< VDDIO minimum (DS p45; pin table says 1650).
inline constexpr uint32_t kSupplyVddioMaxMv = 3600; ///< VDDIO maximum (DS p45).

/** @} */

namespace detail {

/// Index of the longest sensor ID plus one: the report table covers IDs 0x00 to 0x2E, the same
/// range as the driver's per-sensor slots (0x2E, the wheel encoder, has no layout).
inline constexpr std::size_t kSensorTableSize = 0x2F;

/// Length in bytes, report ID included, of each sensor input report (0 = no layout in the manual).
/// RM Figures 82 to 122; 0x17 is reserved, 0x2A travels on channel 5 without an ID.
constexpr std::array<uint8_t, kSensorTableSize> MakeReportLengths() noexcept {
  std::array<uint8_t, kSensorTableSize> t{};
  const std::array<std::array<uint8_t, 2>, 40> rows{{
      {{0x01, 10}}, {{0x02, 10}}, {{0x03, 10}}, {{0x04, 10}}, {{0x05, 14}}, {{0x06, 10}},
      {{0x07, 16}}, {{0x08, 12}}, {{0x09, 14}}, {{0x0A, 8}},  {{0x0B, 8}},  {{0x0C, 6}},
      {{0x0D, 6}},  {{0x0E, 6}},  {{0x0F, 16}}, {{0x10, 5}},  {{0x11, 12}}, {{0x12, 6}},
      {{0x13, 6}},  {{0x14, 16}}, {{0x15, 16}}, {{0x16, 16}}, {{0x18, 8}},  {{0x19, 6}},
      {{0x1A, 6}},  {{0x1B, 6}},  {{0x1C, 6}},  {{0x1E, 16}}, {{0x1F, 6}},  {{0x20, 6}},
      {{0x21, 6}},  {{0x22, 6}},  {{0x23, 6}},  {{0x28, 14}}, {{0x29, 12}}, {{0x2B, 6}},
      {{0x2C, 24}}, {{0x2D, 60}}, {{0x00, 0}},  {{0x00, 0}},
  }};
  for (const auto& r : rows) {
    t[r[0]] = r[1];
  }
  return t;
}

/// The report length table.
inline constexpr std::array<uint8_t, kSensorTableSize> kReportLengths = MakeReportLengths();

} // namespace detail

} // namespace bno08x::reg

#endif // BNO08X_REGISTERS_HPP
