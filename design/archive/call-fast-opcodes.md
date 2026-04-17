# `CALL_FAST*` — Fused Fast-Local Call Opcodes

**OBSOLETE.** This document described a packed-encoding approach
(slot/count in a single 16-bit arg). The implementation uses EXTRA opcode
style instead (CALL_VAR\* / CALL_FAST\* with operand rows in the extra table).
Archived for historical reference.

---

**Status: Design proposal — not a mandate; open for discussion.**

This document describes a potential optimization: fusing a fast-local slot
load with a call opcode into a single fused opcode, reducing dispatch count
and stack pressure on the common hot path of calling a fast-local callable.

**See also:** [../OPCODES.md](../OPCODES.md) §17 (call opcodes), [vector-call.md](vector-call.md)
(vectorcall integration), [evaluator.md](evaluator.md) (evaluation loop),
[../interpreter/README.md](../interpreter/README.md) (Compiler) and [SPECIALS.md](../SPECIALS.md).


## Motivation

Function calls are a dominant hot path. The compiler currently emits at least
two instructions for the common pattern of calling a fast-local callable:

```
FAST  <slot>          ; load callable from fast local
<push args>
CALL  <nargs>         ; invoke callable
```

Each instruction costs a dispatch. The `FAST` instruction also pushes the
callable transiently onto the value stack, increasing stack traffic and stack
pressure.

A `CALL_FAST` family of opcodes fuses the fast-local load with the call,
reducing the sequence to a single dispatched instruction and eliminating the
transient callable slot from the stack.

This proposal is complementary to [vector-call.md](vector-call.md), which
eliminates transient argument tuple allocation. Both optimizations can be
applied independently or together.


## Goals

- Reduce dispatch count for the common pattern "call a fast-local callable."
- Keep the existing `CALL`, `CALL_NAMES`, and `CALL_STAR` calling conventions
  intact; these new opcodes are an optimization layer, not a replacement.
- Preserve the existing call/await flattening behavior: only the call result
  may be auto-awaited, as per current `CALL*` semantics.
- Keep bytecode compact and simple to disassemble.
- Provide a clean fallback path for large slot indices or large argument counts.

## Non-goals

- Not a general-purpose "call anything" opcode; specifically for fast-local
  callables.
- Not a replacement for `CALL*`; used only when preconditions hold.
- No change to keyword argument representation, star-arg representation, or
  tail-call semantics beyond skipping the callable-on-stack step.


## Proposed opcodes

Add fused-call variants parallel to the existing call opcodes:

- `KOS_OP_CALL_FAST` — positional calls
- `KOS_OP_CALL_FAST_NAMES` — positional + keyword-names tuple on stack
- `KOS_OP_CALL_FAST_STAR` — `*args` / `**kwargs` calls

Recommended tail-call equivalents (not required for an initial slice):

- `KOS_OP_TAIL_CALL_FAST`
- `KOS_OP_TAIL_CALL_FAST_NAMES`
- `KOS_OP_TAIL_CALL_FAST_STAR`

The rollout order suggested at the bottom of this document stages these into
four increments.


## Instruction encoding

Koskari instructions are `(opcode:uint16, arg:uint16)`. For `CALL_FAST*` the
`arg` field is split:

```
arg = (slot << 8) | count
slot  = (arg >> 8) & 0xFF        ; fast-local slot index, 0..255
count = arg & 0xFF                ; positional arg count, 0..255 (or flags for _STAR)
```

This yields a slot range of 0–255 and a positional-count (or flags) range of
0–255.

### Opcode style

A new style `KOSTYLE_FAST_CALL` (or reuse `KOSTYLE_PAIR_COUNT`) should be
registered in `KosOpStyle` so the disassembler can decode both subfields. The
existing styles do not capture this two-field split cleanly.

### Fallback policy

If either:

- the callee's fast-slot index exceeds 255, or
- the positional argument count exceeds 255,

then the compiler must fall back to the unfused sequence (`FAST <slot16>`
followed by the regular call opcode). No fused encoding for wide slots or
wide counts is proposed here; wide fallback is always the unfused sequence.

### `CALL_FAST_STAR` encoding

`CALL_STAR` uses `arg` for bitflags (0–3), not positional count. For the
fused star variant, the `arg` field is split as:

```
slot  = (arg >> 8) & 0xFF
flags = arg & 0xFF              ; STAR bitflags, 0..3; same semantics as CALL_STAR arg
```

This is **Option A** from the discussion section and is recommended. A slot
range of 0–255 covers the same domain as the other fused opcodes, and the
flags field only needs two bits.

