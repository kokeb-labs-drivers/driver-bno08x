/**
 * @file test_support.hpp
 * @brief Helpers shared by the host tests: configurations, operation waits and report builders.
 */
#ifndef BNO08X_TEST_SUPPORT_HPP
#define BNO08X_TEST_SUPPORT_HPP

#include <bit>
#include <initializer_list>
#include <string_view>

#include "bno08x.hpp"
#include "model/bno08x_rig.hpp"
#include "test_harness.hpp"

namespace ts {

using namespace bno08x;
using hubmodel::Bytes;

inline Config Cfg(Variant v = Variant::Bno085) {
  Config c{};
  c.variant = v;
  return c;
}

/// Runs the operation to its `OpDone` and returns it; `ok` is false when the call or the wait
/// failed.
template <typename R>
inline bool Run(R& r, const Result<OpId>& res, Event& done, uint32_t ms = 3000) {
  if (!res.ok()) {
    done = Event{};
    done.error = res.error;
    return false;
  }
  return r.Await(res.value, done, ms);
}

/// The `OpDone` error of an operation, `Error::Timeout` when it never finished.
template <typename R>
inline Error Err(R& r, const Result<OpId>& res, uint32_t ms = 3000) {
  Event e{};
  if (!res.ok()) {
    return res.error;
  }
  return r.Await(res.value, e, ms) ? e.error : Error::Timeout;
}

inline Bytes B(std::initializer_list<uint8_t> v) {
  return {v};
}

/// The bytes of a text; a character is converted bit for bit, never used as a number.
inline Bytes AsBytes(std::string_view s) {
  Bytes out;
  for (const char c : s) {
    out.push_back(std::bit_cast<uint8_t>(c));
  }
  return out;
}

/// A sensor report: ID, sequence, status/delay bytes and the payload.
inline Bytes Report(uint8_t id, uint8_t seq, uint8_t status, uint8_t delay_lo,
                    std::initializer_list<uint8_t> data) {
  Bytes b{id, seq, status, delay_lo};
  b.insert(b.end(), data.begin(), data.end());
  return b;
}

inline Bytes Base(uint32_t ticks) {
  return Bytes{0xFB, static_cast<uint8_t>(ticks), static_cast<uint8_t>(ticks >> 8U),
               static_cast<uint8_t>(ticks >> 16U), static_cast<uint8_t>(ticks >> 24U)};
}

inline Bytes Cat(const Bytes& a, const Bytes& b) {
  Bytes r = a;
  r.insert(r.end(), b.begin(), b.end());
  return r;
}

inline FeatureConfig Feat(uint32_t interval_us, uint32_t batch_us = 0, bool wake = false) {
  FeatureConfig f{};
  f.interval_us = interval_us;
  f.batch_us = batch_us;
  f.wake = wake;
  return f;
}

} // namespace ts

#endif // BNO08X_TEST_SUPPORT_HPP
