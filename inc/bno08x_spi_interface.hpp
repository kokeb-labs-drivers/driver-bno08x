/**
 * @file bno08x_spi_interface.hpp
 * @brief What the BNO08x needs from the SPI bus (handbook KB-CPP-123); the port itself is in
 * `bno08x_interface.hpp`.
 * @copyright Copyright (c) 2026 The driver-bno08x authors. SPDX-License-Identifier: MIT
 */
#ifndef BNO08X_SPI_INTERFACE_HPP
#define BNO08X_SPI_INTERFACE_HPP

#include <cstdint>

namespace bno08x::spi {

/**
 * @name Bus requirements
 * A port checks its settings against these with `static_assert`.
 * @{
 */
/// SPI mode 3: SCK idles high, data is sampled on the second edge, DS p19.
inline constexpr uint8_t kSpiMode = 3;
/// Each byte is clocked most significant bit first.
inline constexpr bool kMsbFirst = true;
/// The highest SCK frequency, DS p19.
inline constexpr uint32_t kMaxClockHz = 3'000'000;
/// The data sheet gives no CS high time; the hub needs at least 1 us between two H_INTN assertions,
/// so keep CS high that long.
inline constexpr uint32_t kMinCsHighNs = 1'000;
/// CS low to the first clock edge, minimum (0.1 us as printed), DS p47.
inline constexpr uint32_t kCsSetupNs = 100;
/// The last clock edge to CS high, minimum (16.83 ns as printed), DS p47.
inline constexpr uint32_t kCsHoldNs = 17;
/// One duplex transfer of any length per CS assertion: CS stays low for the whole transfer.
inline constexpr bool kContinuousWord = true;
/** @} */

} // namespace bno08x::spi

#endif // BNO08X_SPI_INTERFACE_HPP
