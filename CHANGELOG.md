# Changelog

All notable changes to this repository are listed here. Versions follow [Semantic Versioning](https://semver.org/).
Entries under **Breaking** need source changes in code that used the affected API; any of them calls for a major
version bump (a minor bump before 1.0).

## [Unreleased]

### Added

- BNO08x driver, first version: the BNO080, BNO085 and BNO086 smart sensor hubs through one `Bno08x<Port, Limits>`
  class over I2C, SPI, UART with SHTP framing and the stand-alone UART-RVC mode; non-blocking `Begin()` and
  `Poll()`; every sensor report the CEVA documents define as a typed event; calibration, tare, reorientation and
  flash records; a table of ambiguities with a switch for each; host examples for every transport and an ESP-IDF
  example. Not verified on hardware.
