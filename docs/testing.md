# Testing and CI

How the driver is checked: host tests against an independent model of the hub on every transport, a fuzzer that
corrupts what the hub sends, compiled documentation, an install test, ESP32 and Cortex-M builds, static analysis
and measured quality gates, on every pull request.

## Host tests

The tests build the real driver against [`tests/model/bno08x_hub_model.hpp`](../tests/model/bno08x_hub_model.hpp), a
model of the hub written from the CEVA documents and not from the driver. The model speaks SHTP and SH-2 on
the four transports: it advertises, resets, accepts feature requests and produces reports, answers commands and
FRS requests, fragments cargoes, escapes UART frames, streams UART-RVC frames, and injects faults (bus errors,
dropped or delayed H_INTN, bad lengths, wrong sequence numbers, corrupted escapes, bad checksums, restarts in the
middle of an operation). Its `Knobs` struct has one switch for each reading of an ambiguity
([Ambiguities](ambiguities.md)), so each decision is shown to work and to fail when the hub reads the other way.
The tests build with the firmware's flags, `-fno-exceptions -fno-rtti`, and warnings as errors, on GCC and Clang.

```bash
cmake -S . -B build -DBNO08X_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

| File | Covers |
| --- | --- |
| [`tests/test_session.cpp`](../tests/test_session.cpp) | Start-up on every transport, the state machine, restarts and recovery |
| [`tests/test_i2c.cpp`](../tests/test_i2c.cpp), [`test_spi.cpp`](../tests/test_spi.cpp) | The synchronous transports: H_INTN gating, WAKE, header-then-rest reads, padding |
| [`tests/test_uart_shtp.cpp`](../tests/test_uart_shtp.cpp), [`test_rvc.cpp`](../tests/test_rvc.cpp) | Framing, escapes, BSQ and BSN tokens, the 100 us gap, the RVC frames and their checksum |
| [`tests/test_shtp_frames.cpp`](../tests/test_shtp_frames.cpp) | Every framing rule V1 to V16 and its counter |
| [`tests/test_sensors.cpp`](../tests/test_sensors.cpp), [`test_timestamps.cpp`](../tests/test_timestamps.cpp) | Report layouts and Q points, batching, timestamps and wrap-around |
| [`tests/test_commands.cpp`](../tests/test_commands.cpp), [`test_control.cpp`](../tests/test_control.cpp), [`test_frs.cpp`](../tests/test_frs.cpp) | Commands, calibration, tare, FRS read and write |
| [`tests/test_variants.cpp`](../tests/test_variants.cpp), [`test_conformance.cpp`](../tests/test_conformance.cpp) | The chip differences and the conformance cases of the ambiguities |
| [`tests/test_errors.cpp`](../tests/test_errors.cpp) | The base `Error` values are 0 to 9; every chip-specific value is returned by some call; invalid input |
| [`tests/test_units.cpp`](../tests/test_units.cpp), [`test_version.cpp`](../tests/test_version.cpp), [`test_kconfig.cpp`](../tests/test_kconfig.cpp) | Unit helpers and the Euler conversion, the version getters, `KconfigDefaults()` |
| [`tests/compile_fail/`](../tests/compile_fail/not_a_port.cpp) | Code that must not compile, each for its `EXPECT:` diagnostic: a class that is not a port, a missing hook, a hook without `noexcept` or with a narrowed parameter, a wrong optional hook, a `Limits` that is too small |
| [`tests/fuzz/`](../tests/fuzz/fuzz_bno08x.cpp) | The fuzz target; CTest replays its corpus (`bno08x_fuzz_bno08x_corpus`), see below |
| [`examples/host/`](../examples/host/README.md) | Every host example, built with the test flags and run (`bno08x_example_<name>`) |
| [`tests/check_install.cmake`](../tests/check_install.cmake) | Installs into a scratch prefix and builds [`tests/install_consumer/`](../tests/install_consumer/CMakeLists.txt) with `find_package(bno08x)` |
| [`tests/check_doxygen.cmake`](../tests/check_doxygen.cmake) | Doxygen with `_config/Doxyfile` and warnings as errors, when `doxygen` is installed |

### The model stands alone

The model does not include the driver's definition headers. Its constants (report IDs, command codes, field
offsets, timing) are its own, taken from the documents with their sections, so a wrong constant in the driver cannot
hide behind the same wrong constant in the model. When a test fails, check the section the model names, and fix
the one that is wrong. Every behaviour a bench run shows becomes a knob of the model and a test that fails on the
code before the fix.

## Fuzz tests

Everything the driver reads comes from the hub over a link a noisy cable, a wrong part or a broken hub controls.
[`tests/fuzz/fuzz_bno08x.cpp`](../tests/fuzz/fuzz_bno08x.cpp) lets libFuzzer choose a transport, a variant and a
configuration, then a script of driver calls, and on every transfer what the link does: pass it to the model, flip
bits, replace it with the fuzzer's bytes or fail it. After every call it checks the driver's promises (F1 to F11) and
aborts, naming the line, when one breaks:

- a failed call returns an `Error` the enum defines and a value-initialised result (F1);
- an operation is accepted only in `Ready` (never on UART-RVC), except a stop request (switch-off, `Reset()`,
  `HubSleep()`), which after `Begin()` is never `NotInitialized` and is `Busy` only while the queue holds at least
  `kOpQueue - 3` stops; `InvalidArgument` sends nothing (F2);
- every accepted operation yields exactly one `OpDone`, never for an unknown or finished id (F3);
- the events between two polls never exceed the queue plus its `Dropped` marker, and each has a defined kind and
  sensor (F4, F5);
- the `Stats` counters never decrease (F6);
- `Faulted` is left only by `Begin()` or `HardReset()` (F7);
- `Latest()` and `Feature()` never fail for a defined sensor once ready and never read out of bounds (F8);
- the Euler conversion of any quaternion stays in range (F9);
- an FRS read never reports more words than the buffer holds (F10);
- everything works with a clock that starts just below the 32-bit wrap (F11);
- on SPI, PS0/WAKE is high at every NRST edge and until the first H_INTN after it (F12);
- on UART-SHTP the model may drop Buffer Status Notifications, and writes still go out (F13).

```bash
# libFuzzer needs Clang (on macOS, Homebrew's LLVM)
cmake -S . -B build-fuzz -DCMAKE_CXX_COMPILER=clang++ -DBNO08X_BUILD_TESTS=ON -DBNO08X_BUILD_FUZZERS=ON
cmake --build build-fuzz --target fuzz_bno08x
./build-fuzz/tests/fuzz/fuzz_bno08x -max_total_time=120 tests/fuzz/corpus/fuzz_bno08x
```

The same file also builds without libFuzzer as `fuzz_replay_fuzz_bno08x`, and CTest replays every input in
[`tests/fuzz/corpus/fuzz_bno08x/`](../tests/fuzz/corpus/fuzz_bno08x) on GCC and Clang
(`bno08x_fuzz_bno08x_corpus`): the seeds for every transport, and every input that ever broke a promise (name it
`regress-*`), so a fixed bug stays fixed. CI fuzzes for 120 s on every pull request, from the seeds plus a corpus it
keeps between runs (job `Host quality / fuzz`).

## Quality gates

The `Host quality` job measures the library code, `inc/` and `src/` only (tests and examples are left out), and
fails when a value is over the limits of this repository's assurance level, `high` (set in
[`.kokeb/handbook.yml`](../.kokeb/handbook.yml), defined by the handbook's ADR-0020):

| Gate | Tool and how it is measured | Limit at `high` |
| --- | --- | --- |
| Complexity | lizard 1.24.1, modified cyclomatic complexity (`-m`: a `switch` counts once) per function | CCN at most 15 |
| Function length | lizard, lines of code per function | at most 60 |
| Parameters | lizard, per function | at most 6 |
| Line coverage | GCC 14 `--coverage -O0`, every CTest, gcovr 8.7 `--merge-lines` (a template's lines count once across its instances), `--exclude-unreachable-branches --exclude-throw-branches` | at least 85 % |
| Branch coverage | same run | at least 75 % |
| MC/DC | GCC `-fcondition-coverage`, same run | measured, reported in the step summary |
| Sanitizers | Clang, `-fsanitize=address,undefined -fno-sanitize-recover=all`, every CTest | no report |
| Fuzzing | libFuzzer with ASan and UBSan, 120 s per target | no broken promise |

The reports are in the run's `host-quality-reports` artifact and its step summary. To run the gates locally:

```bash
# Coverage (GCC 14 or later), then the complexity limits
cmake -S . -B build-cov -DCMAKE_CXX_COMPILER=g++-14 -DBNO08X_BUILD_TESTS=ON \
  -DCMAKE_CXX_FLAGS="--coverage -O0 -fcondition-coverage"
