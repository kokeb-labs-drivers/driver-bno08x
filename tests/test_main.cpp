/**
 * @file test_main.cpp
 * @brief Runs every registered host test; the exit code is non-zero when one fails. An optional
 * argument runs only the tests whose name contains it.
 */
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "test_harness.hpp"

int main(int argc, char** argv) {
  ::ut::Registry& r = ::ut::GetRegistry();
  const char* filter = (argc > 1) ? argv[1] : nullptr;
  int32_t failed_cases = 0;
  int32_t ran = 0;
  for (int32_t i = 0; i < r.count; ++i) {
    if ((filter != nullptr) && (std::strstr(r.tests[i].name, filter) == nullptr)) {
      continue;
    }
    ++ran;
    const int32_t before = r.failures;
    static_cast<void>(std::printf("[ RUN  ] %s\n", r.tests[i].name));
    static_cast<void>(std::fflush(stdout)); // a crash still names its test
    r.tests[i].fn();
    const bool ok = r.failures == before;
    static_cast<void>(std::printf("[ %s ] %s\n", ok ? " OK " : "FAIL", r.tests[i].name));
    if (!ok) {
      ++failed_cases;
    }
  }
  static_cast<void>(std::printf("%" PRId32 " test cases, %" PRId32 " failed\n", ran, failed_cases));
  return ((failed_cases == 0) && (ran > 0)) ? 0 : 1;
}
