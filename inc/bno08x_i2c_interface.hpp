/**
 * @file bno08x_i2c_interface.hpp
 * @brief What the BNO08x needs from the I2C bus (handbook KB-CPP-123); the port itself is in
 * `bno08x_interface.hpp`.
 * @copyright Copyright (c) 2026 The driver-bno08x authors. SPDX-License-Identifier: MIT
 */
#ifndef BNO08X_I2C_INTERFACE_HPP
#define BNO08X_I2C_INTERFACE_HPP

#include <cstdint>

namespace bno08x::i2c {

/**
 * @name Bus requirements
 * A port checks its settings against these with `static_assert`.
 * @{
 */
/// The 7-bit address with SA0 low (0x4B with SA0 high), DS p14.
inline constexpr uint8_t kI2cAddress = 0x4A;
/// The highest SCL frequency, DS p46 (100 kHz and 400 kHz are specified).
inline constexpr uint32_t kMaxClockHz = 400'000;
/// SHTP ends every transfer with a STOP: no repeated start (SHTP S3.2).
inline constexpr bool kRepeatedStart = false;
/** @} */

} // namespace bno08x::i2c

#endif // BNO08X_I2C_INTERFACE_HPP