cmake --build build-cov && ctest --test-dir build-cov
gcovr --root . --filter inc/ --filter src/ --merge-lines --exclude-unreachable-branches --txt build-cov
lizard -m -C 15 -L 60 -a 6 inc src

# Sanitizers (Clang)
cmake -S . -B build-san -DCMAKE_CXX_COMPILER=clang++ -DBNO08X_BUILD_TESTS=ON \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -O1 -g"
cmake --build build-san && ctest --test-dir build-san --output-on-failure
```

The fuzz gate runs locally as shown in [Fuzz tests](#fuzz-tests).

clang-tidy 20 ([`_config/.clang-tidy`](../_config/.clang-tidy): bugprone, CERT, clang-analyzer,
cppcoreguidelines, modernize, performance, portability and readability, each disabled check with its reason) and
cppcheck (strict) run in the `C++ lint` and `Repository checks` jobs and fail on any finding.

The `MISRA` job ([`ci-misra.yml`](../.github/workflows/ci-misra.yml)) runs the shared `ru-misra` workflow of
ci-tools at level `high`: it traces a host build and the Cortex-M build (`bno08x_optimised_build`) of every
translation unit, checks them against MISRA C++:2023 and the handbook's C++20 profile (the CodeQL coding-standards
and profile packs, plus the validated clang-tidy and cppcheck checks), and reports each finding against the
handbook's compliance matrix and deviation records. A finding is fixed in the code; a deliberate exception names
its deviation on the line before it (`// codeql::misra_deviation_next_line(MD-CPP-009)` above the `.ipp` include
of `inc/bno08x.hpp`). Until the repository is public the job runs with `open_source_prerelease: true`, the
declaration that the code is released under the licence in `LICENSE` and is to be published.

