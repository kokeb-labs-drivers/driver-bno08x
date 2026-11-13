#===============================================================================
# BNO08X driver: root CMake
#===============================================================================
# One file, two build systems:
#
# - ESP-IDF (ESP_PLATFORM): this folder is an ESP-IDF component. Point
#   EXTRA_COMPONENT_DIRS at it, check it out under components/, or add it as a
#   component-manager git dependency. menuconfig reads _config/Kconfig.
# - Any other CMake build (host, STM32, RP2040, nRF, ...): one INTERFACE target,
#   bno08x, consumed as bno08x::bno08x both through add_subdirectory() and
#   through find_package(bno08x) after install.
#
#   cmake -S . -B build -DBNO08X_BUILD_TESTS=ON
#   cmake --build build && ctest --test-dir build --output-on-failure
#===============================================================================
cmake_minimum_required(VERSION 3.16)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/bno08x_build_settings.cmake)

#===============================================================================
# ESP-IDF component
#===============================================================================
if(ESP_PLATFORM)
    idf_component_register(
        SRCS ${BNO08X_SOURCE_FILES}
        INCLUDE_DIRS ${BNO08X_PUBLIC_INCLUDE_DIRS}
        REQUIRES ${BNO08X_IDF_REQUIRES}
        KCONFIG ${CMAKE_CURRENT_LIST_DIR}/_config/Kconfig
    )
    if(BNO08X_SOURCE_FILES)
        target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
    else()
        target_compile_features(${COMPONENT_LIB} INTERFACE cxx_std_20)
    endif()
    return()
endif()

project(bno08x_driver
    VERSION ${BNO08X_VERSION}
    DESCRIPTION "C++20 driver for the CEVA BNO080, BNO085 and BNO086 smart sensor hubs (SH-2 over SHTP on I2C, SPI and UART, and the UART-RVC mode)."
    LANGUAGES CXX)

include(GNUInstallDirs)

# ISO C++20 for every target this project builds (tests, fuzzers, examples), never the GNU dialect
# that CMake picks by default (handbook KB-CPP-001, KB-CPP20-001). CXX_EXTENSIONS does not travel
# through an INTERFACE target, so each folder that builds a target sets it as well.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The installed layout keeps the source tree's inc/ + src/ pair, because bno08x.hpp includes
# "../src/bno08x.ipp" relative to itself:
#   <prefix>/include/bno08x/inc/*.hpp, bno08x_version.h
#   <prefix>/include/bno08x/src/bno08x.ipp
set(BNO08X_INSTALL_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}/bno08x/inc")

#===============================================================================
# Target
#===============================================================================
add_library(${BNO08X_TARGET_NAME} INTERFACE)
target_include_directories(${BNO08X_TARGET_NAME}
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/inc>
        $<BUILD_INTERFACE:${BNO08X_VERSION_HEADER_DIR}>
        $<INSTALL_INTERFACE:${BNO08X_INSTALL_INCLUDEDIR}>
)
target_compile_features(${BNO08X_TARGET_NAME} INTERFACE cxx_std_20)

# bno08x::bno08x everywhere: the alias for add_subdirectory(), EXPORT_NAME for the installed package.
add_library(bno08x::bno08x ALIAS ${BNO08X_TARGET_NAME})
set_target_properties(${BNO08X_TARGET_NAME} PROPERTIES EXPORT_NAME bno08x)

option(BNO08X_ENABLE_WARNINGS "Add -Wall -Wextra -Wpedantic to every target that links bno08x::bno08x" OFF)
if(BNO08X_ENABLE_WARNINGS)
    target_compile_options(${BNO08X_TARGET_NAME} INTERFACE -Wall -Wextra -Wpedantic)
endif()

#===============================================================================
# Host tests, documentation snippets, install check (tests/)
#===============================================================================
option(BNO08X_BUILD_TESTS "Build the host tests in tests/" OFF)
option(BNO08X_BUILD_FUZZERS "Also build the libFuzzer targets in tests/fuzz/ (Clang only)" OFF)
if(BNO08X_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

#===============================================================================
# Install and package export
#===============================================================================
include(CMakePackageConfigHelpers)

install(TARGETS ${BNO08X_TARGET_NAME} EXPORT bno08xTargets
    INCLUDES DESTINATION ${BNO08X_INSTALL_INCLUDEDIR})

install(DIRECTORY inc/ DESTINATION ${BNO08X_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h")
install(FILES "${BNO08X_VERSION_HEADER}" DESTINATION ${BNO08X_INSTALL_INCLUDEDIR})
install(DIRECTORY src/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/bno08x/src
    FILES_MATCHING PATTERN "*.ipp")

install(EXPORT bno08xTargets
    FILE bno08xTargets.cmake
    NAMESPACE bno08x::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/bno08x)

configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/cmake/bno08xConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/bno08xConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/bno08x)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/bno08xConfigVersion.cmake"
    VERSION ${BNO08X_VERSION}
    COMPATIBILITY SameMajorVersion
    ARCH_INDEPENDENT)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/bno08xConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/bno08xConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/bno08x)
