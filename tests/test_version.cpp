/**
 * @file test_version.cpp
 * @brief The version getters follow the generated bno08x_version.h, so they cannot drift from
 * CMake.
 *
 * Built twice by tests/CMakeLists.txt: against the real generated header (the getters must equal
 * the CMake project version, passed as EXPECTED_*), and against a probe header generated from the
 * same template with version 9.8.7 (a getter that returns a literal instead of reading the header
 * fails there).
 */
#include <cstdio>
#include <cstring>

#include "bno08x_types.hpp"

static_assert(bno08x::GetDriverVersionMajor() == EXPECTED_MAJOR,
              "major version differs from CMake");
static_assert(bno08x::GetDriverVersionMinor() == EXPECTED_MINOR,
              "minor version differs from CMake");
static_assert(bno08x::GetDriverVersionPatch() == EXPECTED_PATCH,
              "patch version differs from CMake");

int main() {
  if (std::strcmp(bno08x::GetDriverVersion(), EXPECTED_STRING) != 0) {
    std::printf("version string %s, expected %s\n", bno08x::GetDriverVersion(), EXPECTED_STRING);
    return 1;
  }
  std::printf("version %s: PASS\n", bno08x::GetDriverVersion());
  return 0;
}
