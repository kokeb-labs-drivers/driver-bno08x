/**
 * @file bno08x_euler.hpp
 * @brief Integer quaternion to Euler angle conversion (no floating point, no table).
 * @copyright Copyright (c) 2026 The driver-bno08x authors. SPDX-License-Identifier: MIT
 */
#ifndef BNO08X_EULER_HPP
#define BNO08X_EULER_HPP

#include <array>
#include <cstdint>

#include "bno08x_types.hpp"

namespace bno08x {

namespace detail {

/// arctan(2^-n) in 1/100 degree times 16 (fixed point with 4 extra bits), n = 0 to 13.
inline constexpr std::array<int32_t, 14> kAtanTable{
    {72000, 42504, 22458, 11400, 5722, 2864, 1432, 716, 358, 179, 90, 45, 22, 11}};

/// Integer square root, rounded down; bounded 32-step loop.
constexpr uint32_t Isqrt(uint64_t v) noexcept {
  uint64_t bit = uint64_t{1} << 62U;
  uint64_t res = 0;
  uint64_t rem = v;
  while (bit > rem) {
    bit >>= 2U;
  }
  while (bit != 0U) {
    if (rem >= (res + bit)) {
      rem -= res + bit;
      res = (res >> 1U) + bit;
    } else {
      res >>= 1U;
    }
    bit >>= 2U;
  }
  return static_cast<uint32_t>(res);
}

/// `v` shifted right by `n` bits, rounding toward zero (no shift of a signed value).
constexpr int64_t Asr(int64_t v, uint32_t n) noexcept {
  return (v >= 0) ? static_cast<int64_t>(static_cast<uint64_t>(v) >> n)
                  : -static_cast<int64_t>(static_cast<uint64_t>(-v) >> n);
}

/// atan2(y, x) in centidegrees by CORDIC (14 iterations), -18000 to 18000.
constexpr int32_t Atan2CentiDeg(int32_t y, int32_t x) noexcept {
  if ((x == 0) && (y == 0)) {
    return 0;
  }
  int64_t cx = x;
  int64_t cy = y;
  int32_t base = 0;
  if (cx < 0) {
    base = (cy >= 0) ? 18000 : -18000;
    cx = -cx;
    cy = -cy;
  }
  int32_t angle = 0; // 1/100 degree * 16
  for (uint32_t n = 0U; n < kAtanTable.size(); ++n) {
    const int64_t dx = Asr(cy, n);
    const int64_t dy = Asr(cx, n);
    if (cy > 0) {
      cx += dx;
      cy -= dy;
      angle += kAtanTable[n];
    } else {
      cx -= dx;
      cy += dy;
      angle -= kAtanTable[n];
    }
  }
  return base + ((angle + ((angle >= 0) ? 8 : -8)) / 16);
}

} // namespace detail

/**
 * @brief Euler angles from a Q14 unit quaternion, with integer arithmetic only.
 * @param i Quaternion i (x) component, Q14.
 * @param j Quaternion j (y) component, Q14.
 * @param k Quaternion k (z) component, Q14.
 * @param real Quaternion real (w) component, Q14.
 * @return Yaw, pitch and roll in centidegrees (accurate to a few centidegrees).
 */
[[nodiscard]] constexpr EulerCentiDeg ToEulerCentiDeg(int32_t i, int32_t j, int32_t k,
                                                      int32_t real) noexcept {
  const int64_t w = real;
  const int64_t x = i;
  const int64_t y = j;
  const int64_t z = k;
  EulerCentiDeg e{};
  e.roll =
      detail::Atan2CentiDeg(static_cast<int32_t>(2 * ((w * x) + (y * z)) / 16384),
                            static_cast<int32_t>(((w * w) - (x * x) - (y * y) + (z * z)) / 16384));
  e.yaw =
      detail::Atan2CentiDeg(static_cast<int32_t>(2 * ((w * z) + (x * y)) / 16384),
                            static_cast<int32_t>(((w * w) + (x * x) - (y * y) - (z * z)) / 16384));
  // sin(pitch) = 2 (w y - z x); pitch = atan2(s, sqrt(1 - s^2)).
  int64_t s = 2 * ((w * y) - (z * x)) / 16384;
  if (s > 16384) {
    s = 16384;
  } else if (s < -16384) {
    s = -16384;
  } else {
    // in range: nothing to clamp
  }
  const uint32_t c = detail::Isqrt(static_cast<uint64_t>((int64_t{16384} * 16384) - (s * s)));
  e.pitch = detail::Atan2CentiDeg(static_cast<int32_t>(s), static_cast<int32_t>(c));
  return e;
}

} // namespace bno08x

#endif // BNO08X_EULER_HPP
