/**
 * @file test_kconfig.cpp
 * @brief KconfigDefaults() and KconfigLimits: the defaults without Kconfig, and menuconfig values
 * when they are set.
 *
 * Built twice: without CONFIG_ macros (KCONFIG_SET undefined) and with the values an sdkconfig.h
 * would define (KCONFIG_SET=1, see tests/CMakeLists.txt).
 */
#include "bno08x_kconfig.hpp"
#include "test_harness.hpp"

#ifdef KCONFIG_SET
TEST_CASE("Kconfig values reach the Config and the limits") {
  constexpr bno08x::Config kC = bno08x::KconfigDefaults();
  CHECK_EQ(kC.variant, bno08x::Variant::Bno086);
  CHECK_EQ(kC.i2c_address, bno08x::reg::kI2cAddrHigh);
  CHECK(!kC.strict_variant);
  CHECK(!kC.auto_restore);
  CHECK(kC.set_feature_confirm);
  CHECK_EQ(kC.startup_timeout_ms, 250U);
  CHECK_EQ(kC.op_timeout_ms, 400U);
  CHECK(kC.Valid());
  static_assert(bno08x::LimitsLike<bno08x::KconfigLimits>);
  CHECK_EQ(bno08x::KconfigLimits::kRxCargo, 512U);
  CHECK_EQ(bno08x::KconfigLimits::kTxCargo, 64U);
  CHECK_EQ(bno08x::KconfigLimits::kOpQueue, 4U);
  CHECK_EQ(bno08x::KconfigLimits::kEventQueue, 16U);
}
#else
TEST_CASE("Without Kconfig, KconfigDefaults() equals Config{} and the limits equal the defaults") {
  constexpr bno08x::Config kC = bno08x::KconfigDefaults();
  constexpr bno08x::Config kD{};
  CHECK_EQ(kC.variant, kD.variant);
  CHECK_EQ(kC.i2c_address, kD.i2c_address);
  CHECK_EQ(kC.strict_variant, kD.strict_variant);
  CHECK_EQ(kC.auto_restore, kD.auto_restore);
  CHECK_EQ(kC.set_feature_confirm, kD.set_feature_confirm);
  CHECK_EQ(kC.startup_timeout_ms, kD.startup_timeout_ms);
  CHECK_EQ(kC.op_timeout_ms, kD.op_timeout_ms);
  static_assert(bno08x::LimitsLike<bno08x::KconfigLimits>);
  CHECK_EQ(bno08x::KconfigLimits::kRxCargo, bno08x::DefaultLimits::kRxCargo);
  CHECK_EQ(bno08x::KconfigLimits::kEventQueue, bno08x::DefaultLimits::kEventQueue);
}
#endif
