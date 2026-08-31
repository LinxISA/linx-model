# ISA Codec

## Scope

`linx-model` ships a committed generated LinxISA 0.58.5 codec for `isa::Minst`.
The source of truth is:

- `isa/v0.58/linxisa-v0.58.json` from the LinxISA 0.58.5 projection, generated
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
- exposes exactly 754 current LinxISA forms, including the `B.FPATR`
  `TransA`/`TransB` form and the `B.IOT`/`B.IOS` `PEMode`/`SizeCode` forms;
  rejects retired scalar branches `B.EQ`, `B.GE`, `B.GEU`, `B.LT`, `B.LTU`,
  `B.NE`, `B.NZ`, and `B.Z`, plus retired `B.ARG`,
  `C.B.IOS`, `BSTART.FIXP`, and generic `BSTART.CUBE`/`BSTART.TMA` spellings

## PTO 0.58 Shared state

`emulator::SharedTileBank` models one core-private `S0..S255` bank. Its
destination write path preflights every selected PE before applying one atomic
descriptor-and-payload update. The first write fixes the allocation mask and
per-PE capacity; later subset writes are legal, while allocation expansion or
descriptor drift fails without partial effects. `PEMode` values decode to the
fixed participation masks `0000`, `1000`, `0100`, `0010`, `0001`, `1100`,
`1110`, and `1111`; mode zero is a strict no-op. Uninitialized reads return no
value without changing state. `B.IOS` accepts `SizeCode` 1 through 12, while
`B.IOT` accepts 1 through 10.

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

The generator validates the exact root PTO ISA 0.58.5 lock identity, source
commit/tree, catalog hashes/counts, and the generated 754/2648/3380/815
form/field/piece/constraint cardinalities before writing. `check-isa-codec`
also rejects stale committed output without modifying it.

It additionally authenticates the complete authority bytes against the
immutable LinxISA v0.58.5 authority at `660c870ff241f9803d8060f00ed3684ffed6f06c`:
compiled catalog
`ba09fca626f27e3efc4496e763ca3fc1b326709270904664db41da4d54185bcc`, PTO
lock `8d7b9c7df563f230862891ac4f7da6bbd137a14f1ca8148bbb3685ce10382dc0`,
and release manifest
`aadbbb9a16b11bff97f10cfb0e98a9d538ea02def0ad19f9e14243788bffa916`.
Standalone builds must provide that checkout through
`LINXISA_AUTHORITY_ROOT`; missing authority is an error for generation and
freshness checks.

Then rebuild and rerun tests:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```
