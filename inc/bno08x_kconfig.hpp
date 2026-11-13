/**
 * @file bno08x_kconfig.hpp
 * @brief Optional ESP-IDF layer: a `Config` and the buffer `Limits` built from `CONFIG_BNO08X_*`
 * options.
 * @copyright Copyright (c) 2026 The driver-bno08x authors. SPDX-License-Identifier: MIT
 *
 * @details
 * Include this header only where you want menuconfig to choose the defaults; the driver never
 * includes it and no driver signature depends on it. Under ESP-IDF the values come from
 * `sdkconfig.h`. Without Kconfig every option falls back to the driver's default, so
 * `KconfigDefaults()` equals `Config{}` and `KconfigLimits` equals `DefaultLimits`.
 *
 * Kconfig writes a bool option set to "n" as an absent macro, so an absent option means off.
 *
 * One configuration per program: the inline functions below depend on the `CONFIG_` macros, so
 * every translation unit of one program must see the same values (ESP-IDF has one `sdkconfig.h` per
 * build; each host test is its own program). No driver or library header includes this one.
 */
#ifndef BNO08X_KCONFIG_HPP
#define BNO08X_KCONFIG_HPP

#ifdef __has_include
#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif
#endif

#include "bno08x.hpp"

#ifndef CONFIG_BNO08X_RX_CARGO
/** Largest cargo received, in bytes, when Kconfig does not set it. */
#define CONFIG_BNO08X_RX_CARGO 1024
#endif
#ifndef CONFIG_BNO08X_TX_CARGO
/** Largest cargo sent, in bytes, when Kconfig does not set it. */
#define CONFIG_BNO08X_TX_CARGO 128
#endif
#ifndef CONFIG_BNO08X_OP_QUEUE
/** Pending operations when Kconfig does not set it. */
#define CONFIG_BNO08X_OP_QUEUE 8
#endif
#ifndef CONFIG_BNO08X_EVENT_QUEUE
/** Queued events when Kconfig does not set it. */
#define CONFIG_BNO08X_EVENT_QUEUE 32
#endif
#ifndef CONFIG_BNO08X_STARTUP_TIMEOUT_MS
/** Start-up wait limit in milliseconds when Kconfig does not set it. */
#define CONFIG_BNO08X_STARTUP_TIMEOUT_MS 500
#endif
#ifndef CONFIG_BNO08X_OP_TIMEOUT_MS
/** Operation limit in milliseconds when Kconfig does not set it. */
#define CONFIG_BNO08X_OP_TIMEOUT_MS 1000
#endif

namespace bno08x {

/** @brief The buffer sizes menuconfig selected; pass it as the second template argument of
 * `Bno08x`. */
struct KconfigLimits {
  static constexpr std::size_t kRxCargo = CONFIG_BNO08X_RX_CARGO;       ///< Largest cargo received.
  static constexpr std::size_t kTxCargo = CONFIG_BNO08X_TX_CARGO;       ///< Largest cargo sent.
  static constexpr std::size_t kOpQueue = CONFIG_BNO08X_OP_QUEUE;       ///< Pending operations.
  static constexpr std::size_t kEventQueue = CONFIG_BNO08X_EVENT_QUEUE; ///< Queued events.
};

/**
 * @brief The configuration menuconfig selected, on top of the driver defaults.
 * @return `Config{}` with the chip variant, the I2C address, the two timeouts and the strictness
 * options taken from Kconfig.
 */
constexpr Config KconfigDefaults() noexcept {
  Config c{};
#if defined(CONFIG_BNO08X_VARIANT_BNO080)
  c.variant = Variant::Bno080;
#elif defined(CONFIG_BNO08X_VARIANT_BNO086)
  c.variant = Variant::Bno086;
#else
  c.variant = Variant::Bno085;
#endif
#ifdef CONFIG_BNO08X_I2C_ADDR_HIGH
  c.i2c_address = reg::kI2cAddrHigh;
#endif
#ifdef CONFIG_BNO08X_NOT_STRICT_VARIANT
  c.strict_variant = false;
#endif
#ifdef CONFIG_BNO08X_NO_AUTO_RESTORE
  c.auto_restore = false;
#endif
#ifdef CONFIG_BNO08X_SET_FEATURE_CONFIRM
  c.set_feature_confirm = true;
#endif
  c.startup_timeout_ms = CONFIG_BNO08X_STARTUP_TIMEOUT_MS;
  c.op_timeout_ms = CONFIG_BNO08X_OP_TIMEOUT_MS;
  return c;
}

} // namespace bno08x

#endif // BNO08X_KCONFIG_HPP
