// clang-format off: the table below must keep one row per line.
/**
 * @file bno08x_interface.hpp
 * @brief The port of the BNO08x driver: the CRTP base, the four transports and the `HubPort` concept.
 * @copyright Copyright (c) 2026 The driver-bno08x authors. SPDX-License-Identifier: MIT
 *
 * @details
 * The driver never calls an SDK. It calls hooks on a class you derive from
 * `bno08x::HubInterface<Self>`. The class declares which link it is with
 * `static constexpr bno08x::Transport kTransport`, and the driver is constrained to the hooks of
 * that transport (each hook's exact signature and `noexcept` are checked). No hook may block
 * longer than one bus transfer; the driver never loops waiting on a hook.
 *
 * | Hook | Transports | Contract |
 * | --- | --- | --- |
 * | `bool EnsureInitialized() noexcept` | all | Bring the bus up on the first call; `true` at once afterwards |
 * | `uint32_t NowUs() noexcept` | all | Free-running microsecond counter; the driver compares elapsed times, so the wrap at 2^32 is harmless as long as `Poll()` runs at least every 35 minutes (an interval measured across one longer pause can read short) |
 * | `bool I2cWrite(uint8_t addr, const uint8_t* data, std::size_t len) noexcept` | I2C | START, `addr` + W, `len` bytes, STOP; `false` on a NACK or bus error |
 * | `bool I2cRead(uint8_t addr, uint8_t* data, std::size_t len) noexcept` | I2C | START, `addr` + R, `len` bytes, NAK on the last, STOP; never a repeated start; stretching by the hub is tolerated up to the port's own timeout |
 * | `bool IntAsserted() noexcept` | I2C, SPI | H_INTN level: `true` when low (optional for the UART transports) |
 * | `bool SpiTransfer(const uint8_t* tx, uint8_t* rx, std::size_t len) noexcept` | SPI | One chip-select assertion for the whole call, mode 3, MSB first, at most 3 MHz, full duplex |
 * | `bool SetWake(bool level) noexcept` | SPI | Drives PS0/WAKE: `true` = high (idle), `false` = low (wake request) |
 * | `std::size_t UartRead(uint8_t* data, std::size_t cap) noexcept` | UART | Bytes available now, at most `cap`; never waits |
 * | `bool UartWriteByte(uint8_t b) noexcept` | UART-SHTP | Queue one byte; `false` when the transmitter is busy. The driver paces bytes 100 us apart |
 *
 * Optional hooks (each detected by name, then checked for its exact signature): `bool SetReset(bool level)`
 * (NRST, `true` = high), `bool SetBootn(bool level)` (BOOTN, driven high around a reset),
 * `bool TakeIntTimestamp(uint32_t& us)` (the time of the latest H_INTN edge, consumed once),
 * `void Log(LogLevel, const char*)`, and for UART ports that pace bytes in hardware
 * `bool UartWrite(const uint8_t* data, std::size_t len)` together with
 * `static constexpr bool kPortPacesUartTx = true`.
 */
// clang-format on
#ifndef BNO08X_INTERFACE_HPP
#define BNO08X_INTERFACE_HPP

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "bno08x_detail.hpp"
#include "bno08x_i2c_interface.hpp"
#include "bno08x_spi_interface.hpp"
#include "bno08x_types.hpp"
#include "bno08x_uart_interface.hpp"

