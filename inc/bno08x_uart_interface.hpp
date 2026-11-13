/**
 * @file bno08x_uart_interface.hpp
 * @brief What the BNO08x needs from the UART link (handbook KB-CPP-123); the port itself is in
 * `bno08x_interface.hpp`.
 * @copyright Copyright (c) 2026 The driver-bno08x authors. SPDX-License-Identifier: MIT
 */
#ifndef BNO08X_UART_INTERFACE_HPP
#define BNO08X_UART_INTERFACE_HPP

#include <cstdint>

namespace bno08x::uart {

/**
 * @name Bus requirements
 * A port checks its settings against these with `static_assert`.
 * @{
 */
/// The highest baud rate: SHTP over UART runs at 3 Mbit/s, DS p20 (UART-RVC uses 115200).
inline constexpr uint32_t kMaxBaudRate = 3'000'000;
/// The minimum gap between two bytes the host sends, microseconds (SHTP S4).
inline constexpr uint32_t kTurnaroundUs = 100;
/** @} */

} // namespace bno08x::uart

#endif // BNO08X_UART_INTERFACE_HPP
