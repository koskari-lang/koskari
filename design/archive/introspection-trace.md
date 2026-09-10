# Koskari Introspection and Trace Snapshot Design

**Status**: archived 2026-09-10, superseded. Specifies a TraceView / KosTraceView snapshot object that was never built; exception decoration in eval.c attaches __koskari_dynchain__, a list of frame dicts.

This document defines a read-only introspection mechanism for the Koskari
evaluator. The goal is to provide structured visibility into frames, dynamic
extents, and extent-local storage for debugging and exception reporting,
without allowing mutation or execution control.

This mechanism must preserve all execution invariants:

* Single evaluator (`KosContext`) with heap-resident dynamic chain
* No reliance on CPython frame stack for Koskari semantics
* Continuation semantics remain unchanged
* No mutation of evaluator state through introspection

Introspection is strictly for debugging, diagnostics, and trace generation.

---

## 1. Design Goals

We require:

1. A reified, inspect-only snapshot object representing evaluator state.
2. A unified mechanism usable by:

   * Exception trace reporting
   * Debug utilities
   * Explicit opcode capture
3. Strictly read-only semantics.
4. No ability to resume, inject, or modify execution.
5. Snapshot validity even if the VM continues executing.

This is not a general reflection API.

---

## 2. Core Concept: TraceView (Snapshot Object)

Introduce an internal structure:

```
KosTraceView
```

This object represents a frozen snapshot of evaluator state at a specific point
in time.

### Snapshot Semantics

The snapshot must:

* Clone relevant dynamic chain segments
* Capture frame metadata (code, pc, namespace)
* Optionally capture bounded stack previews
* Retain references safely
* Remain stable after VM resumes

This is not a live view of `KosContext`.

---

## 3. Storage Classes Reflected

The snapshot exposes Koskari's storage model:

1. Operand stack (optional preview only)
2. Frame storage (FAST slots, VAR bindings)
3. Extent-local storage (dynamic chain node state)

This reinforces the architectural principle:

> Stack is for computation; extents are for control state.

---

## 4. Python-Facing Structure

Expose a Python wrapper object:

```
TraceView
  .frames -> tuple[FrameView]
  .chain  -> tuple[DynNodeView]
```

### FrameView

Contains:

* Code object
* Program counter (pc)
* Decoded source position
* Namespace summary
* Optional locals preview

### DynNodeView

Contains:

* Node kind (FRAME, REGION, PROMPT, WIND, NS, CC)
* Extent-local storage summary
* Control metadata (break_pc, continue_pc, enabled flags, etc.)
* Prompt handler metadata
* Wind hook state
* Namespace restore metadata

All fields are read-only.

---

## 5. Proposed opcode: KOS_OP_CAPTURE_FRAME (not in opcodes.incl)

This section records a **design** for a possible future opcode. It is **not**
part of the live `KosOpCode` enum until added to `koskari/lib/opcodes.incl` and
implemented; [OPCODES.md](../OPCODES.md) documents only opcodes that exist there.

Proposed mnemonic:

```
KOS_OP_CAPTURE_FRAME
```

### Semantics

* Style: COUNT
* Stack effect: `[...] -> [..., traceview]`

Argument meaning:

* 0 → capture current frame only
* 1 → capture full frame chain
* N → capture N frames upward

Requirements:

* Does not alter evaluator state
* Produces stable snapshot object
* Cannot embed Python/C stack
* Cannot expose mutable VM internals

This opcode is debugging-only and may be gated behind build flags if desired.

If the opcode is added to the tree, follow the checklist in [OPCODES.md](../OPCODES.md) §1.10
(Maintaining this document: incl, evaluator, tests, documentation).

---

## 6. Exception Integration

When the VM produces a raise boundary event:

1. Capture a `TraceView` snapshot
2. Attach it to the Python exception object

   * e.g., `exc.__koskari_trace__`
3. Use it to format structured trace output

TraceView becomes the unified mechanism for traceback reporting.

---

## 7. Safety Rules

* No mutation of evaluator state
* No exposure of raw C structs
* No execution injection
* No continuation interaction
* Snapshot must survive VM continuation
* Respect dynamic boundary rules

---

## 8. Optional Verbosity Levels

Support configurable debug levels:

* Level 0: Structure only (frames + node kinds + pcs)
* Level 1: Include extent-local storage summaries
* Level 2: Include bounded operand stack and locals preview

Default should be minimal.

---

## 9. Non-Goals

* Not a full reflection API
* Not exposing mutable namespace
* Not replacing continuation capture
* Not exposing CPython stack
* Not enabling runtime program control

---

## 10. Architectural Rationale

This design:

* Preserves evaluator invariants
* Leverages the existing dynamic chain model
* Avoids conflating continuation capture with debugging
* Creates a single structured mechanism for:

  * Debug output
  * Traceback generation
  * IDE tooling

It formalizes the principle:

> The dynamic chain is the structured runtime spine of execution.

---

## Related Documentation

* [evaluator.md](evaluator.md) — Evaluation model and VM structures
* [execution-invariants.md](execution-invariants.md) — Execution model invariants
* [../OPCODES.md](../OPCODES.md) — Opcode reference for implemented opcodes (`koskari/lib/opcodes.incl`)