namespace bno08x {

/**
 * @brief CRTP base of the platform port.
 *
 * Derive `class MyPort : public bno08x::HubInterface<MyPort>`, add `static constexpr Transport
 * kTransport` and implement the hooks of your transport. The base declares no hook itself (a
 * forwarding hook would turn a missing override into infinite recursion).
 *
 * @tparam Derived Your port class.
 */
template <typename Derived>
class HubInterface {
public:
  /** @brief Ports are not copied: the driver holds a reference. */
  HubInterface(const HubInterface&) = delete;
  /** @brief Ports are not copied: the driver holds a reference. */
  HubInterface& operator=(const HubInterface&) = delete;

protected:
  /** @brief Only the derived port is constructed. */
  HubInterface() noexcept = default;
  /** @brief Destroyed through the derived port only. */
  ~HubInterface() noexcept = default;
};

namespace detail {

/// The hooks every transport needs.
template <typename Port>
concept CommonHooks = requires {
  ExactHook<Port, bool() noexcept>::Take(&Port::EnsureInitialized);
  ExactHook<Port, uint32_t() noexcept>::Take(&Port::NowUs);
};

/// The hooks of the I2C transport.
template <typename Port>
concept I2cHooks = requires {
  ExactHook<Port, bool(uint8_t, const uint8_t*, std::size_t) noexcept>::Take(&Port::I2cWrite);
  ExactHook<Port, bool(uint8_t, uint8_t*, std::size_t) noexcept>::Take(&Port::I2cRead);
  ExactHook<Port, bool() noexcept>::Take(&Port::IntAsserted);
};

/// The hooks of the SPI transport.
template <typename Port>
concept SpiHooks = requires {
  ExactHook<Port, bool(const uint8_t*, uint8_t*, std::size_t) noexcept>::Take(&Port::SpiTransfer);
  ExactHook<Port, bool(bool) noexcept>::Take(&Port::SetWake);
  ExactHook<Port, bool() noexcept>::Take(&Port::IntAsserted);
};

/// The hooks of the UART-RVC transport (receive only).
template <typename Port>
concept UartRxHooks = requires {
  ExactHook<Port, std::size_t(uint8_t*, std::size_t) noexcept>::Take(&Port::UartRead);
};

/// The port declares something called `UartWriteByte`.
template <typename Port>
concept DeclaresUartWriteByte = requires(Port& port) { port.UartWriteByte(uint8_t{0}); };

/// The port transmits one byte: `bool UartWriteByte(uint8_t) noexcept`.
template <typename Port>
concept HasUartWriteByte =
    requires { ExactHook<Port, bool(uint8_t) noexcept>::Take(&Port::UartWriteByte); };

/// The port declares something called `UartWrite`.
template <typename Port>
concept DeclaresUartWrite =
    requires(Port& port, const uint8_t* p) { port.UartWrite(p, std::size_t{0}); };

/// The port transmits a block: `bool UartWrite(const uint8_t*, std::size_t) noexcept`.
template <typename Port>
concept HasUartWrite = requires {
  ExactHook<Port, bool(const uint8_t*, std::size_t) noexcept>::Take(&Port::UartWrite);
};

/// The hooks of the UART-SHTP transport: receive, and one of the two transmit forms.
template <typename Port>
concept UartShtpHooks = UartRxHooks<Port> && (HasUartWriteByte<Port> || HasUartWrite<Port>);

/// The port declares `kTransport` with a value of `Transport`.
template <typename Port>
concept DeclaresTransport = requires {
  { Port::kTransport } -> std::convertible_to<Transport>;
};

/// The port's transport is `T`.
template <typename Port, Transport T>
concept IsTransport = DeclaresTransport<Port> && (Port::kTransport == T);

} // namespace detail

/**
 * @brief Satisfied by a class that derives from `HubInterface<Port>`, names its transport and
 * implements that transport's hooks with exactly the types in the table above.
 * @tparam Port The port class.
 */
template <typename Port>
concept HubPort =
    std::derived_from<Port, HubInterface<Port>> && detail::CommonHooks<Port> &&
    ((detail::IsTransport<Port, Transport::I2c> && detail::I2cHooks<Port>) ||
     (detail::IsTransport<Port, Transport::Spi> && detail::SpiHooks<Port>) ||
     (detail::IsTransport<Port, Transport::UartShtp> && detail::UartShtpHooks<Port>) ||
     (detail::IsTransport<Port, Transport::UartRvc> && detail::UartRxHooks<Port>));

} // namespace bno08x

#endif // BNO08X_INTERFACE_HPP
