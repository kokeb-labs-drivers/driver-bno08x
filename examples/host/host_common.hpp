/**
 * @file host_common.hpp
 * @brief What the host examples share: a simulated hub on a chosen link, and a few print helpers.
 *
 * The hub model of tests/model stands in for the chip, so every example runs on a PC. A real
 * program replaces `rig::Rig<Port>` with its own port class (docs/porting.md) and calls `Poll()`
 * from its loop.
 */
#ifndef BNO08X_HOST_COMMON_HPP
#define BNO08X_HOST_COMMON_HPP

#include <cinttypes>
#include <cstdint>
#include <cstdio>

#include "bno08x.hpp"
#include "bno08x_euler.hpp"
#include "model/bno08x_rig.hpp"

namespace ex {

/// The recommended configuration for a BNO085.
inline bno08x::Config DefaultConfig() {
  return bno08x::Config{};
}

/// Waits for an operation to finish. Returns its outcome; prints the reason when it failed.
template <typename R>
inline bool Finish(R& rig, const bno08x::Result<bno08x::OpId>& op, const char* what) {
  if (!op.ok()) {
    static_cast<void>(std::printf("%s: refused: %s\n", what, bno08x::ToString(op.error)));
    return false;
  }
  bno08x::Event done{};
  if (!rig.Await(op.value, done)) {
    static_cast<void>(std::printf("%s: no answer\n", what));
    return false;
  }
  if (done.error != bno08x::Error::Ok) {
    static_cast<void>(std::printf("%s: failed: %s\n", what, bno08x::ToString(done.error)));
    return false;
  }
  static_cast<void>(std::printf("%s: ok\n", what));
  return true;
}

/// Prints the newest rotation vector as Euler angles in degrees.
template <typename R>
inline void PrintEuler(R& rig, bno08x::SensorId id) {
  const auto latest = rig.drv.Latest(id);
  if (!latest.ok()) {
    static_cast<void>(std::printf("no sample yet\n"));
    return;
  }
  const bno08x::Event& e = latest.value;
  const bno08x::EulerCentiDeg eu = bno08x::ToEulerCentiDeg(e.v[0], e.v[1], e.v[2], e.v[3]);
  static_cast<void>(std::printf("yaw %.2f pitch %.2f roll %.2f deg (accuracy %" PRId32 ")\n",
                                eu.yaw / 100.0, eu.pitch / 100.0, eu.roll / 100.0,
                                static_cast<int32_t>(e.accuracy)));
}

} // namespace ex

#endif // BNO08X_HOST_COMMON_HPP