An alternative is to omit `CALL_FAST_STAR` from the initial proposal and only
add it if profiling shows it is hot (Option B / deferred). See "Open
questions."


## Stack conventions

The fused opcodes reuse the exact stack layout of the corresponding non-fused
opcodes, except the callable is **not** present on the stack before the
opcode executes. The callable is loaded from the fast-local slot at execution
time.

### `KOS_OP_CALL_FAST`

- **Before:** `[..., a_0, ..., a_{count-1}]`
- **After:** `[..., result]`
- Callable: `frame.fast[slot]`

### `KOS_OP_CALL_FAST_NAMES`

Matches `CALL_NAMES` shape except callable is not on stack.

- **Before:** `[..., pos_0, ..., pos_{count-1}, kw_val_0, ..., kw_val_{M-1}, names_tuple]`
- **After:** `[..., result]`
- `names_tuple` is a tuple of `M` keyword names; `M = len(names_tuple)`.

### `KOS_OP_CALL_FAST_STAR`

Matches `CALL_STAR` shape except callable is not on stack.

- **Before:**
  - flags = 3: `[..., pos, kwd]`
  - flags = 2: `[..., kwd]`
  - flags = 1: `[..., pos]`
  - flags = 0: `[...]`
- **After:** `[..., result]`
- `pos` is an iterable/tuple of positional args (normalized to tuple as in
  `CALL_STAR`); `kwd` is a dict of keyword args.


## Semantics

### Shared call behavior

For all `CALL_FAST*` opcodes:

1. Decode `slot` and `count` (or `flags`) from `arg`.
2. Load `callable = frame.fast[slot]`.
3. Validate callable (or let `kos_callable_call` / Python raise `TypeError`).
4. Consume arguments from the value stack according to the corresponding
   non-fused call-shape rules.
5. Invoke via the same internal call helper used by the matching `CALL*`
   opcode (`kos_callable_call` or `kos_callable_vcall` if the vectorcall
   integration from [vector-call.md](vector-call.md) is in place).
6. Preserve existing in-loop execution behavior for Koskari callables (new
   frame push) and existing auto-await behavior for Python call results.

### Tail-call variants

`TAIL_CALL_FAST*` mirror existing `TAIL_CALL*` behavior but source the
callable from a fast slot. Key invariants to preserve:

- Frame-replacement semantics for Koskari functions and expressions.
- Any existing rule about clearing or forbidding pending native continuations
  on tail-call.
- Call-result auto-await semantics remain unchanged.

### Error conditions

| Condition | Handling |
|-----------|----------|
| `slot >= frame.nfast` | Debug: assert; release: `InternalError` (VM corruption) |
| Slot unbound or cleared | Match existing `FAST` load semantics (NameError/UnboundLocalError equivalent) |
| Stack underflow for encoded `count` | Debug: assert; release: VM corruption |
| `names_tuple` not a tuple (`_NAMES`) | TypeError, matching `CALL_NAMES` |
| `flags` out of 0–3 range (`_STAR`) | Debug: assert; release: VM corruption |


## Disassembly

The disassembler should render each fused opcode with both subfields shown:

```
CALL_FAST       slot=<n> argc=<m>
CALL_FAST_NAMES slot=<n> argc=<m>
CALL_FAST_STAR  slot=<n> flags=<f>
TAIL_CALL_FAST       slot=<n> argc=<m>
TAIL_CALL_FAST_NAMES slot=<n> argc=<m>
TAIL_CALL_FAST_STAR  slot=<n> flags=<f>
```


## Opcode style registration

`opcodes.incl` and `bytecode.h` must register the new style. Options:

- Add `KOSTYLE_FAST_CALL` with hi8 = slot, lo8 = argc/flags.
- Reuse or extend `KOSTYLE_PAIR_COUNT` if its semantics are compatible.

Registering a named style keeps the disassembler, verifier, and any static
analysis tools from having to special-case these opcodes.


## Compiler lowering

### When to emit

The compiler may emit `CALL_FAST*` only when all of the following hold:

- The callee expression resolved to a `FAST` slot at the current call site.
- `slot <= 255`.
- For `CALL_FAST` and `CALL_FAST_NAMES`: positional arg count `<= 255`.
- The call site matches one of the supported call shapes.

Guarding emission behind a feature flag (e.g. `--enable-fused-calls`) is
recommended until tests stabilize.

### Rewrite rules (peephole / lowering)

Applied after normal instruction emission or as a peephole pass:

