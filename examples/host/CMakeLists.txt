# =============================================================================
# BNO08x driver: host examples (any PC, no hardware)
# =============================================================================
# Each example runs the driver against the hub model the host tests use (tests/model), over the
# transport its name says.
#
#   cmake -S examples/host -B build-example
#   cmake --build build-example && ./build-example/i2c_quickstart
#
# The host tests also build this folder and run every example (CTest bno08x_example_<name>), so they
# always compile against the current headers.
# =============================================================================
cmake_minimum_required(VERSION 3.16)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  project(bno08x_host_examples LANGUAGES CXX)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. bno08x)
endif()

# ISO C++20, also when this folder is built on its own (handbook KB-CPP20-001).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(BNO08X_HOST_EXAMPLES i2c_quickstart spi_gyro_rv uart_shtp uart_rvc calibration tare batching
  frs_orientation metadata_dump step_counter fault_tolerance)
set(BNO08X_HOST_EXAMPLE_TARGETS "")
foreach(name ${BNO08X_HOST_EXAMPLES})
  add_executable(bno08x_example_${name} ${name}.cpp)
  target_link_libraries(bno08x_example_${name} PRIVATE bno08x::bno08x)
  target_include_directories(bno08x_example_${name} PRIVATE ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../../tests)
  target_compile_features(bno08x_example_${name} PRIVATE cxx_std_20)
  target_compile_options(bno08x_example_${name} PRIVATE -fno-exceptions -fno-rtti -Wall -Wextra -Wpedantic
    -pedantic-errors)
  list(APPEND BNO08X_HOST_EXAMPLE_TARGETS bno08x_example_${name})
endforeach()
set(BNO08X_HOST_EXAMPLE_TARGETS ${BNO08X_HOST_EXAMPLE_TARGETS} PARENT_SCOPE)
