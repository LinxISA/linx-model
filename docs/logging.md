# Logging

## Format

Structured log lines use a fixed field order:

```text
[LEVEL] cycle=<n|?> module=<module-name> stage=<stage-name> | message
```

For `ERROR` and `FATAL`, a second line includes the source location.

Example:

```text
[INFO ] cycle=42 module=fetch stage=decode | packet={pc=16, valid=true, tag=7}
```

## Levels

- `TRACE`
- `DEBUG`
- `INFO`
- `WARN`
- `ERROR`
- `FATAL`

The minimum emitted level is controlled by `SimLogger::SetMinLevel()` or the
simulator CLI `--log-level` option.

## Module Logging

Inside a module, use the helper macros or `SimObject::Log()`:

```cpp
LOG_INFO("issue") << "packet=" << packet;
```

The runtime logger automatically attaches cycle, module name, and stage.

## Packet Dump

Packets should implement:

```cpp
void DumpFields(linx::model::PacketDumpWriter& writer) const;
```

The dumper supports:

- fundamental values
- streamable custom types
- packet classes with `DumpFields`
- `std::unique_ptr<T>` and `std::shared_ptr<T>` by printing the pointed object
  or `null`
