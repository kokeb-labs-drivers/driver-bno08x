# =============================================================================
# Fuzz targets (docs/testing.md, "Fuzz tests")
# =============================================================================
# Each tests/fuzz/fuzz_<name>.cpp defines LLVMFuzzerTestOneInput() and is built twice:
#
#   fuzz_replay_<name>   any compiler; a CTest that replays tests/fuzz/corpus/fuzz_<name>/
#   fuzz_<name>          Clang with -DBNO08X_BUILD_FUZZERS=ON: libFuzzer with ASan and UBSan
#
#   cmake -S . -B build-fuzz -DCMAKE_CXX_COMPILER=clang++ -DBNO08X_BUILD_TESTS=ON -DBNO08X_BUILD_FUZZERS=ON
#   cmake --build build-fuzz --target fuzz_bno08x
#   ./build-fuzz/tests/fuzz/fuzz_bno08x -max_total_time=60 tests/fuzz/corpus/fuzz_bno08x
# =============================================================================

file(GLOB BNO08X_FUZZ_TARGETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/fuzz_*.cpp)
foreach(src ${BNO08X_FUZZ_TARGETS})
  get_filename_component(name ${src} NAME_WE)

  add_executable(fuzz_replay_${name} ${src} replay_main.cpp)
  bno08x_test_target(fuzz_replay_${name})
  target_include_directories(fuzz_replay_${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
  add_test(NAME bno08x_${name}_corpus
    COMMAND fuzz_replay_${name} ${CMAKE_CURRENT_SOURCE_DIR}/corpus/${name})

  if(BNO08X_BUILD_FUZZERS)
    add_executable(${name} ${src})
    bno08x_test_target(${name})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_compile_options(${name} PRIVATE -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all -g -O1)
    target_link_options(${name} PRIVATE -fsanitize=fuzzer,address,undefined)
  endif()
endforeach()
