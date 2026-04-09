# Testing

## Test Layers

`linx-model` uses lightweight executable tests through `CTest`.

- Unit tests in [`tests/unit/`](../tests/unit)
- System tests in [`tests/system/`](../tests/system)
- Contract and validation checks in [`tests/checks/`](../tests/checks)

## Local Commands

Configure and run the default test suite:

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

Run individual labels:

```bash
ctest --test-dir build --output-on-failure -L unit
ctest --test-dir build --output-on-failure -L system
ctest --test-dir build --output-on-failure -L checks
```

## Sanitizers

The sanitizer configuration enables AddressSanitizer, LeakSanitizer, and
UndefinedBehaviorSanitizer in one build.

```bash
cmake -S . -B build-sanitize -G Ninja -DLINX_MODEL_ENABLE_SANITIZERS=ON
cmake --build build-sanitize
ctest --test-dir build-sanitize --output-on-failure
```

This is the supported leak-detection path. There is no separate valgrind flow.

## Validation Coverage

The checks target exercises:

- missing `input` / `output` declarations on leaf modules
- missing signal names or descriptions
- queue-role conflicts
- top-level objects that are not declared as SimQueue-based

These checks run through `ValidateModel()` and are wired into CI.