```
FAST slot; <push N pos args>; CALL N
  →  <push N pos args>; CALL_FAST (slot<<8 | N)
  (when slot <= 255 and N <= 255)

FAST slot; <push N pos>; <push M kw vals>; <push names>; CALL_NAMES N
  →  <push N pos>; <push M kw vals>; <push names>; CALL_FAST_NAMES (slot<<8 | N)
  (when slot <= 255 and N <= 255)

FAST slot; <push star payload>; CALL_STAR flags
  →  <push star payload>; CALL_FAST_STAR (slot<<8 | flags)
  (when slot <= 255)
```

Tail-call variants follow the same patterns replacing `CALL*` with
`TAIL_CALL*` and `CALL_FAST*` with `TAIL_CALL_FAST*`.

### Interaction with CALL_ATTR*

`CALL_FAST_ATTR_NAMES` and `CALL_VAR_ATTR_NAMES` are distinct from the
`CALL_FAST*` opcodes described above:

- **CALL_FAST_ATTR_NAMES** loads obj from a fast slot and invokes `CALL_ATTR_NAMES`
  semantics. The *receiver* (obj) is from the slot; the callable is the resolved
  attribute `obj.name`. Stack: `[pos..., kw...]` — obj is not on the stack.
- **CALL_VAR_ATTR_NAMES** does the same but loads obj from a VAR (name lookup).
- **CALL_FAST** (above) fuses callable-from-slot with `CALL`. The *callable*
  is in the fast slot; obj is irrelevant. Stack: `[arg_0, ..., arg_n]`.

The compiler does not emit these opcodes; they are available for manual bytecode
construction (e.g. `build_expr` / `build_code`) or future lowering passes.

### Implementation: backward-scan heuristic in the compression pass

When implemented, the fused-call lowering should live in the `compress` method
of the compiler (or in a dedicated pass inserted between `compress` and
`convert_fast` in `complete`), using the same linear-scan idiom already
established by `convert_fast` for the VAR→FAST translation.

The heuristic is a **backward scan from each `CALL*` op** across the already-
squished instruction buffer. When the current instruction is any of `CALL`,
`CALL_NAMES`, `CALL_STAR`, `TAIL_CALL`, `TAIL_CALL_NAMES`, or `TAIL_CALL_STAR`,
look back through the buffer past all intervening push-like instructions to
find the instruction that loaded the callable. If that instruction is `FAST`
(i.e. the VAR→FAST translation has already run, or we are scanning
post-`convert_fast`), and the slot index and arg count both fit in 8 bits,
delete the `FAST` instruction from the buffer and replace the `CALL*` with the
corresponding `CALL_FAST*` carrying the packed `(slot << 8) | count` arg.

Because `convert_fast` performs a single forward pass that rewrites VAR to
FAST in place, the fused-call pass should run **after** `convert_fast` so that
the callable-load instruction has already been resolved to a `FAST` slot before
the backward scan inspects it. The call order in `complete` would therefore be:

```
compress(instructions)        # existing: constant-fold, dead-code, jump elision
→ convert_fast(...)           # existing: VAR → FAST for locals/closures
→ fuse_fast_calls(...)        # new: FAST + CALL* → CALL_FAST*
→ Code(...)
```

The backward scan must be conservative: any instruction whose stack effect
cannot be statically determined (branches, labels, or other opcodes that
alter control flow) should abort the backward walk and leave the sequence
unfused. In practice, the compiler already emits the callable load as the
first instruction in a call sequence and the argument pushes immediately after
it, so the look-back distance is bounded and predictable.


## Evaluator implementation sketch

### Shared fast-load helper

A shared inline or macro to retrieve and validate the callable from a fast
slot, reusing the existing `FAST` opcode slot-access logic:

```c
/* Pseudo-code: load callable from fast slot; returns NULL on error */
static inline PyObject *
load_fast_slot(KosDynFrame *frame, uint8_t slot)
{
    if (slot >= KosDynFrame_NFAST(frame)) {
        /* debug: assert(0); release: */
        PyErr_SetString(PyExc_SystemError, "CALL_FAST: slot out of range");
        return NULL;
    }
    PyObject *callable = KosDynFrame_FAST(frame, slot);
    if (! callable) {
        /* unbound; match FAST opcode behavior (NameError equivalent) */
        PyErr_SetString(PyExc_NameError, "fast local referenced before assignment");
        return NULL;
    }
    return callable;  /* borrowed */
}
```

### Opcode handler sketches

`OP_CALL_FAST`:

```c
uint8_t slot = (KosOp_ARG(insn) >> 8) & 0xFF;
uint8_t npos = KosOp_ARG(insn) & 0xFF;
PyObject *callable = load_fast_slot(frame_top, slot);
if (! callable) return -1;
/* Borrow args directly from stack via TailPtr (if vectorcall integration
   is in place) or PopTail, then call */
return do_fused_call(ctx, out_ev, callable, npos, NULL);
```