## Mutation testing

Each review round changes the code at every fix site (a removed condition, a shifted bound, a skipped call) and
runs the host tests and the fuzz corpus against each change; a change that nothing detects needs a test. These
changes are equivalent: no input tells them from the driver, for the reason given.

| Change | Why it cannot be seen |
| --- | --- |
| `emitSensor` without its 4-byte guard | `ApplyReportLengths` refuses every length below 4, so no report shorter than its header reaches it |
| NRST release failure without `enterFaulted()` (`state_ = Faulted` only) | while NRST is low only stops can be queued, and the hub refuses every transfer |
| `restartFailed()` without clearing `restart_event_pending_` or `restarting_` | both are read only after `Begin()` or `HardReset()`, which set them again |
| a completed silent restore request recording `desired_` | it carries `desired_` as it was when queued; `desired_` can change before it ends only by a switch-off queued behind it (which blocks the record), by a request of the caller ahead of it (impossible: the restore skips a sensor with one queued) or by `Begin()` and a restart (which end it with an error, not `Ok`) |
| a bad-channel header of length exactly 4 opened as an empty discard instead of dropped | an empty discard completes at once and ends the same way: nothing kept, the cargo closed, counted once |
| the read limit taken when it equals the read buffer (`>` against `>=`) | both keep the same value, the buffer size |
| the FRS buffer pointer kept after an FRS operation ends | each FRS operation sets it when it is queued, only one can be queued (`Busy` otherwise), and a response is used only while an FRS operation is the head on the wire |
| auto-restore allowed to fill the last queue slot too | a stop then displaces the newest restore request instead of taking the free slot; that request is queued again (`restore_next_` rewinds), so the hub ends the same |

`recover()` without NRST keeps a Buffer Status Notification already received: the hub granted it and keeps running,
so it holds; clearing it would only cost one more query.

## Documentation snippets

Every C++ block on this site is included from [`tests/docs/`](../tests/docs/my_port.hpp), which the host build
compiles (target `bno08x_doc_snippets`) with the driver instantiated over the documented port. A change to
the API that breaks a page breaks the build first. The README's example is a generated copy of the same file;
`kokeb-docs check` fails when it drifts.

## CI workflows

Everything runs from [`ci.yml`](../.github/workflows/ci.yml), which calls one workflow file per check and ends in
the `CI gate` job, the single required check (`CI gate / gate`).

| Job | What it checks |
| --- | --- |
| Host tests | GCC, then Clang (one job), build and run every test above, then build and run `examples/host` on its own |
| Host quality | The gates above: complexity, coverage, sanitizers, fuzzing |
| Cross-compile | `arm-none-eabi-g++` for Cortex-M0+, M4F and M7F with the strict flags, warnings as errors (nothing linked) |
| C++ lint | clang-format and clang-tidy over the sources |
| MISRA | MISRA C++:2023 and the C++20 profile on traced host and Cortex-M builds, at level `high` (see [Quality gates](#quality-gates)) |
| Docs | Strict site build (`_config/mkdocs.yml`), figure and generated-region drift, Doxygen (`_config/Doxyfile`), offline link check, Lighthouse |
| Repository checks | One job: DCO on pull requests, the org standard, Markdown and YAML lint, the [Kokeb Labs handbook](https://github.com/kokeb-labs/handbook) rules for `.kokeb/handbook.yml` (by rule ID), template drift (warns only) and cppcheck over the driver sources; a second job runs the handbook's source checks (include guards, bus requirements, raw access) |
| CI gate | Passes only if every job above passed: the one required check |

The **ESP32 examples** job builds every app in `examples/esp32/app_config.yml`, for each listed ESP-IDF version and
build type, with the repository root as the driver's component, and lints the example sources. The gate needs it
too.

The workflows call the shared, versioned workflows of
[kokeb-labs/ci-tools](https://github.com/kokeb-labs/ci-tools) at `@v1`.

## On hardware

Nothing in this repository has been run on a BNO08x. The model is written from the documents, so a hub that behaves
differently is a bench result. The ESP32 example ([Examples](examples.md)) is the bench test: tilt the board and
confirm the angles follow and `GetStats()` stays clean. [Ambiguities](ambiguities.md) lists what to ask the vendor
and what to try first. Record the board and the result in the pull request's bench-status section.
