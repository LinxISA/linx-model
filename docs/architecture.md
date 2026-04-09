# Architecture

## Core Model

`linx-model` is a `SimQueue`-based simulation framework. Modules do not pass
mutable state to each other directly. Instead, they communicate through named
queue ports owned and wired by parent modules.

The execution contract is:

1. `SimSystem::Step()` calls `Work()` on every registered module.
2. `SimSystem::Step()` then calls `Xfer()` on every registered module.
3. `SimQueue::Work()` decrements pending delays and promotes ready writes to the
   readable side.

This enforces a cycle boundary between producer and consumer visibility.

Within each `Module`, `WorkSelf()` is event-driven:

- the module tracks the visible-side epoch of every `input` and `inner` queue
- `WorkSelf()` only runs when at least one observed input queue changes
- inactive modules still advance owned queue timing so delayed packets can wake
  downstream modules in a later cycle
- `XferSelf()` only runs for modules whose `WorkSelf()` executed in the same
  cycle

## Queue Semantics

`SimQueue<T>` supports:

- value payloads such as `bool`, `int`, enums, and small structs
- `std::unique_ptr<T>` for ownership transfer without object copies
- `std::shared_ptr<T>` for shared-object scenarios

`latency == N` means a write becomes readable after exactly `N` calls to
`Work()`. `latency == 0` is still synchronized to the next `Work()` boundary.

## Module Contract

Every queue-wired module derives from `Module<Derived, PortT>`.

- `DescribeInput(name, description)` declares an input signal.
- `DescribeOutput(name, description)` declares an output signal.
- `DescribeInner(name, description)` declares an internal registered link.
- `Bind*()` attaches a real queue instance to the named port.
- `CreateOwnedQueue()` allocates parent-owned queues with stable addresses.

Leaf or outward-facing modules must declare at least one input and one output
unless they explicitly call `SetRequireIOContract(false)`.

## Port Metadata

Each declared port carries `PortInfo`:

- `index`
- `direction`
- `name`
- `description`

This metadata serves three purposes:

- documents signal intent in headers and generated API docs
- supports validation checks for missing names and descriptions
- makes pipe-view and debug output stable across modules

## Work and Xfer Rules

Inside `Work()`, modules are expected to:

- read current visible values from `input` and `inner` queues
- perform combinational logic only
- optionally push results into `output` queues

`Xfer()` is reserved for explicit registered-state transfer local to the module.
Cross-module timing should stay queue-driven.
