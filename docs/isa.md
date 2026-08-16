# ISA Codec

## Scope

`linx-model` ships a committed generated LinxISA 0.58.1 codec for `isa::Minst`.
The source of truth is:

- `isa/v0.58/linxisa-v0.58.json` from the LinxISA 0.58.1 projection, generated
  from the locked PTO-ISA/pto-spec release

The generated C++ tables are committed under:

- [`include/linx/model/isa/generated_tables.hpp`](../include/linx/model/isa/generated_tables.hpp)
- [`src/isa/generated_tables.cpp`](../src/isa/generated_tables.cpp)

This keeps the library build standalone. No runtime JSON loading is required.

## Minst Contract

`isa::Minst` is the single in-flight uop object for the simulator.

- `MinstPtr` is created in fetch
- `SimQueue<MinstPtr>` moves ownership through the pipeline
- decode populates form metadata plus canonical decoded fields
- later stages read typed summaries from the same object
- retire, flush, or DFX consumes the packet

The canonical field map is the lossless re-encode source. Typed summaries such
as `srcs`, `dsts`, `immediates`, `shift_amount`, `memory`, `is_branch`, and
`is_control` are derived views.

## Decode and Encode

The public APIs are declared in
[`include/linx/model/isa/codec.hpp`](../include/linx/model/isa/codec.hpp).

- `DecodeMinstPacked(bits, length_bits, out)` decodes a packed 16/32/48/64-bit
  instruction word
- `DecodeMinst(raw_lo, raw_hi, length_bits, out)` decodes split low/high words
  for 48-bit and 64-bit forms
- `EncodeMinst(inst)` encodes from `form_id + decoded_fields`
- `DisassembleProgram(image)` and `PrintDisassembly(os, image)` render
  executable program images back into assembly

Decoder behavior:

- matches on `mask` and `match`
- chooses the unique most-specific form by fixed-bit count
- validates field constraints
- populates `Minst` metadata and typed views
- exposes exactly 765 current LinxISA forms, including `B.FPATR` and
  `BSTART.ICALL`, and rejects retired `B.ARG`,
  `C.B.IOS`, `BSTART.FIXP`, and generic `BSTART.CUBE`/`BSTART.TMA` spellings

## PTO 0.58 Shared state

`emulator::SharedTileBank` models one core-private `S0..S255` bank. Its
destination write path preflights every selected PE before applying one atomic
descriptor-and-payload update. The first write fixes the allocation mask and
per-PE capacity; later subset writes are legal, while allocation expansion or
descriptor drift fails without partial effects. A zero PE mask is a strict
no-op, and uninitialized reads return no value without changing state. PE-mask
bits are architectural-order: bit 3 selects PE0 through bit 0 selecting PE3.

The machine-readable differential contract is committed at
`tests/fixtures/pto_v058_shared_state.json`. It fixes the TLSU state
transitions plus the TMOV, cooperative CUBE, and TGEMV Shared-operand policy so
QEMU, LinxCoreModel, and RTL validation can consume the same cases.

Encoder behavior:

- requires a valid form and the full canonical field set
- validates field ranges and per-form constraints
- produces the exact encoded word and length

## Dump and Assembly

Every decoded `Minst` supports:

- `Assemble()` for deterministic human-readable output
- `DumpFields(PacketDumpWriter&)` for structured logs and tests

The dump includes:

- raw encoded bits and instruction length
- form uid, mnemonic, asm template, encoding kind, group, and uop class tags
- stage and lifecycle state
- source, destination, immediate, shift, and memory summaries
- the full canonical decoded field map

## CLI Path

The standalone `linx_model_cli` target uses the same APIs:

- load a program image with `--bin`
- auto-detect ELF vs raw binary
- decode executable bytes into `Minst`
- print canonical assembly with `--disasm` or `--disasm-only`

## Regeneration

After updating the source JSON, regenerate the committed tables with:

```bash
cmake --build build --target gen-isa-codec
cmake --build build --target check-isa-codec
```

The generator validates the exact root PTO ISA 0.58.1 lock identity, source
commit/tree, catalog hashes/counts, and the generated 765/2661/3401/780
form/field/piece/constraint cardinalities before writing. `check-isa-codec`
also rejects stale committed output without modifying it.

Then rebuild and rerun tests:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```
