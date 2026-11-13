/**
 * @file frs_orientation.cpp
 * @brief Set the system orientation, then read the stored record back.
 */
#include <cinttypes>
#include <cstdint>

#include "host_common.hpp"

int main() {
  rig::Rig<rig::I2cFull> r;
  if (!r.Start(ex::DefaultConfig())) {
    return 1;
  }
  // The system orientation record is four Q30 words: x, y, z, w. This is the identity rotation.
  const std::array<uint32_t, 4> identity{0, 0, 0, 0x40000000U};
  bool ok = ex::Finish(r, r.drv.WriteFrs(0x2D3E, identity), "write system orientation record");
  static bno08x::FrsBuffer buffer; // must outlive the operation
  ok = ex::Finish(r, r.drv.ReadFrs(0x2D3E, buffer), "read system orientation record") && ok;
  static_cast<void>(std::printf("%zu words, w = 0x%08" PRIX32 "\n", buffer.length,
                                static_cast<uint32_t>(buffer.words[3])));
  // For a rotation that applies at once and is not stored, use SetReorientation() instead.
  bno08x::Quat14 q{};
  q.w = 16384;
  ok = ex::Finish(r, r.drv.SetReorientation(q), "set orientation") && ok;
  return ok ? 0 : 1;
}
