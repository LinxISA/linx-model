# Documentation

This directory is the hub for `linx-model` documentation.

## Contents

- [Architecture](./architecture.md)
- [Testing](./testing.md)
- [Logging](./logging.md)

## Doxygen

The API reference is generated from public headers plus
[`doxygen-mainpage.dox`](./doxygen-mainpage.dox). Build it with:

```bash
cmake -S . -B build-docs -G Ninja -DLINX_MODEL_BUILD_DOCS=ON
cmake --build build-docs --target docs
```
