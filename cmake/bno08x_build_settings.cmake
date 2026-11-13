#===============================================================================
# BNO08X driver: build settings
#===============================================================================
# Read by the root CMakeLists.txt in both of its modes (plain CMake and the
# ESP-IDF component): target name, version, include directories, sources and
# ESP-IDF requirements. This file is the single source
# of the version; tests/CMakeLists.txt checks every other copy against it.
#===============================================================================

include_guard(GLOBAL)

set(BNO08X_TARGET_NAME "bno08x")

#===============================================================================
# Version (single source of truth)
#===============================================================================
set(BNO08X_VERSION_MAJOR 0)
set(BNO08X_VERSION_MINOR 1)
set(BNO08X_VERSION_PATCH 0)
set(BNO08X_VERSION "${BNO08X_VERSION_MAJOR}.${BNO08X_VERSION_MINOR}.${BNO08X_VERSION_PATCH}")

#===============================================================================
# Generated version header (build tree, never the source tree)
#===============================================================================
set(BNO08X_VERSION_TEMPLATE "${CMAKE_CURRENT_LIST_DIR}/../inc/bno08x_version.h.in")
set(BNO08X_VERSION_HEADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/bno08x_generated")
set(BNO08X_VERSION_HEADER "${BNO08X_VERSION_HEADER_DIR}/bno08x_version.h")

# The directory must exist before ESP-IDF's idf_component_register() checks the include paths.
file(MAKE_DIRECTORY "${BNO08X_VERSION_HEADER_DIR}")
configure_file("${BNO08X_VERSION_TEMPLATE}" "${BNO08X_VERSION_HEADER}" @ONLY)

#===============================================================================
# Public include directories, sources, ESP-IDF requirements
#===============================================================================
set(BNO08X_PUBLIC_INCLUDE_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../inc"
    "${BNO08X_VERSION_HEADER_DIR}"
)

# Header-only: the template implementation is src/bno08x.ipp, included by inc/bno08x.hpp.
set(BNO08X_SOURCE_FILES)

set(BNO08X_IDF_REQUIRES)