`OP_CALL_FAST_NAMES`:

```c
uint8_t slot = (KosOp_ARG(insn) >> 8) & 0xFF;
uint8_t npos = KosOp_ARG(insn) & 0xFF;
PyObject *callable = load_fast_slot(frame_top, slot);
if (! callable) return -1;
PyObject *kwnames = KosStackBlock_PopRef(stack);  /* pop TOS */
/* ... validate kwnames is tuple, compute nkw, call ... */
return do_fused_call_names(ctx, out_ev, callable, npos, kwnames);
```

`OP_CALL_FAST_STAR`:

```c
uint8_t slot  = (KosOp_ARG(insn) >> 8) & 0xFF;
uint8_t flags = KosOp_ARG(insn) & 0xFF;
PyObject *callable = load_fast_slot(frame_top, slot);
if (! callable) return -1;
/* Pop pos/kwd per flags, normalize pos to tuple, call */
return do_fused_call_star(ctx, out_ev, callable, flags);
```

Tail variants route into `kos_callable_tail_call` (or `kos_callable_tail_vcall`
if vector-call integration is in place).

### Interaction with vectorcall integration

If the vectorcall changes from [vector-call.md](vector-call.md) are already
applied, `do_fused_call` can use `kos_callable_vcall` + `TailPtr` + `Drop`
instead of `PopTail`. That combination eliminates both the transient callable
slot (from fused opcodes) and the transient argument tuple (from vectorcall),
making the combined optimization larger than either alone.


## Runtime policy decisions

These should be recorded as explicit invariants in
[execution-invariants.md](execution-invariants.md) when implemented:

1. **Slot validity:** Debug builds assert `slot < nfast`; release builds raise
   `SystemError` (VM corruption). This matches the existing policy for other
   internal invariant violations.

2. **Unbound slot:** Treat as `NameError` / `UnboundLocalError`, matching the
   behavior of `KOS_OP_FAST` on an unbound slot.

3. **Stack underflow:** Debug assert; release raises `SystemError`. The
   compiler is responsible for ensuring correct stack depth.

4. **Wide slot or wide count:** No wide encoding; always fall back to the
   unfused sequence.


## Testing plan

Tests should parallel existing `CALL*` tests, verifying that fused and
unfused sequences produce identical results.

### Suggested categories

- **Positional:** 0, 1, many args
- **Names:** 0 keywords, 1 keyword, many keywords
- **Star:** flags 0–3 with representative `*args` / `**kwargs` payloads
- **Callable types:** Koskari function, Koskari expression, Python function,
  Python async function (auto-await behavior), non-callable (TypeError)
- **Slot errors:** unbound slot, slot index at boundary (255), slot out of
  range (if testable)
- **Tail-call variants:** same set as above, verifying frame-replacement
  semantics match `TAIL_CALL*`

Tests live under `tests/` and must be invocable via `tox`.


## Rollout strategy

1. Implement `KOS_OP_CALL_FAST` (highest ROI, simplest encoding; positional
   calls only).
2. Add `KOS_OP_TAIL_CALL_FAST`.
3. Add `KOS_OP_CALL_FAST_NAMES` and `KOS_OP_TAIL_CALL_FAST_NAMES`.
4. Add `KOS_OP_CALL_FAST_STAR` and `KOS_OP_TAIL_CALL_FAST_STAR` last (or
   defer if profiling shows they are not hot).
5. Add compiler peephole / lowering rules, guarded behind a feature flag,
   until tests stabilize.


## Open questions

1. **`CALL_FAST_STAR` in the first iteration?** If yes, Option A encoding
   (`slot:8 | flags:8`) is straightforward. If not, defer until profiling
   motivates it.

2. **Slot-out-of-range in release builds:** `SystemError` (VM corruption) vs.
   `NameError` (user-visible). The current proposal recommends `SystemError`
   because this condition can only arise from a compiler bug.

3. **Wide encoding?** A "wide" `CALL_FAST` for slots or counts > 255 could
   use a separate opcode or a prefix byte. The current proposal says always
   fall back to `FAST + CALL`; revisit if wide slots become common.

4. **Interaction with `CALL_ATTR*`:** The `CALL_ATTR` family performs attribute
   lookup + call. A `CALL_ATTR_FAST` where the *object* is a fast local (not
   the callable itself) is a different pattern and is out of scope here.

5. **Interaction with vectorcall integration:** The two optimizations compose
   cleanly. It is worth deciding whether to implement them together or in
   sequence.
