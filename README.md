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
  `MinstPtr`, decode populates generated LinxISA 0.58.7 fields, and retire/flush
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
- [`docs/isa.md`](./docs/isa.md) generated LinxISA 0.58.7 codec and `Minst`
  packet contract

Doxygen can be generated locally with:

```bash
cmake -S . -B build-docs -G Ninja -DLINX_MODEL_BUILD_DOCS=ON
cmake --build build-docs --target docs
```

To regenerate the committed LinxISA codec tables after updating the source JSON:

```bash
cmake --build build --target gen-isa-codec
cmake --build build --target check-isa-codec
```

The PTO 0.59.1 compatibility API `isa::DecodePtoScopedScalar` accepts a
stream-order 64-bit carrier and returns the explicit shareability value and
decoded inner form. `PtoScopedScalar::CodeBytes` identifies the full carrier
size for callers. The default Minst executor still selects the active v0.58
profile. `ReferenceExecutorOptions::enable_pto_scoped_scalar` explicitly
enables the candidate reference path for the 60 immediate and PC-relative
single-access forms without writeback. Each actual memory request records the
outer TPC, complete carrier, scope, address, size, direction, value, and result.
Successful execution retires eight bytes. Alignment and missing-memory load
faults preserve the outer PC, destination state, and retirement count. This
single-context reference path records scope for comparison. Cross-agent
visibility and coherence remain a platform integration responsibility.
The unit suite checks all 705 released carrier witnesses.

The CLI exposes the same capability for raw images and ELF `PT_LOAD` images.

```bash
./build/linx_model_cli --engine ref --enable-pto-v0591-scoped --bin program.elf
```

The current loader admits ELF load segments without interpreting
`.note.pto.isa`. Release orchestration must therefore verify the exact 0.59.1
note before invoking this opt-in path.
With `LINXISA_AUTHORITY_ROOT` set to a checkout containing the imported release,
run `cmake --build build --target check-pto-scoped-codec` to check the generated
bindings, witnesses, and reference-runtime form table.

Generation fails closed unless the development lock matches the exact PTO ISA
0.58.7 golden, ABI, projection, source commit/tree, catalog hashes/counts, and
the 794-form codec cardinality. The freshness target regenerates into a temporary
directory and byte-compares every committed output.

For a standalone checkout, configure with an immutable LinxISA authority:

```bash
cmake -S . -B build -G Ninja \
  -DLINXISA_AUTHORITY_ROOT=/path/to/linx-isa-at-1deac9de1d2303baa04448e8a5c3520fc9b56ec0
cmake --build build --target check-isa-codec
```

Hosted CI checks out that exact LinxISA commit and runs the freshness target.
The generator authenticates the complete catalog, lock, and release-manifest
bytes before interpreting their declared identities, so same-count semantic or
encoding mutations fail closed.
