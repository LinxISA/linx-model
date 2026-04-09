# linx-model

`linx-model` is a small C++20 framework for queue-based, cycle-accurate
simulation. The core contract is that inter-module communication happens
through named [`SimQueue`](./include/linx/model/sim_queue.hpp) ports, while
`Work()` computes combinational behavior and `Xfer()` advances registered
state.

## Overview

- `SimQueue<T>` models latency-aware, cycle-visible FIFO movement for value
  payloads, `std::unique_ptr<T>`, and `std::shared_ptr<T>`.
- `isa::Minst` is the single in-flight uop payload. Fetch allocates
  `MinstPtr`, decode populates generated LinxISA v0.4 fields, and retire/flush
  or DFX consumes the same object.
- `ProgramImage` plus the ELF/raw loader and ISA disassembler let the model CLI
  load a binary, decode instructions, and print assembly directly from the
  generated codec.
- `Module<Derived, PortT>` provides recursive, named `input` / `output` /
  `inner` ports plus parent-owned queue wiring and event-driven module dispatch.
- `SimSystem` owns the global cycle counter, logger, validation, and the top
  level `Work()` then `Xfer()` schedule.
- Validation checks enforce named ports, leaf-module I/O contracts, queue-role
  correctness, and top-level SimQueue-based composition.

## Build

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

The `ctest` suite includes committed LLVM-built ELF fixtures under
[`tests/fixtures/llvm_elf/`](./tests/fixtures/llvm_elf). Regenerate them from a
superproject checkout with:

```bash
python3 tests/fixtures/llvm_elf/regenerate_llvm_elf_fixtures.py --root "$(pwd)"
```

The repo also builds a standalone CLI:

```bash
./build/linx_model_cli --bin path/to/program.elf --disasm-only
./build/linx_model_cli --bin path/to/program.bin --raw-base 0x1000 --disasm-only
```

To run the sanitizer configuration:

```bash
cmake -S . -B build-sanitize -G Ninja -DLINX_MODEL_ENABLE_SANITIZERS=ON
cmake --build build-sanitize
ctest --test-dir build-sanitize --output-on-failure
```

## Repository Layout

- [`include/`](./include) public headers
- [`src/`](./src) non-template runtime support
- [`tests/unit/`](./tests/unit) unit tests
- [`tests/system/`](./tests/system) system tests
- [`tests/checks/`](./tests/checks) validation and contract checks
- [`docs/`](./docs) architecture, testing, and logging documentation
- [`tools/ci/`](./tools/ci) local and CI helper scripts

## Documentation

- [`docs/index.md`](./docs/index.md) documentation hub
- [`docs/architecture.md`](./docs/architecture.md) execution model and queue
  semantics
- [`docs/testing.md`](./docs/testing.md) UT / ST / checks / sanitizers
- [`docs/logging.md`](./docs/logging.md) structured log format and packet dump
- [`docs/isa.md`](./docs/isa.md) generated LinxISA v0.4 codec and `Minst`
  packet contract

Doxygen can be generated locally with:

```bash
cmake -S . -B build-docs -G Ninja -DLINX_MODEL_BUILD_DOCS=ON
cmake --build build-docs --target docs
```

To regenerate the committed LinxISA codec tables after updating the source JSON:

```bash
cmake --build build --target gen-isa-codec
```
