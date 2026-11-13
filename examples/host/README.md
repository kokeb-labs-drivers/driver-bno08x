# BNO08x host examples

The driver on your PC, with no hardware. Each program runs the real driver over a port from
[`tests/model/bno08x_rig.hpp`](../../tests/model/bno08x_rig.hpp), which connects it to the hub model the host tests
use (an independent model of the SH-2 hub). The programs differ in the transport and in what they show.

## Build and run

From the repository root, with CMake 3.16+ and a C++20 compiler:

```bash
cmake -S examples/host -B build-example
cmake --build build-example
./build-example/bno08x_example_i2c_quickstart
```

## The programs

| Program | What it shows |
| --- | --- |
| `bno08x_example_i2c_quickstart` | Start a BNO085 on I2C, ask for the rotation vector at 50 Hz and print the angles |
| `bno08x_example_spi_gyro_rv` | SPI with H_INTN and WAKE: the gyro-integrated rotation vector on its own channel |
| `bno08x_example_uart_shtp` | SHTP over UART: the driver frames, escapes and paces the bytes itself |
| `bno08x_example_uart_rvc` | UART-RVC: the hub streams 19-byte frames, the driver only listens |
| `bno08x_example_calibration` | Choose which calibrations run, read the choice back and save the result |
| `bno08x_example_tare` | Tare the heading now, then make it survive a restart |
| `bno08x_example_batching` | Let the hub collect samples and deliver them together; flush to get them early |
| `bno08x_example_frs_orientation` | Set the system orientation, then read the stored record back |
| `bno08x_example_metadata_dump` | Read a sensor's metadata: ranges, resolution, fastest and slowest rates |
| `bno08x_example_step_counter` | The 16-bit step count wraps; compare counts with a difference |
| `bno08x_example_fault_tolerance` | A failing bus and a silent hub: the driver reports, counts and recovers |

The model produces arbitrary data, so your angles differ from the documentation. The host tests build this folder
too and run every program (CTest `bno08x_example_<name>`), so CI fails if one stops compiling or a call fails.

## Files

| File | What it is |
| --- | --- |
| [`host_common.hpp`](host_common.hpp) | The shared setup: a default `Config`, the finish check, an Euler printer |
| [`CMakeLists.txt`](CMakeLists.txt) | Standalone project; adds the driver with `add_subdirectory(../..)` |
