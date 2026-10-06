# Delimited-Continuation Shape Sweep

This directory contains the reset/shift exercise sweep: a matrix of
delimited-continuation shapes run as standalone Koskari programs, each
verified against the value the Danvy/Filinski reduction gives. It is a
diagnostic tool, invoked explicitly like the native tests; pytest does
not collect it, and the phased suite checks the same semantics through
`tests/reproducers/` instead.

## Why subprocess isolation

Each case runs in its own subprocess. A dynchain fault corrupts state
for whatever runs next in the same process, so a broken shift surfaces
as an unrelated later construct failing (for example, a try block
reporting "CLEAR_TRY: top is not a TRY region"). An in-process harness
misattributes the fault and reads as if the shape passed.

## Running

The sweep needs a built extension and runs against whichever
interpreter invokes it:

```bash
# From project root, after a build
.tox/py/bin/python tests/sweep/delimited_sweep.py
```

## Shape families

| Group | Coverage |
|-------|----------|
| stack | operand-stack discipline around capture and resume |
| position | cross-frame shifts (callee-hosted, two deep, abandonment) |
| remote | k escaping its reset, invoked post-unwind |
| reentrant | shifts firing inside invoked continuations; multi-shot; amb x amb |
| enclosing | operands pending in the enclosing call around the reset |
| invoke | k invocation patterns (abandon, twice, loops) |
| extent | try, dyn-wind, parameterize interleaved with the region |

Case names are referenced by the design notes under
`design/interpreter/`.
