# Koskari opcodes — reference

Authoritative technical reference for Koskari bytecode opcodes. The **opcode set**
and their order are defined in `koskari/lib/opcodes.incl`; any new opcode must be
added there first, then documented here. This document defines semantics and
invariants; the evaluator and compiler are expected to follow it.

**See also:** [interpreter/bytecode.md](interpreter/bytecode.md) (format, layout,
metadata tables), [interpreter/execution-invariants.md](interpreter/execution-invariants.md)
(execution model), [interpreter/bytecode-metadata-tables.md](interpreter/bytecode-metadata-tables.md)
(extents, jump locality), [interpreter/evaluator.md](interpreter/evaluator.md)
(evaluation loop), [FEATURES.md](FEATURES.md) (implementation status).


## 1. Overview and conventions

### 1.1 How to use this document

- **VM / evaluator implementers:** Per-opcode stack effect, invariants, and
  post-compile disassembly tables. Section 1.3 (global invariants) and each
  opcode's Invariants bullets are the **contract**: the evaluator must preserve
  them; violations are undefined behaviour (or asserted in debug builds).
- **Compiler writers:** Section 1.7 (pre-compile vs post-compile), **Compiler form**
  lines, and pre-compile list-of-pairs examples. Ensure emitted code satisfies
  the invariants documented per opcode.
- **Debugging / disassembly:** Disassembly table format (§1.2), arg view
  conventions, and `koskari.dis` for runtime disassembly of KosCode objects.

Section headings use stable anchors (e.g. `### KOS_OP_JUMP` → `#kos_op_jump`) for
linking from design notes, PRs, or other docs.

### 1.2 Notation

- **Stack**: `[a, b, c]` with TOS (top of stack) on the right. "TOS" = top of
  stack; "below TOS" = second element, etc.
- **Stack effect**: Written as "Before → After" (e.g. `[..., callable, a, b] → [..., result]`).
- **Chain**: The dynamic chain (frames, prompts, regions, etc.) is described
  when an opcode pushes or pops nodes. See [interpreter/execution-invariants.md](interpreter/execution-invariants.md)
  and [interpreter/bytecode-metadata-tables.md](interpreter/bytecode-metadata-tables.md).

### 1.3 Glossary

- **pc**: Instruction index (program counter) into the code array.
- **TOS**: Top of stack; rightmost element in stack notation.
- **cpool**: Constant pool; the read-only array of constants in a KosCode (`cpool[i]`).
- **extent**: A statically described block (stored in the code object's extent
  table) with kind `KosExtentKind` (KOS_EXT_TRY, KOS_EXT_LOOP, KOS_EXT_FOREACH,
  KOS_EXT_PROMPT, KOS_EXT_NS, etc.), enter/exit pcs, and kind-specific targets;
  see bytecode-metadata-tables.md.
- **dynamic chain / chain node**: At runtime, the evaluator maintains a chain of
  nodes (`KosDynKind`: KOS_DYN_FRAME, KOS_DYN_REGION, KOS_DYN_NS, KOS_DYN_PROMPT,
  KOS_DYN_CC, KOS_DYN_WIND, etc.). PUSH_* opcodes push a node onto this chain;
  POP_* opcodes pop it. Each opcode section that pushes onto the chain states
  **Pushes:** the chain node type and, when relevant, the static extent kind.
- **pre-compile / post-compile**: Pre-compile = stream emitted by compiler (labels
  as strings, constants as values). Post-compile = final bytecode after finalize
  (labels → pcs, values → pool indices). §1.7 details the pipeline.
- **safety net**: LABEL and POSITION are normally stripped in post-compile; if
  retained, they are executed as a no-op (advance PC only) so the evaluator does
  not misbehave.

### 1.4 Disassembly tables

Example disassembly in this document is given as a **table** with five columns:

| Column | Meaning |
|--------|--------|
| **index** | Instruction index (pc). |
| **opcode** | Opcode name (e.g. `KOS_OP_CHECK_IN`). |
| **arg** | Raw argument value (uint16). |
| **arg view** | Decoded argument for readability (e.g. constant as `index (repr)`, branch as `N (-> N)`). |
| **comment** | Stack state before the instruction and/or control-flow or stack effect. |

Long comments (stack, branch conditions) may span the cell; keep index and opcode compact so the table stays scannable.

### 1.5 Global invariants

Unless specified otherwise for an opcode:

- Execution obeys the invariants in [interpreter/execution-invariants.md](interpreter/execution-invariants.md).
- Extent and location metadata are as in [interpreter/bytecode-metadata-tables.md](interpreter/bytecode-metadata-tables.md).
- **Jump locality**: Control-flow jump opcodes (JUMP, JUMP_FALSE, JUMP_TRUE,
  JUMP_AND, JUMP_OR, ITER_NEXT, CHECK_*) may only target a pc inside the current
  extent or at its `pop_pc` (leaving the block). See bytecode-metadata-tables.md
  § "Jump Legality and Locality".

The **Invariants** bullets per opcode (and this section) form the **contract**:
the evaluator must preserve them, and the compiler must produce code that
satisfies them. Violations are undefined behaviour (or may be asserted in debug
builds).

### 1.6 Implementation status

Implementation status is summarized in [interpreter/bytecode.md](interpreter/bytecode.md)
§ "Opcode implementation status" and in [FEATURES.md](FEATURES.md). In this
document, each opcode section notes: **Implemented**, **Stub** (unimplemented;
runtime error if executed), or **No runtime** (e.g. THEEND, compile-time only).
Status is maintained manually; when adding or changing opcodes, update both the
implementation and this document.

### 1.7 Compilation: pre-compile vs post-compile

The compiler emits a **pre-compile** stream of instructions. A **finalize** (or
"compress") pass then produces the **post-compile** bytecode stored in a
KosCode. Many opcodes have different argument meaning in each form.

**Pipeline**

1. **Pre-compile stream**: Pairs of (opcode, arg). Labels are string names.
   Constants and names may appear as literal values (e.g. `CONST "hello"`, `VAR
   "x"`). LABEL and POSITION may be present.
2. **Finalize**: Resolve string labels to instruction indices (pcs). Build the
   constant pool from in-line values and assign indices. Rewrite CONST, VAR,
   ATTR, and other name/key operands to pool indices. Optionally lower VAR to
   FAST for names that have a fixed slot. Derive extent and position tables.
   LABEL/POSITION may be rewritten to NOOP or retained with unused arg.
3. **Post-compile (bytecode)**: Fixed (op, arg) per instruction; branch args are
   pcs; constant/name args are pool indices or slot indices. This is what the
   evaluator executes and what disassembly tables in this document show.

**Argument mappings**

| Pre-compile arg        | Post-compile arg        | Opcodes (examples) |
|------------------------|-------------------------|---------------------|
| String label name      | pc (uint16)             | JUMP, JUMP_FALSE, JUMP_TRUE, JUMP_AND, JUMP_OR, ITER_NEXT, CHECK_*, SET_PROMPT_HANDLER, PUSH_LOOP, PUSH_FOREACH, PUSH_TRY, PUSH_FINALLY, PUSH_WIND, PUSH_REGION |
| Literal value          | Constant pool index     | CONST |
| Name (string/symbol)   | Constant pool index     | VAR, VAR_BIND, VAR_CLEAR, VAR_REF, VAR_STORE; ATTR*, CONST_ITEM* (key); INVOKE_K_VAR |
| Name → slot (optional) | Fast local index       | VAR may be lowered to FAST*; then arg is slot index |
| (unchanged)            | (unchanged)             | NOARG, COUNT, etc. |

**Compile-time only**: LABEL and POSITION are **omitted** in the post-compile
bytecode under normal circumstances. Finalize strips them and adjusts any branch
targets that pointed at a stripped instruction. Position data from POSITION is
recorded in the code object's position table only. If a LABEL or POSITION is
retained (e.g. by a different code path), it has a cursory no-op implementation
as a safety net; at runtime it advances PC only.

Where the pre form differs from the bytecode form, opcode sections below include
a **Compiler form** line. Disassembly tables are **post-compile** unless marked
otherwise.

### 1.8 Maintaining this document

When adding or changing an opcode:

1. **Add to `koskari/lib/opcodes.incl`** (name and style). The enum and order
   there are authoritative.
2. **Implement or stub** the handler in `koskari/lib/eval.c` (or mark as
   no-runtime for THEEND / compile-time for LABEL, POSITION).
3. **Add or update** the opcode's section in this document: category, stack
   effect, invariants, compiler form (if pre ≠ post), and at least one
   disassembly table (post-compile). For stubs, add **Intended** (design) and
   **Current** (stub/error) if helpful.
4. **Add or extend tests** that match the documented behaviour; consider adding a
   "Covered by …" line in the doc (e.g. `tests/lib/test_ceval.py`).


## 2. Opcode categories (index)

- [3. No-op and compile-time](#3-no-op-and-compile-time) — NOOP, LABEL, POSITION
- [4. Stack](#4-stack) — DUP, POP
- [5. Constants](#5-constants) — CONST
- [6. Mode control](#6-mode-control) — AWAIT, BREAK, CONTINUE, RAISE, RETURN, YIELD, YIELD_FROM
- [7. Iteration](#7-iteration) — ITER_NEXT, NEXT
- [8. Jumps](#8-jumps) — JUMP, JUMP_FALSE, JUMP_TRUE, JUMP_AND, JUMP_OR
- [9. Check jumps](#9-check-jumps) — CHECK_EQ, CHECK_GE, CHECK_GT, CHECK_LE, CHECK_LT, CHECK_NE, CHECK_IN, CHECK_NOT_IN, CHECK_IS, CHECK_IS_NOT, CHECK_IS_INSTANCE, CHECK_IS_SUBCLASS, CHECK_EXCEPTION
- [10. Introspection](#10-introspection) — GLOBALS, LOCALS
- [11. Exception handling](#11-exception-handling) — EXCEPTION, EXCEPTION_CLEAR, EXCEPTION_MATCH
- [12. Variables by name](#12-variables-by-name) — VAR, VAR_BIND, VAR_CLEAR, VAR_REF, VAR_STORE
- [13. Fast variables](#13-fast-variables) — FAST, FAST_BIND, FAST_CLEAR, FAST_REF, FAST_STORE
- [14. Attributes](#14-attributes) — ATTR, ATTR_CLEAR, ATTR_REF, ATTR_STORE
- [15. Items](#15-items) — ITEM, CONST_ITEM, and variants
- [16. Argument production](#16-argument-production) — BUILD_ARGS, BUILD_FUNCTION, BUILD_KWDS, etc.
- [17. Calls](#17-calls) — CALL, CALL_NAMES, CALL_STAR, CALL_ATTR*, RECUR*, TAIL_*
- [18. Block control](#18-block-control) — PUSH_FOREACH, PUSH_LOOP, PUSH_NS, PUSH_TRY, PUSH_FINALLY, CLEAR_*, POP_*
- [19. Object protocol](#19-object-protocol) — OBJECT_* (comparisons, predicates, numeric, general)
- [20. Delimited continuations](#20-delimited-continuations) — PUSH_PROMPT, SET_PROMPT_HANDLER, POP_PROMPT, SHIFT, INVOKE_K, INVOKE_K_VAR
- [21. Undelimited continuations (call/cc)](#21-undelimited-continuations-callcc) — PUSH_CC, POP_CC, CAPTURE_CC
- [22. Dynamic wind](#22-dynamic-wind) — PUSH_WIND, POP_WIND, END_WIND
- [23. Region control](#23-region-control) — PUSH_REGION, POP_REGION
- [24. Sentinel](#24-sentinel) — THEEND

### 2.1 Quick-reference index

| Opcode | Style | One-line effect | Section |
|--------|-------|------------------|---------|
| NOOP | NOARG | Advance PC only | [§3](#kos_op_noop) |
| LABEL | COMPILE | Jump target; stripped post-compile | [§3](#kos_op_label) |
| POSITION | COMPILE | Source position; stripped post-compile | [§3](#kos_op_position) |
| DUP | COUNT | Duplicate top N stack elements | [§4](#kos_op_dup) |
| POP | COUNT | Pop N elements | [§4](#kos_op_pop) |
| CONST | CONST | Push constant from pool | [§5](#kos_op_const) |
| AWAIT | NOARG | Suspend on awaitable; resume with value | [§6](#kos_op_await) |
| BREAK | COUNT | Jump to loop break target | [§6](#kos_op_break) |
| CONTINUE | COUNT | Jump to loop continue target | [§6](#kos_op_continue) |
| RAISE | COUNT | Pop N, raise exception | [§6](#kos_op_raise) |
| RETURN | COUNT | Return TOS (or push to parent) | [§6](#kos_op_return) |
| YIELD | COUNT | Yield TOS | [§6](#kos_op_yield) |
| YIELD_FROM | NOARG | Stub: yield from iterator | [§6](#kos_op_yield_from) |
| ITER_NEXT | LABEL | Next from region iterator; else jump | [§7](#kos_op_iter_next) |
| NEXT | COUNT | Stack-based next (1 or 2 args) | [§7](#kos_op_next) |
| JUMP | LABEL | Unconditional jump to pc | [§8](#kos_op_jump) |
| JUMP_FALSE | LABEL | Jump if TOS false | [§8](#kos_op_jump_false) |
| JUMP_TRUE | LABEL | Jump if TOS true | [§8](#kos_op_jump_true) |
| JUMP_AND | LABEL | Short-circuit AND | [§8](#kos_op_jump_and) |
| JUMP_OR | LABEL | Short-circuit OR | [§8](#kos_op_jump_or) |
| CHECK_EQ | LABEL | Jump if left ≠ right | [§9](#kos_op_check_eq) |
| CHECK_NE | LABEL | Jump if left == right | [§9](#kos_op_check_ne) |
| CHECK_IN | LABEL | Jump if value not in container | [§9](#kos_op_check_in) |
| CHECK_NOT_IN | LABEL | Jump if value in container | [§9](#kos_op_check_not_in) |
| CHECK_IS | LABEL | Jump if left is not right | [§9](#kos_op_check_is) |
| CHECK_IS_NOT | LABEL | Jump if left is right | [§9](#kos_op_check_is_not) |
| CHECK_EXCEPTION | LABEL | Jump if exception no match | [§9](#kos_op_check_exception) |
| GLOBALS | NOARG | Push globals dict | [§10](#kos_op_globals) |
| LOCALS | NOARG | Push locals dict | [§10](#kos_op_locals) |
| EXCEPTION | NOARG | Push current exception | [§11](#kos_op_exception) |
| EXCEPTION_CLEAR | NOARG | Clear exception state | [§11](#kos_op_exception_clear) |
| EXCEPTION_MATCH | COUNT | Match exception, push result | [§11](#kos_op_exception_match) |
| VAR | VAR | Load by name | [§12](#kos_op_var) |
| VAR_BIND | VAR | Bind name to value | [§12](#kos_op_var_bind) |
| VAR_STORE | VAR | Store TOS in named variable | [§12](#kos_op_var_store) |
| FAST | FASTVAR | Load from slot | [§13](#kos_op_fast) |
| FAST_STORE | FASTVAR | Store TOS in slot | [§13](#kos_op_fast_store) |
| ATTR | CONST | Get attribute | [§14](#kos_op_attr) |
| ATTR_STORE | CONST | Set attribute | [§14](#kos_op_attr_store) |
| ITEM | NOARG | Get item (key on stack) | [§15](#kos_op_item) |
| ITEM_STORE | NOARG | Set item | [§15](#kos_op_item_store) |
| CONST_ITEM | CONST | Get item (constant key) | [§15](#kos_op_const_item) |
| BUILD_FUNCTION | COUNT | Build function from spec | [§16](#kos_op_build_function) |
| CALL | COUNT | Call with N positional args | [§17](#kos_op_call) |
| PUSH_FOREACH | LABEL | Push foreach region; break target | [§18](#kos_op_push_foreach) |
| PUSH_LOOP | LABEL | Push loop region | [§18](#kos_op_push_loop) |
| PUSH_NS | COUNT | Push namespace scope | [§18](#kos_op_push_ns) |
| PUSH_TRY | LABEL | Push try region | [§18](#kos_op_push_try) |
| PUSH_PROMPT | CONST | Push prompt (delimited cont.) | [§20](#kos_op_push_prompt) |
| SET_PROMPT_HANDLER | LABEL | Set handler for top prompt | [§20](#kos_op_set_prompt_handler) |
| POP_PROMPT | NOARG | Pop prompt | [§20](#kos_op_pop_prompt) |
| SHIFT | CONST | Capture continuation, abort to prompt | [§20](#kos_op_shift) |
| INVOKE_K | NOARG | Pop value, pop k; invoke k(value) | [§20](#kos_op_invoke_k) |
| INVOKE_K_VAR | CONST | Invoke continuation by name | [§20](#kos_op_invoke_k_var) |
| THEEND | NOARG | Unreachable; no runtime | [§24](#kos_op_theend) |

Other opcodes (CHECK_LT/LE/GT/GE, CHECK_IS_INSTANCE/IS_SUBCLASS, VAR_CLEAR, VAR_REF, FAST_BIND/CLEAR/REF, ATTR_CLEAR/REF, ITEM_CLEAR/REF, CONST_ITEM_*, BUILD_*, CALL_NAMES/STAR, CALL_ATTR*, RECUR*, TAIL_*, PUSH_FINALLY/POP_*, CLEAR_*, OBJECT_*, PUSH_CC/POP_CC/CAPTURE_CC, PUSH_WIND/POP_WIND/END_WIND, PUSH_REGION/POP_REGION) are listed under their category sections above; use the category index to jump to the section, then scan for the opcode name.


## 3. No-op and compile-time

### KOS_OP_NOOP

- **Style**: NOARG. Argument unused.
- **Status**: Implemented.
- **Stack**: No change.
- **Invariants**: None beyond global.
- **Behavior**: Advance PC only; no effect.

**Example:** NOOP in a short sequence (e.g. after label resolution).

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (42)   | stack: [..., 42] |
| 1     | NOOP   | 0   | 0        | no effect; advance PC |
| 2     | RETURN | 1   | 1        | |

---

### KOS_OP_LABEL

- **Style**: COMPILE. Compile-time only.
- **Status**: Compile-time; stripped in post-compile under normal circumstances. If retained, no-op at runtime.
- **Stack**: No change.
- **Invariants**: Not present in final bytecode after normal finalize; or executed as no-op if retained.
- **Behavior**: Jump target placeholder. Finalize resolves branch targets to the LABEL's pc, then **strips** the LABEL instruction (branch args are adjusted to point at the next instruction). If a LABEL is retained, it advances PC only (safety net).
- **Compiler form:** Pre-compile arg = string (label name). Branches refer to this name; finalize resolves names to pcs and strips LABEL. Post-compile: **normally omitted**; if retained, arg is 0 or unused.

**Pre-compile example:** List of (opcode, arg) pairs. The label name `"done"` is used by JUMP; finalize resolves it and then strips the LABEL.

```
["JUMP", "done"],
["CONST", 99],
["LABEL", "done"],
["RETURN", 1],
```

**Post-compile (normal):** LABEL is stripped. JUMP's target was the LABEL at index 2; after stripping, that target becomes index 1 (the next instruction). Constant 99 is in the pool at index 0.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | JUMP   | 1   | 1 (-> 1) | branch to "done" (was LABEL at 2; after strip, target is 1) |
| 1     | CONST  | 0   | 0 (99)   | dead code; pool 0 = 99 |
| 2     | RETURN | 1   | 1        | (was index 3 before strip) |

**Post-compile (if LABEL retained):** Safety-net case only. If the LABEL were not stripped, it would appear as below and execute as a no-op.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | JUMP   | 2   | 2 (-> 2) | branch to label |
| 1     | CONST  | 0   | 0 (99)   | dead code |
| 2     | LABEL  | 0   | —        | target "done"; no-op if executed |
| 3     | RETURN | 1   | 1        | |

---

### KOS_OP_POSITION

- **Style**: COMPILE. Compile-time only.
- **Status**: Compile-time; **stripped** in post-compile under normal circumstances. If retained, no-op at runtime.
- **Stack**: No change.
- **Invariants**: Position is recorded in the code object's position table during finalize. The POSITION instruction itself is normally omitted from the bytecode.
- **Behavior**: Source position marker. Finalize records the position (line, col) in the position table and **strips** the POSITION instruction. If a POSITION is retained, it advances PC only (safety net).
- **Compiler form:** Pre-compile arg may be (line, col) or a position token; finalize records position in the position table and strips POSITION. Post-compile: **normally omitted**; if retained, arg typically 0 or unused.

**Example:** Normally POSITION is stripped; position is stored in the position table only, so the bytecode would be: 0 CONST, 1 RETURN. If POSITION were retained (safety net), it would look like:

| index | opcode  | arg | arg view | comment |
|------:|---------|-----|----------|---------|
| 0     | POSITION| 0   | —        | line 1, col 0 (in position table); no-op if executed |
| 1     | CONST   | 0   | 0 (1)    | stack: [..., 1] |
| 2     | RETURN  | 1   | 1        | |


## 4. Stack

### KOS_OP_DUP

- **Style**: COUNT. Arg = number of elements to duplicate.
- **Status**: Implemented.
- **Stack**: `[..., a_1, ..., a_n] → [..., a_1, ..., a_n, a_1, ..., a_n]` (top `arg` elements duplicated).
- **Invariants**: Stack must have at least `arg` elements.

**Example:** DUP 1: duplicate TOS (e.g. for use in two branches).

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (10)   | stack: [..., 10] |
| 1     | DUP    | 1   | 1        | stack: [..., 10, 10] |
| 2     | RETURN | 1   | 1        | |

---

### KOS_OP_POP

- **Style**: COUNT. Arg = number of elements to pop.
- **Status**: Implemented.
- **Stack**: `[..., a_1, ..., a_n] → [...]` (pop `arg` elements).
- **Invariants**: Stack must have at least `arg` elements.

**Example:** POP 1: discard TOS.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (1)    | stack: [..., 1] |
| 1     | CONST  | 1   | 1 (2)    | stack: [..., 1, 2] |
| 2     | POP    | 1   | 1        | stack: [..., 1] |
| 3     | RETURN | 1   | 1        | |


## 5. Constants

### KOS_OP_CONST

- **Style**: CONST. Arg = constant pool index (post-compile).
- **Status**: Implemented.
- **Stack**: `[...] → [..., c]` where `c = cpool[arg]`.
- **Invariants**: `arg < cpool_len`.
- **Compiler form:** Pre-compile arg = literal value (e.g. `CONST "hello"`, `CONST
  42`). Finalize accumulates values into the constant pool and rewrites arg to
  the pool index.

**Example:** Load constant at index 0 (value 42). Pool: 0 = 42.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (42)   | stack: [..., 42] |
| 1     | RETURN | 1   | 1        | |


## 6. Mode control

### KOS_OP_AWAIT

- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: `[..., awaitable] →` (suspension); on resume `[...] → [..., value]`.
- **Invariants**: TOS must be an awaitable. AWAIT always suspends the VM.

**Example:** Await a coroutine; TOS is the awaitable. On resume, result is pushed.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (coro)  | stack: [..., awaitable] |
| 1     | AWAIT  | 0   | 0        | suspend; on resume stack: [..., value] |
| 2     | RETURN | 1   | 1        | |

---

### KOS_OP_BREAK

- **Style**: COUNT. Arg = number of stack elements to pop before jumping.
- **Status**: Implemented.
- **Stack**: Pops `arg` elements; control transfers to break target of innermost LOOP/FOREACH extent.
- **Invariants**: Must be inside a LOOP or FOREACH extent.

**Example:** Inside a loop; pop 0 and jump to extent break_pc (e.g. after POP_LOOP).

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 2     | BREAK  | 0   | 0        | pop 0, jump to break target (e.g. pc 6) |
| …     | …      | …   | …        | |
| 6     | POP_LOOP | 0 | 0        | break target |

---

### KOS_OP_CONTINUE

- **Style**: COUNT. Arg = number of stack elements to pop before jumping.
- **Status**: Implemented.
- **Stack**: Pops `arg` elements; control transfers to continue target of innermost LOOP/FOREACH extent.
- **Invariants**: Must be inside a LOOP or FOREACH extent.

**Example:** Inside a loop; pop 0 and jump to extent continue_pc (e.g. loop head).

| index | opcode   | arg | arg view | comment |
|------:|---------|-----|----------|---------|
| 4     | CONTINUE| 0   | 0        | pop 0, jump to continue target (e.g. pc 1) |
| …     | …       | …   | …        | |

---

### KOS_OP_RAISE

- **Style**: COUNT. Arg = number of values to pop (e.g. 1 for exception instance).
- **Status**: Implemented.
- **Stack**: Pops `arg` values; raises with the popped exception.
- **Invariants**: Popped value(s) form the exception to raise.

**Example:** Raise exception from TOS (arg = 1). Pool: 0 = ValueError("bad").

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (ValueError(...)) | stack: [..., exc] |
| 1     | RAISE  | 1   | 1        | pop 1, raise exc; stack unwound |

---

### KOS_OP_RETURN

- **Style**: COUNT. Arg = number of values to leave on stack as return value (typically 1).
- **Status**: Implemented.
- **Stack**: Return value is TOS. If frame has a parent, value is pushed onto parent and frame is detached; else boundary event RETURN(value).
- **Invariants**: See execution-invariants: in-loop flattening for RETURN.

**Example:** Return TOS (arg = 1). Pool: 0 = 42.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (42)   | stack: [..., 42] |
| 1     | RETURN | 1   | 1        | return 42 (boundary or push to parent) |

---

### KOS_OP_YIELD

- **Style**: COUNT. Arg = number of values to yield (typically 1).
- **Status**: Implemented.
- **Stack**: Yields TOS. Sets KOS_CODE_YIELDS on the code object. If frame has parent (async gen), value is fed to parent in-loop; else boundary event YIELD(value).
- **Invariants**: Code is treated as async generator when YIELDS flag set.

**Example:** Yield TOS (arg = 1). Pool: 0 = 100.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (100)  | stack: [..., 100] |
| 1     | YIELD  | 1   | 1        | yield 100; on resume continue |

---

### KOS_OP_YIELD_FROM

- **Style**: NOARG.
- **Status**: Stub.
- **Stack**: (Intended: delegate to sub-iterator; not implemented.)
- **Intended (not yet implemented):** Delegate to sub-iterator; yield/return values
  from it. **Current:** Stub; runtime error if executed.

**Example:** (Stub; no runtime table.)


## 7. Iteration

### KOS_OP_ITER_NEXT

- **Style**: LABEL. Arg = else branch pc (post-compile).
- **Status**: Implemented.
- **Stack**: Uses region iterator (PUSH_FOREACH). If next available: push value; else jump to `arg`. Auto-await if iterator is async.
- **Invariants**: Must be inside a PUSH_FOREACH extent. Jump target `arg` must be local.
- **Compiler form:** Pre-compile arg = string label (else / exhausted target); post-compile arg = pc.

**Example:** For-each body: get next from region iterator; if exhausted jump to 6.

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 0     | CONST    | 0   | 0 ([1,2,3]) | stack: [..., iterable] (and loop_result) |
| 1     | PUSH_FOREACH | 6 | 6 (-> 6) | push region; iterator from iterable |
| 2     | ITER_NEXT| 6   | 6 (-> 6)  | next from region; if exhausted → pc=6. stack: [..., val] or jump |
| 3     | POP      | 1   | 1        | consume value (body) |
| 4     | JUMP     | 2   | 2 (-> 2) | loop back |
| 5     | POP_LOOP | 0   | 0        | |
| 6     | LABEL    | —   | —        | else (exhausted) |

---

### KOS_OP_NEXT

- **Style**: COUNT. Arg = 1 or 2: 1 = TOS is iterator (StopIteration raises); 2 = TOS = default, below = iterator (StopIteration pushes default).
- **Status**: Implemented.
- **Stack**: Stack-based next from iterator; may suspend on async iterator.
- **Invariants**: Stack layout per arg (1 or 2 elements as described).

**Example (arg=1):** TOS = iterator; push next or raise StopIteration.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (iter) | stack: [..., iterator] |
| 1     | NEXT   | 1   | 1        | stack: [..., next_val] or raise StopIteration |

**Example (arg=2):** TOS = default, below = iterator; push next or default.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (99)   | stack: [..., default] |
| 1     | CONST  | 1   | 1 (iter) | stack: [..., default, iterator] |
| 2     | NEXT   | 2   | 2        | stack: [..., next_val] or [..., 99] if exhausted |


## 8. Jumps

### KOS_OP_JUMP

- **Style**: LABEL. Arg = target pc (post-compile).
- **Status**: Implemented.
- **Stack**: No change.
- **Invariants**: Target must be inside current extent or at its `pop_pc` (jump locality).
- **Compiler form:** Pre-compile arg = string label name (must match a LABEL in
  the same compilation unit). Post-compile arg = pc of that LABEL.

**Pre-compile example:** Same snippet as LABEL; JUMP uses the string `"done"`.

```
["JUMP", "done"],
["CONST", 99],
["LABEL", "done"],
["RETURN", 1],
```

**Post-compile example:** After finalize, LABEL is stripped (see LABEL). JUMP's target was the LABEL; it becomes the next instruction (index 1). Constant pool: 0 = 99.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | JUMP   | 1   | 1 (-> 1) | pc := 1 (target "done" after LABEL stripped) |
| 1     | CONST  | 0   | 0 (99)   | dead code |
| 2     | RETURN | 1   | 1        | |

---

### KOS_OP_JUMP_FALSE

- **Style**: LABEL. Arg = target pc (post-compile).
- **Status**: Implemented.
- **Stack**: Pops one value; if false, jump to `arg`; else advance PC.
- **Invariants**: Jump locality for target.
- **Compiler form:** Pre-compile arg = string label; post-compile arg = pc.

**Example:** Jump if TOS is false (e.g. "else" branch). Pool: 0 = False, 1 = 42, 2 = 0.

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 0     | CONST      | 0   | 0 (False)| stack: [..., False] |
| 1     | JUMP_FALSE | 4   | 4 (-> 4) | pop TOS; if false → pc=4 |
| 2     | CONST      | 1   | 1 (42)   | stack: [..., 42] |
| 3     | JUMP       | 5   | 5 (-> 5) | |
| 4     | LABEL      | —   | —        | else |
| 5     | CONST      | 2   | 2 (0)    | stack: [..., 0] |
| 6     | RETURN     | 1   | 1        | |

---

### KOS_OP_JUMP_TRUE

- **Style**: LABEL. Arg = target pc (post-compile).
- **Status**: Implemented.
- **Stack**: Pops one value; if true, jump to `arg`; else advance PC.
- **Invariants**: Jump locality for target.
- **Compiler form:** Pre-compile arg = string label; post-compile arg = pc.

**Example:** Jump if TOS is true (e.g. "then" branch).

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 0     | VAR        | 0   | 0 (x)    | stack: [..., x] |
| 1     | JUMP_TRUE  | 3   | 3 (-> 3) | pop TOS; if true → pc=3 |
| 2     | CONST      | 0   | 0 (0)    | else path |
| 3     | LABEL      | —   | —        | then path |
| 4     | RETURN     | 1   | 1        | |

---

### KOS_OP_JUMP_AND

- **Style**: LABEL. Short-circuit AND: jump if TOS is false.
- **Status**: Implemented.
- **Stack**: Pops one value; if false, jump to `arg`; else advance.
- **Invariants**: Jump locality for target.
- **Compiler form:** Pre-compile arg = string label; post-compile arg = pc.

**Example:** Short-circuit and: if first operand false, jump to "fail".

| index | opcode    | arg | arg view | comment |
|------:|-----------|-----|----------|---------|
| 0     | CONST     | 0   | 0 (True) | stack: [..., a] |
| 1     | JUMP_AND  | 4   | 4 (-> 4) | if not a → pc=4 |
| 2     | CONST     | 1   | 1 (True) | stack: [..., b] |
| 3     | JUMP_AND  | 4   | 4 (-> 4) | if not b → pc=4 |
| 4     | LABEL     | —   | —        | fail (or result) |

---

### KOS_OP_JUMP_OR

- **Style**: LABEL. Short-circuit OR: jump if TOS is true.
- **Status**: Implemented.
- **Stack**: Pops one value; if true, jump to `arg`; else advance.
- **Invariants**: Jump locality for target.
- **Compiler form:** Pre-compile arg = string label; post-compile arg = pc.

**Example:** Short-circuit or: if first operand true, jump to "ok".

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 0     | CONST    | 0   | 0 (False)| stack: [..., a] |
| 1     | JUMP_OR  | 4   | 4 (-> 4) | if a → pc=4 |
| 2     | CONST    | 1   | 1 (False)| stack: [..., b] |
| 3     | JUMP_OR  | 4   | 4 (-> 4) | if b → pc=4 |
| 4     | LABEL    | —   | —        | ok |
| 5     | RETURN   | 1   | 1        | |


## 9. Check jumps

Check jumps: if the predicate does **not** hold, jump to `arg`. All take LABEL; jump locality applies.

### KOS_OP_CHECK_EQ

- **Style**: LABEL. Arg = target pc (post-compile).
- **Status**: Implemented.
- **Stack**: `[..., left, right]` (right = TOS). Pops both; if left != right, jump to `arg`; else fall through. Stack after: `[...]`.
- **Invariants**: Jump locality for target.
- **Compiler form:** Pre-compile arg = string label (fail/mismatch target); post-compile arg = pc.

**Example:** Pattern match or guard: jump if not equal. Pool: 0 = 1, 1 = 2, 2 = 99, 3 = 0. Covered by `tests/lib/test_ceval.py`: `test_check_eq_jumps_on_mismatch`, `test_check_eq_continues_on_match`.

| index | opcode  | arg | arg view | comment |
|------:|---------|-----|----------|---------|
| 0     | CONST   | 0   | 0 (1)    | stack: [..., left] |
| 1     | CONST   | 1   | 1 (2)    | stack: [..., left, right] |
| 2     | CHECK_EQ| 5   | 5 (-> 5) | if left != right → pc=5; else stack: [...] |
| 3     | CONST   | 2   | 2 (99)   | stack: [..., 99] |
| 4     | RETURN  | 1   | 1        | |
| 5     | LABEL   | —   | —        | fail |
| 6     | CONST   | 3   | 3 (0)    | stack: [..., 0] |
| 7     | RETURN  | 1   | 1        | |

---

### KOS_OP_CHECK_NE

- **Style**: LABEL. Arg = target pc (post-compile).
- **Status**: Implemented.
- **Stack**: `[..., left, right]` (right = TOS). Pops both; if left == right (check fails), jump to `arg`; else fall through. Stack after: `[...]`.
- **Invariants**: Jump locality for target.
- **Compiler form:** Pre-compile arg = string label; post-compile arg = pc.

**Example:** Jump when left == right (e.g. "not equal" guard). Pool: 0 = 1, 1 = 1, 2 = 0.

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 0     | CONST    | 0   | 0 (1)    | stack: [..., left] |
| 1     | CONST    | 1   | 1 (1)    | stack: [..., left, right] |
| 2     | CHECK_NE | 5   | 5 (-> 5) | if left == right → pc=5; else stack: [...] |
| 3     | RETURN   | 1   | 1        | |
| 5     | LABEL    | —   | —        | fail |

---

### KOS_OP_CHECK_LT, KOS_OP_CHECK_LE, KOS_OP_CHECK_GT, KOS_OP_CHECK_GE

- **Style**: LABEL. Arg = target pc (post-compile).
- **Status**: Implemented.
- **Stack**: `[..., left, right]` (right = TOS). Pops both; if comparison fails (e.g. CHECK_LT: left >= right), jump to `arg`; else fall through. Stack after: `[...]`.
- **Invariants**: Jump locality for target.
- **Compiler form:** Pre-compile arg = string label; post-compile arg = pc.

---

### KOS_OP_CHECK_IN

- **Style**: LABEL. Arg = target pc (post-compile).
- **Status**: Implemented.
- **Stack**: `[..., container, value]` (value = TOS). Pops both; tests `value in container` (PySequence_Contains). If **false** (check fails), jump to `arg`; else fall through. Stack after: `[...]` in both cases (both operands always popped).
- **Invariants**: Jump locality for target.
- **Compiler form:** Pre-compile arg = string label (e.g. "miss"); post-compile arg = pc.

**Example: value in container (guard / pattern match).** Constant pool: 0 = `[1, 2, 3]`, 1 = `2`, 2 = `1`, 3 = `0`. If `2 in [1,2,3]` push 1 and return; else jump to "miss", push 0 and return. Covered by `tests/lib/test_ceval.py`: `test_check_in_continues_when_contained`, `test_check_in_jumps_when_not_contained`.

| index | opcode   | arg | arg view   | comment |
|------:|----------|-----|------------|---------|
| 0     | CONST    | 0   | 0 ([1, 2, 3]) | stack: [..., container] |
| 1     | CONST    | 1   | 1 (2)      | stack: [..., container, value] |
| 2     | CHECK_IN | 5   | 5 (-> 5)   | if value not in container → pc=5; else fall through. stack: [...] |
| 3     | CONST    | 2   | 2 (1)      | stack: [..., 1] |
| 4     | RETURN   | 1   | 1          | |
| 5     | LABEL    | —   | —          | miss |
| 6     | CONST    | 3   | 3 (0)      | stack: [..., 0] |
| 7     | RETURN   | 1   | 1          | |

---

### KOS_OP_CHECK_NOT_IN

- **Style**: LABEL. Arg = target pc (post-compile).
- **Status**: Implemented.
- **Stack**: `[..., container, value]` (value = TOS). Pops both; tests `value not in container`. If **true** (i.e. value not in container), jump to `arg`; else fall through. Stack after: `[...]` in both cases.
- **Invariants**: Jump locality for target.
- **Compiler form:** Pre-compile arg = string label; post-compile arg = pc.

**Example:** Jump when value not in container (e.g. guard for "in" branch).

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 0     | CONST      | 0   | 0 (s)    | stack: [..., container] |
| 1     | CONST      | 1   | 1 (key) | stack: [..., container, value] |
| 2     | CHECK_NOT_IN | 5 | 5 (-> 5)| if value not in container → pc=5; else stack: [...] |
| 3     | RETURN     | 1   | 1        | |
| 5     | LABEL      | —   | —        | miss |

---

### KOS_OP_CHECK_IS

- **Style**: LABEL. Arg = target pc (post-compile).
- **Status**: Implemented.
- **Stack**: `[..., left, right]` (right = TOS). Pops both; if left is not right, jump to `arg`; else fall through. Stack after: `[...]`.
- **Invariants**: Jump locality for target.
- **Compiler form:** Pre-compile arg = string label; post-compile arg = pc.

**Example:** Identity check (e.g. for None or singleton). Pool: 0 = [], 1 = [], 2 = 1, 3 = 0.

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 0     | CONST    | 0   | 0 ([])   | stack: [..., left] |
| 1     | CONST    | 1   | 1 ([])   | stack: [..., left, right] (different object) |
| 2     | CHECK_IS | 5   | 5 (-> 5) | if left is not right → pc=5 |
| 3     | CONST    | 2   | 2 (1)    | stack: [..., 1] |
| 4     | RETURN   | 1   | 1        | |
| 5     | LABEL    | —   | —        | diff |
| 6     | CONST    | 3   | 3 (0)    | stack: [..., 0] |
| 7     | RETURN   | 1   | 1        | |

---

### KOS_OP_CHECK_IS_NOT

- **Style**: LABEL. Arg = target pc (post-compile).
- **Status**: Implemented.
- **Stack**: `[..., left, right]` (right = TOS). Pops both; if left is right, jump to `arg`; else fall through. Stack after: `[...]`.
- **Invariants**: Jump locality for target.
- **Compiler form:** Pre-compile arg = string label; post-compile arg = pc.

**Example:** Jump when left is right (e.g. "is not" guard). Pool: 0 = None, 1 = None.

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 0     | CONST      | 0   | 0 (None) | stack: [..., left] |
| 1     | CONST      | 1   | 1 (None) | stack: [..., left, right] |
| 2     | CHECK_IS_NOT | 5  | 5 (-> 5) | if left is right → pc=5; else stack: [...] |
| 3     | RETURN     | 1   | 1        | |
| 5     | LABEL      | —   | —        | same |

---

### KOS_OP_CHECK_IS_INSTANCE

- **Style**: LABEL (or COUNT for type tuple). Arg = target pc.
- **Status**: Implemented.
- **Stack**: `[..., obj, type]` (or obj + N types). Pops operands; if isinstance(obj, type) fails, jump to `arg`; else fall through. Stack after: `[...]`.
- **Invariants**: Jump locality for target.

**Example:** Pattern match by type. Pool: 0 = obj, 1 = ValueError.

| index | opcode          | arg | arg view | comment |
|------:|-----------------|-----|----------|---------|
| 0     | EXCEPTION       | 0   | 0        | stack: [..., exc] |
| 1     | CONST           | 0   | 0 (ValueError) | stack: [..., exc, typ] |
| 2     | CHECK_IS_INSTANCE | 5  | 5 (-> 5) | if not isinstance(exc, typ) → pc=5 |
| 3     | RETURN          | 1   | 1        | handle |
| 5     | LABEL           | —   | —        | re-raise |

---

### KOS_OP_CHECK_IS_SUBCLASS

- **Style**: LABEL (or COUNT). Arg = target pc.
- **Status**: Implemented.
- **Stack**: Class and type(s); jump if issubclass check fails. Stack after per implementation.
- **Invariants**: Jump locality for target.

---

### KOS_OP_CHECK_EXCEPTION

- **Style**: LABEL. Arg = target pc (post-compile).
- **Status**: Implemented.
- **Stack**: Used in exception handler; match current exception against type(s). Jump if no match.
- **Invariants**: Jump locality for target. Used inside exception handling path.
- **Compiler form:** Pre-compile arg = string label (next handler or re-raise); post-compile arg = pc.

**Example:** In except block: if exception not matching type, jump to next handler.

| index | opcode        | arg | arg view | comment |
|------:|---------------|-----|----------|---------|
| 3     | EXCEPTION     | 0   | 0        | stack: [..., exc] |
| 4     | CHECK_EXCEPTION | 7 | 7 (-> 7) | if exc not isinstance(typ) → pc=7 |
| 5     | POP           | 1   | 1        | consume exc |
| 6     | RETURN        | 1   | 1        | handle |
| 7     | LABEL         | —   | —        | re-raise or next handler |


## 10. Introspection

### KOS_OP_GLOBALS

- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: `[...] → [..., globals_dict]`.
- **Invariants**: Frame has globals namespace.

**Example:** Push frame's globals dict.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | GLOBALS| 0   | 0        | stack: [..., globals_dict] |
| 1     | RETURN | 1   | 1        | |

---

### KOS_OP_LOCALS

- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: `[...] → [..., locals_dict]`.
- **Invariants**: Frame has locals namespace.

**Example:** Push frame's locals dict.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | LOCALS | 0   | 0        | stack: [..., locals_dict] |
| 1     | RETURN | 1   | 1        | |


## 11. Exception handling

### KOS_OP_EXCEPTION

- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: `[...] → [..., exception]`. Push current exception (in handler).
- **Invariants**: Used when an exception is being handled.

**Example:** In except block: load current exception.

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 2     | EXCEPTION| 0   | 0        | stack: [..., exc] |
| 3     | VAR_STORE| 0   | 0 (e)    | store exc in name "e" |

---

### KOS_OP_EXCEPTION_CLEAR

- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: No change. Clear current exception state.
- **Invariants**: —

**Example:** After handling, clear exception before continuing.

| index | opcode         | arg | arg view | comment |
|------:|----------------|-----|----------|---------|
| 4     | EXCEPTION_CLEAR| 0   | 0        | clear exception state |
| 5     | RETURN         | 1   | 1        | |

---

### KOS_OP_EXCEPTION_MATCH

- **Style**: COUNT. Arg = count of type(s) to match.
- **Status**: Implemented.
- **Stack**: Pops exception and type(s); pushes match result (e.g. bool).
- **Invariants**: Used in handler.

**Example:** Match current exception against one type; push True/False.

| index | opcode        | arg | arg view | comment |
|------:|---------------|-----|----------|---------|
| 2     | EXCEPTION     | 0   | 0        | stack: [..., exc] |
| 3     | CONST         | 0   | 0 (ValueError) | stack: [..., exc, typ] |
| 4     | EXCEPTION_MATCH | 1  | 1        | stack: [..., result] |
| 5     | JUMP_FALSE    | 8   | 8 (-> 8)  | if no match, next handler |


## 12. Variables by name

### KOS_OP_VAR

- **Style**: VAR. Arg = constant pool index of variable name (post-compile).
- **Status**: Implemented.
- **Stack**: `[...] → [..., value]`. Load value bound to name in frame namespace.
- **Invariants**: Name in constant pool.
- **Compiler form:** Pre-compile arg = name (string/symbol); finalize stores name in pool and rewrites arg to pool index. May be lowered to FAST with slot index for locals.

**Example:** Load variable "x". Pool: 0 = "x".

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | VAR    | 0   | 0 ('x')  | stack: [..., value] |
| 1     | RETURN | 1   | 1        | |

---

### KOS_OP_VAR_BIND

- **Style**: VAR. Arg = constant pool index of name (post-compile).
- **Status**: Implemented.
- **Stack**: `[..., value] → [...]`. Bind name to value in frame namespace.
- **Invariants**: Name in constant pool.
- **Compiler form:** Pre-compile arg = name (string/symbol); post-compile arg = pool index.

**Example:** Bind "n" to TOS. Pool: 0 = "n".

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 0     | CONST    | 0   | 0 (42)   | stack: [..., 42] |
| 1     | VAR_BIND | 1   | 1 ('n')  | bind n = 42; stack: [...] |

---

### KOS_OP_VAR_CLEAR

- **Style**: VAR. Arg = constant pool index of name (post-compile).
- **Status**: Implemented.
- **Stack**: No stack effect. Delete name from namespace.
- **Invariants**: Name in constant pool.
- **Compiler form:** Pre-compile arg = name; post-compile arg = pool index.

**Example:** Delete name "tmp". Pool: 0 = "tmp".

| index | opcode    | arg | arg view | comment |
|------:|-----------|-----|----------|---------|
| 2     | VAR_CLEAR | 0   | 0 ('tmp')| delete "tmp" from namespace |

---

### KOS_OP_VAR_REF

- **Style**: VAR. Arg = constant pool index of name (post-compile).
- **Status**: Implemented.
- **Stack**: `[...] → [..., ref]`. Push a reference to the named variable (e.g. for closure).
- **Invariants**: Name in constant pool.
- **Compiler form:** Pre-compile arg = name; post-compile arg = pool index.

**Example:** Push reference for "cell". Pool: 0 = "cell".

| index | opcode | arg | arg view  | comment |
|------:|--------|-----|-----------|---------|
| 0     | VAR_REF| 0   | 0 ('cell')| stack: [..., ref] |

---

### KOS_OP_VAR_STORE

- **Style**: VAR. Arg = constant pool index of name (post-compile).
- **Status**: Implemented.
- **Stack**: `[..., value] → [...]`. Store TOS into the named variable.
- **Invariants**: Name must be bound (e.g. to a ref or slot).
- **Compiler form:** Pre-compile arg = name; post-compile arg = pool index.

**Example:** Store TOS into "x". Pool: 0 = "x".

| index | opcode    | arg | arg view | comment |
|------:|-----------|-----|----------|---------|
| 0     | CONST     | 0   | 0 (10)   | stack: [..., 10] |
| 1     | VAR_STORE | 1   | 1 ('x')  | store 10 in "x"; stack: [...] |


## 13. Fast variables

### KOS_OP_FAST

- **Style**: FASTVAR. Arg = fast local slot index.
- **Status**: Implemented.
- **Stack**: `[...] → [..., value]`. Load from slot.
- **Invariants**: Slot index in range for frame.
- **Compiler form:** Usually post-compile only: emitted when VAR is lowered to a
  fast local (arg = slot index). Compiler may emit FAST directly with slot index
  in both forms.

**Example:** Load fast local 0. Arg view: 0 (local name if known).

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | FAST   | 0   | 0 (a)    | stack: [..., value] |
| 1     | RETURN | 1   | 1        | |

---

### KOS_OP_FAST_BIND

- **Style**: FASTVAR. Arg = fast local slot index.
- **Status**: Implemented.
- **Stack**: `[..., value] → [...]`. Bind slot (pop value into slot).
- **Invariants**: Slot index in range.
- **Compiler form:** Typically post-compile (from VAR_BIND lowering) or compiler emits with slot index.

**Example:** Bind slot 0 to TOS.

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 0     | CONST    | 0   | 0 (1)    | stack: [..., 1] |
| 1     | FAST_BIND| 0   | 0 (a)    | slot 0 := 1; stack: [...] |

---

### KOS_OP_FAST_CLEAR

- **Style**: FASTVAR. Arg = fast local slot index.
- **Status**: Implemented.
- **Stack**: No stack effect. Clear slot.
- **Invariants**: Slot index in range.

**Example:** Clear slot 1.

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 2     | FAST_CLEAR | 1   | 1        | clear slot 1 |

---

### KOS_OP_FAST_REF

- **Style**: FASTVAR. Arg = fast local slot index.
- **Status**: Implemented.
- **Stack**: `[...] → [..., ref]`. Push reference to slot.
- **Invariants**: Slot index in range.

**Example:** Push reference to slot 0.

| index | opcode  | arg | arg view | comment |
|------:|---------|-----|----------|---------|
| 0     | FAST_REF| 0   | 0 (a)    | stack: [..., ref] |

---

### KOS_OP_FAST_STORE

- **Style**: FASTVAR. Arg = fast local slot index.
- **Status**: Implemented.
- **Stack**: `[..., value] → [...]`. Store TOS in slot.
- **Invariants**: Slot index in range.

**Example:** Store TOS in slot 0.

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 0     | CONST      | 0   | 0 (99)   | stack: [..., 99] |
| 1     | FAST_STORE | 0   | 0 (a)    | slot 0 := 99; stack: [...] |


## 14. Attributes

### KOS_OP_ATTR

- **Style**: CONST. Arg = constant pool index of attribute name (post-compile).
- **Status**: Implemented.
- **Stack**: `[..., obj] → [..., obj, value]`. Get attribute; push value.
- **Invariants**: TOS is the object. Name in constant pool.
- **Compiler form:** Pre-compile arg = attribute name (string); post-compile arg = pool index.

**Example:** Get obj.name. Pool: 0 = "name".

| index | opcode | arg | arg view   | comment |
|------:|--------|-----|------------|---------|
| 0     | VAR    | 0   | 0 ('obj')  | stack: [..., obj] |
| 1     | ATTR   | 1   | 1 ('name') | stack: [..., obj, value] |
| 2     | RETURN | 1   | 1          | |

---

### KOS_OP_ATTR_STORE

- **Style**: CONST. Arg = constant pool index of attribute name.
- **Status**: Implemented.
- **Stack**: `[..., obj, value] → [..., obj]`. Pop value, set attribute on obj.
- **Invariants**: Below TOS is object; name in pool.

**Example:** Set obj.x = TOS. Pool: 0 = "x".

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 0     | VAR        | 0   | 0 ('obj')| stack: [..., obj] |
| 1     | CONST      | 1   | 1 (10)   | stack: [..., obj, 10] |
| 2     | ATTR_STORE | 2   | 2 ('x')  | obj.x = 10; stack: [..., obj] |

---

### KOS_OP_ATTR_CLEAR

- **Style**: CONST. Arg = constant pool index of attribute name.
- **Status**: Implemented.
- **Stack**: `[..., obj] → [...]`. Delete attribute.
- **Invariants**: TOS is the object.

**Example:** Delete obj.attr. Pool: 0 = "attr".

| index | opcode     | arg | arg view  | comment |
|------:|------------|-----|-----------|---------|
| 0     | VAR        | 0   | 0 ('obj') | stack: [..., obj] |
| 1     | ATTR_CLEAR | 1   | 1 ('attr')| del obj.attr |

---

### KOS_OP_ATTR_REF

- **Style**: CONST. Arg = constant pool index of attribute name.
- **Status**: Implemented.
- **Stack**: `[..., obj] → [..., obj, attr_ref]`. Push attribute reference.
- **Invariants**: TOS is the object.

**Example:** Push reference for obj.prop. Pool: 0 = "prop".

| index | opcode   | arg | arg view  | comment |
|------:|----------|-----|-----------|---------|
| 0     | VAR      | 0   | 0 ('obj') | stack: [..., obj] |
| 1     | ATTR_REF | 1   | 1 ('prop')| stack: [..., obj, attr_ref] |


## 15. Items

### KOS_OP_ITEM

- **Style**: NOARG. Key from stack (below object).
- **Status**: Implemented.
- **Stack**: `[..., obj, key] → [..., obj, value]`. Get item; push value.
- **Invariants**: TOS = key, below = object.

**Example:** Get obj[key]. Pool: 0 = "d", 1 = "k".

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | VAR    | 0   | 0 ('d')  | stack: [..., d] |
| 1     | CONST  | 1   | 1 ('k')  | stack: [..., d, 'k'] |
| 2     | ITEM   | 0   | 0        | stack: [..., d, d['k']] |
| 3     | RETURN | 1   | 1        | |

---

### KOS_OP_ITEM_STORE

- **Style**: NOARG. Key and value from stack.
- **Status**: Implemented.
- **Stack**: `[..., obj, key, value] → [..., obj]`. Set item.
- **Invariants**: TOS = value, below = key, below = object.

**Example:** obj[key] = value.

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 0     | VAR        | 0   | 0 ('d')  | stack: [..., d] |
| 1     | CONST      | 1   | 1 ('k')  | stack: [..., d, 'k'] |
| 2     | CONST      | 2   | 2 (42)   | stack: [..., d, 'k', 42] |
| 3     | ITEM_STORE | 0   | 0        | d['k'] = 42; stack: [..., d] |

---

### KOS_OP_ITEM_CLEAR

- **Style**: NOARG. Key from stack.
- **Status**: Implemented.
- **Stack**: `[..., obj, key] → [..., obj]`. Delete item.
- **Invariants**: Object and key on stack.

**Example:** del obj[key].

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 0     | VAR        | 0   | 0 ('d')  | stack: [..., d] |
| 1     | CONST      | 1   | 1 ('k')  | stack: [..., d, 'k'] |
| 2     | ITEM_CLEAR | 0   | 0        | del d['k']; stack: [..., d] |

---

### KOS_OP_ITEM_REF

- **Style**: NOARG. Key from stack.
- **Status**: Implemented.
- **Stack**: `[..., obj, key] → [..., obj, item_ref]`. Push item reference.
- **Invariants**: Object and key on stack.

**Example:** Push reference for d[k]. Pool: 0 = "d", 1 = "k".

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 0     | VAR      | 0   | 0 ('d')  | stack: [..., d] |
| 1     | CONST    | 1   | 1 ('k')  | stack: [..., d, 'k'] |
| 2     | ITEM_REF | 0   | 0        | stack: [..., d, item_ref] |

---

### KOS_OP_CONST_ITEM

- **Style**: CONST. Arg = constant pool index of key (post-compile).
- **Status**: Implemented.
- **Stack**: `[..., obj] → [..., obj, value]`. Get item with constant key.
- **Invariants**: TOS is object; key from pool.
- **Compiler form:** Pre-compile arg = key value (e.g. string or index); post-compile arg = pool index.

**Example:** Get obj["key"]. Pool: 0 = "obj", 1 = "key".

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 0     | VAR        | 0   | 0 ('obj')| stack: [..., obj] |
| 1     | CONST_ITEM | 1   | 1 ('key')| stack: [..., obj, obj['key']] |

---

### KOS_OP_CONST_ITEM_STORE

- **Style**: CONST. Arg = constant pool index of key.
- **Status**: Implemented.
- **Stack**: `[..., obj, value] → [..., obj]`. Set item with constant key.
- **Invariants**: Key from pool.

**Example:** obj["key"] = value. Pool: 0 = "key".

| index | opcode        | arg | arg view | comment |
|------:|---------------|-----|----------|---------|
| 0     | VAR           | 0   | 0 ('obj')| stack: [..., obj] |
| 1     | CONST         | 1   | 1 (42)   | stack: [..., obj, 42] |
| 2     | CONST_ITEM_STORE | 0 | 0 ('key')| obj['key'] = 42; stack: [..., obj] |

---

### KOS_OP_CONST_ITEM_CLEAR

- **Style**: CONST. Arg = constant pool index of key.
- **Status**: Implemented.
- **Stack**: `[..., obj] → [..., obj]`. Delete item with constant key.
- **Invariants**: Key from pool.

**Example:** del obj["key"]. Pool: 0 = "key".

| index | opcode        | arg | arg view | comment |
|------:|---------------|-----|----------|---------|
| 0     | VAR           | 0   | 0 ('obj')| stack: [..., obj] |
| 1     | CONST_ITEM_CLEAR | 0 | 0 ('key')| del obj['key']; stack: [..., obj] |

---

### KOS_OP_CONST_ITEM_REF

- **Style**: CONST. Arg = constant pool index of key.
- **Status**: Implemented.
- **Stack**: `[..., obj] → [..., obj, item_ref]`. Push item reference for constant key.
- **Invariants**: Key from pool.

**Example:** Push reference for obj["key"]. Pool: 0 = "key".

| index | opcode        | arg | arg view | comment |
|------:|---------------|-----|----------|---------|
| 0     | VAR           | 0   | 0 ('obj')| stack: [..., obj] |
| 1     | CONST_ITEM_REF| 0   | 0 ('key')| stack: [..., obj, item_ref] |


## 16. Argument production

### KOS_OP_BUILD_ARGS

- **Style**: COUNT. Arg = number of positional arguments.
- **Status**: Stub.
- **Stack**: (Intended: build positional args bundle from stack for CALL.)
- **Example:** (Stub; no runtime table.)

---

### KOS_OP_BUILD_FUNCTION

- **Style**: COUNT. Arg = count (e.g. 0 for no closure cells).
- **Status**: Implemented.
- **Stack**: `[..., spec_or_code] → [..., function]`. Build function from TOS (e.g. function spec from constant pool).
- **Invariants**: TOS is function spec or code.

**Example:** Build function from spec in constant pool. Pool: 0 = function_spec. Covered by `tests/lib/test_ceval.py`: `test_build_function_returns_function`, `test_build_function_returned_function_is_callable`.

| index | opcode       | arg | arg view | comment |
|------:|--------------|-----|----------|---------|
| 0     | CONST        | 0   | 0 (fspec)| stack: [..., fspec] |
| 1     | BUILD_FUNCTION | 0 | 0        | stack: [..., function] |
| 2     | RETURN       | 1   | 1        | |

---

### KOS_OP_BUILD_KWDS, KOS_OP_BUILD_KWDS_NAMES, KOS_OP_COMBINE_ARGS, KOS_OP_COMBINE_KWDS

- **Styles**: PAIR_COUNT, CONST, COUNT, COUNT.
- **Status**: Stub.
- **Stack**: (Intended: build keyword/combined args for CALL_NAMES/CALL_STAR.)
- **Example:** (Stub; no runtime table.)


## 17. Calls

### KOS_OP_CALL

- **Style**: COUNT. Arg = number of positional arguments.
- **Status**: Implemented.
- **Stack**: `[..., callable, arg_1, ..., arg_n] → [..., result]`. KosExpression/KosFunction: new frame pushed, in-loop execution. Python callable: may suspend on awaitable.
- **Invariants**: Stack has at least 1 + arg elements. See execution-invariants for in-loop flattening.

**Example:** Call with 1 argument. Pool: 0 = fn, 1 = 42.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (fn)   | stack: [..., callable] |
| 1     | CONST  | 1   | 1 (42)   | stack: [..., callable, 42] |
| 2     | CALL   | 1   | 1        | pop callable + 1 arg; push result. stack: [..., result] |
| 3     | RETURN | 1   | 1        | |

---

### KOS_OP_CALL_NAMES, KOS_OP_CALL_STAR

- **Style**: COUNT, STAR.
- **Status**: Stub.
- **Stack**: (Intended: call with keyword names or *args/**kwargs.)
- **Intended (not yet implemented):** Call with keyword arguments (CALL_NAMES) or
  *args/**kwargs (CALL_STAR). **Current:** Stub; runtime error if executed.
- **Example:** (Stub; no runtime table.)

---

### KOS_OP_CALL_ATTR, KOS_OP_CALL_ATTR_NAMES, KOS_OP_CALL_ATTR_STAR

- **Style**: COUNT, COUNT, STAR.
- **Status**: Stub.
- **Stack**: (Intended: method call: obj, method_name, args.)
- **Example:** (Stub; no runtime table.)

---

### KOS_OP_RECUR, KOS_OP_RECUR_NAMES, KOS_OP_RECUR_STAR

- **Style**: COUNT, COUNT, STAR.
- **Status**: Stub.
- **Stack**: (Intended: explicit recursion; call current function.)
- **Example:** (Stub; no runtime table.)

---

### KOS_OP_TAIL_CALL*, KOS_OP_TAIL_RECUR*

- **Style**: COUNT, STAR.
- **Status**: Stub.
- **Stack**: (Intended: tail call / tail recur; replace current frame.)
- **Example:** (Stub; no runtime table.)


## 18. Block control

### KOS_OP_PUSH_FOREACH

- **Style**: LABEL. Arg = break target pc (post-compile).
- **Status**: Implemented.
- **Stack**: Pops iterable (and loop_result); creates iterator; pushes FOREACH region. ITER_NEXT uses region's iterator.
- **Pushes:** KOS_DYN_REGION (static extent KOS_EXT_FOREACH; region holds iterator and loop state).
- **Invariants**: Jump locality for break target. Extent kind KOS_EXT_FOREACH.
- **Compiler form:** Pre-compile arg = string label (break target); post-compile arg = pc.

**Example:** for x in [1,2,3]: body. Stack at entry: [..., loop_result, iterable]. Pool: 0 = [1,2,3].

| index | opcode       | arg | arg view | comment |
|------:|--------------|-----|----------|---------|
| 0     | CONST        | 0   | 0 (None) | stack: [..., loop_result] |
| 1     | CONST        | 1   | 1 ([1,2,3]) | stack: [..., loop_result, iterable] |
| 2     | PUSH_FOREACH | 7   | 7 (-> 7) | pop both; push FOREACH region; break → 7 |
| 3     | ITER_NEXT    | 7   | 7 (-> 7) | next from region; else → 7. stack: [..., x] |
| 4     | POP          | 1   | 1        | body: consume x |
| 5     | JUMP         | 3   | 3 (-> 3) | loop back |
| 6     | POP_LOOP     | 0   | 0        | |
| 7     | LABEL        | —   | —        | break / exhausted |

---

### KOS_OP_PUSH_WITH, KOS_OP_PUSH_BEGIN, KOS_OP_PUSH_CLASS

- **Status**: Stub.
- **Pushes (intended):** PUSH_WITH → KOS_DYN_REGION (KOS_EXT_WITH); PUSH_BEGIN → KOS_DYN_REGION (KOS_EXT_BEGIN); PUSH_CLASS → region or dedicated class extent per design.
- **Example:** (Stub; no runtime table.)

---

### KOS_OP_PUSH_LOOP

- **Style**: LABEL. Arg = break target pc (post-compile).
- **Status**: Implemented.
- **Stack**: Pushes LOOP region and stack block. Break/continue use extent break_pc/continue_pc.
- **Pushes:** KOS_DYN_REGION (static extent KOS_EXT_LOOP; plus stack block for loop body).
- **Invariants**: Extent kind KOS_EXT_LOOP. Jump locality for break/continue.
- **Compiler form:** Pre-compile arg = string label (break target); post-compile arg = pc.

**Example:** while loop. Break target = 6.

| index | opcode    | arg | arg view | comment |
|------:|-----------|-----|----------|---------|
| 0     | PUSH_LOOP | 6   | 6 (-> 6) | push LOOP region; break → 6 |
| 1     | CONST     | 0   | 0 (False)| condition |
| 2     | JUMP_FALSE| 6   | 6 (-> 6) | exit loop if false |
| 3     | …         | …   | …        | body |
| 4     | JUMP      | 1   | 1 (-> 1) | continue (loop head) |
| 5     | POP_LOOP  | 0   | 0        | |
| 6     | LABEL     | —   | —        | break target |

---

### KOS_OP_PUSH_NS

- **Style**: COUNT. Arg = 0 or number of values to bind. 0: push namespace with NSPEC only; >0: NSPEC plus value/name bindings.
- **Status**: Implemented.
- **Stack**: Pops values/names as per arg; pushes namespace scope (KosDynNs).
- **Pushes:** KOS_DYN_NS (static extent KOS_EXT_NS; stores saved namespace for restore on POP_NS).
- **Invariants**: Extent kind KOS_EXT_NS.

**Example:** Push namespace with 0 (NSPEC only). Stack unchanged except chain.

| index | opcode  | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | PUSH_NS| 0   | 0        | push NS region; chain: [..., KosDynNs] |
| 1     | …      | …   | …        | body |
| 2     | POP_NS | 0   | 0        | pop NS; restore previous namespace |

---

### KOS_OP_PUSH_TRY

- **Style**: LABEL. Handler targets in extent table.
- **Status**: Implemented.
- **Stack**: Pushes TRY region. Exception dispatch uses kos_try_handler_for.
- **Pushes:** KOS_DYN_REGION (static extent KOS_EXT_TRY; handler targets in extent descriptor).
- **Invariants**: Extent kind KOS_EXT_TRY. Handler targets (except_pc, finally_pc, exit_pc) from extent descriptor.

**Example:** try/except. Extent table holds except_pc, finally_pc, exit_pc.

| index | opcode  | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | PUSH_TRY | 0  | 0        | push TRY region; extent table: except_pc, etc. |
| 1     | …      | …   | …        | try body |
| 2     | POP_TRY | 0   | 0        | normal exit: pop TRY |
| 3     | JUMP   | 5   | 5 (-> 5)  | skip handler |
| 4     | LABEL  | —   | —        | except handler (except_pc) |
| 5     | LABEL  | —   | —        | exit |

---

### KOS_OP_PUSH_FINALLY

- **Style**: LABEL. Arg = handler/finally target.
- **Status**: Implemented.
- **Stack**: Pushes FINALLY region; extent table holds finally_pc and related targets.
- **Pushes:** KOS_DYN_REGION (static extent KOS_EXT_FINALLY).
- **Invariants**: Extent kind KOS_EXT_FINALLY.

**Example:** Push finally block. Arg = finally block pc (or extent holds it).

| index | opcode      | arg | arg view | comment |
|------:|-------------|-----|----------|---------|
| 0     | PUSH_FINALLY| 3   | 3 (-> 3) | push FINALLY; finally code at 3 |
| 1     | …           | …   | …        | protected body |
| 2     | POP_FINALLY | 0   | 0        | normal exit |
| 3     | LABEL       | —   | —        | finally block |
| 4     | END_FINALLY | 0   | 0        | (conceptual) |

---

### KOS_OP_CLEAR_FINALLY, KOS_OP_CLEAR_LOOP, KOS_OP_CLEAR_TRY

- **Status**: Implemented. Used for cleanup on non-local exit (break/continue/exception).
- **Stack**: Clear region state; do not pop (POP_* does that at target).
- **Example:** On break: CLEAR_LOOP (and possibly CLEAR_TRY) then jump to break_pc; POP_LOOP at target.

---

### KOS_OP_POP_LOOP, KOS_OP_POP_NS, KOS_OP_POP_TRY, KOS_OP_POP_FINALLY

- **Status**: Implemented.
- **Stack**: Pop corresponding region from dynamic chain.
- **Invariants**: Top of chain must match the POP kind. pc == pop_pc is outside block.

**Example:** POP_NS. Pool: N/A.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 2     | POP_NS | 0   | 0        | pop KosDynNs; restore saved namespace |

---

### KOS_OP_POP_WITH, KOS_OP_POP_BEGIN

- **Status**: Stub.
- **Example:** (Stub; no runtime table.)


## 19. Object protocol

Object protocol opcodes: single Python operation on TOS (and optionally below); pop operands, push result. All **Implemented** unless noted.

- **Comparisons**: OBJECT_EQ, OBJECT_NE, OBJECT_LT, OBJECT_LE, OBJECT_GT, OBJECT_GE — stack `[..., a, b] → [..., result]`.
- **Predicates**: OBJECT_IN, OBJECT_NOT_IN, OBJECT_IS, OBJECT_IS_NOT; OBJECT_IS_INSTANCE (COUNT), OBJECT_IS_SUBCLASS (COUNT).
- **Numeric**: OBJECT_ADD, OBJECT_SUBTRACT, OBJECT_MULTIPLY, OBJECT_POWER, OBJECT_FLOOR_DIVIDE, OBJECT_TRUE_DIVIDE, OBJECT_MODULO, OBJECT_* (bit ops, matrix multiply).
- **General**: OBJECT_ITER, OBJECT_AITER, OBJECT_LEN, OBJECT_NOT, OBJECT_REPR, OBJECT_STR, OBJECT_TYPE, OBJECT_DIR, OBJECT_VARS, OBJECT_TRUTH, OBJECT_DIVMOD, OBJECT_BIT_INVERT, OBJECT_BIT_LEFT, OBJECT_BIT_RIGHT.

**Example (OBJECT_ADD):** Arg = number of operands (e.g. 2 for binary add). Pop N values, push result (or await). Pool: 0 = 1, 1 = 2.

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 0     | CONST      | 0   | 0 (1)    | stack: [..., 1] |
| 1     | CONST      | 1   | 1 (2)    | stack: [..., 1, 2] |
| 2     | OBJECT_ADD| 2   | 2        | stack: [..., 3] |
| 3     | RETURN     | 1   | 1        | |

**Example (OBJECT_LEN):** Unary; TOS = object. Pop 1, push len(TOS).

| index | opcode    | arg | arg view | comment |
|------:|-----------|-----|----------|---------|
| 0     | CONST     | 0   | 0 ([1,2,3]) | stack: [..., obj] |
| 1     | OBJECT_LEN| 0   | 0        | stack: [..., 3] |


## 20. Delimited continuations

### KOS_OP_PUSH_PROMPT

- **Style**: CONST. Arg = prompt_id (constant pool index, post-compile).
- **Status**: Implemented.
- **Stack**: No stack change. Pushes prompt onto dynamic chain.
- **Pushes:** KOS_DYN_PROMPT (static extent KOS_EXT_PROMPT; SHIFT captures up to this prompt).
- **Invariants**: SHIFT captures up to this prompt.
- **Compiler form:** Pre-compile arg may be prompt id value or symbol; post-compile arg = pool index.

**Example:** reset (push prompt). Pool: 0 = prompt_id.

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 0     | PUSH_PROMPT| 0   | 0 (pid)  | push prompt onto chain |
| 1     | SET_PROMPT_HANDLER | 3 | 3 (-> 3) | handler at 3 |
| 2     | …          | …   | …        | body (may SHIFT) |
| 3     | LABEL      | —   | —        | handler: (k, val) |
| 4     | POP_PROMPT | 0   | 0        | pop prompt |

---

### KOS_OP_SET_PROMPT_HANDLER

- **Style**: LABEL. Arg = handler pc (post-compile).
- **Status**: Implemented.
- **Stack**: No stack change. Sets handler for top prompt.
- **Invariants**: Must be immediately after PUSH_PROMPT (or before code that can capture).
- **Compiler form:** Pre-compile arg = string label (handler block); post-compile arg = pc.

**Example:** See PUSH_PROMPT table; arg = 3 (-> 3).

| index | opcode           | arg | arg view | comment |
|------:|------------------|-----|----------|---------|
| 1     | SET_PROMPT_HANDLER | 3  | 3 (-> 3) | handler for top prompt at pc 3 |

---

### KOS_OP_POP_PROMPT

- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: No stack change. Pops prompt from dynamic chain.
- **Invariants**: Top of chain must be the matching prompt.

**Example:** End of reset body.

| index | opcode    | arg | arg view | comment |
|------:|-----------|-----|----------|---------|
| 4     | POP_PROMPT| 0   | 0        | pop prompt from chain |

---

### KOS_OP_SHIFT

- **Style**: CONST. Arg = prompt_id (constant pool index, post-compile).
- **Status**: Implemented.
- **Stack**: Captures continuation from current point up to prompt; aborts to prompt handler with (continuation, value). Value typically from TOS.
- **Invariants**: No PYCALL node between current position and prompt. Capture validated by kos_validate_capture.
- **Compiler form:** Pre-compile arg may be prompt id value/symbol; post-compile arg = pool index.

**Example:** shift k: body. Capture k, pass to handler. Pool: 0 = prompt_id.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 2     | CONST  | 1   | 1 (42)   | stack: [..., 42] (value to send to handler) |
| 3     | SHIFT  | 0   | 0 (pid)  | capture continuation; abort to prompt handler with (k, 42) |

---

### KOS_OP_INVOKE_K

- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: Pops value, then continuation (TOS); invokes continuation with value. Transfer plan runs; wind/unwind executed.
- **Invariants**: Continuation must be valid (same context, usable state). Multi-shot: invocation clones state.

**Example:** Invoke continuation k with value 100. Stack before: [..., 100, k].

| index | opcode  | arg | arg view | comment |
|------:|---------|-----|----------|---------|
| 0     | …       | …   | …        | stack: [..., value, continuation] |
| 1     | INVOKE_K| 0   | 0        | pop value, pop k; invoke k(value) |

---

### KOS_OP_INVOKE_K_VAR

- **Style**: CONST. Arg = constant pool index of name (post-compile).
- **Status**: Implemented.
- **Stack**: Pops value; looks up continuation by name; invokes it with value.
- **Invariants**: Name must be bound to a continuation (e.g. from shift handler).
- **Compiler form:** Pre-compile arg = name (string/symbol); post-compile arg = pool index.

**Example:** Invoke continuation bound to name "k". Pool: 0 = "k".

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 0     | CONST      | 1   | 1 (99)   | stack: [..., 99] |
| 1     | INVOKE_K_VAR | 0 | 0 ('k')  | pop 99; lookup "k"; invoke k(99) |


## 21. Undelimited continuations (call/cc)

CAPTURE_CC captures the continuation of the CC dynamic extent (from the current
pc until the extent is exited). Invoking that continuation is a **non-local
jump to the POP_CC opcode**, with a value pushed onto the stack; POP_CC then
runs (pops the CC block) and execution continues at the next instruction (the
PUSH_CC resume target).

### KOS_OP_PUSH_CC

- **Style**: NOARG. The instruction has no argument. The resume PC (instruction
  index to continue at when the continuation is invoked) is **not** stored in
  the bytecode; the evaluator obtains it from the **block table** (extent
  table): the CC extent for this PUSH_CC has `pop_pc` at the matching POP_CC,
  and the resume PC is `pop_pc + 1` (the instruction immediately after POP_CC).
- **Stack**: Pushes CC block onto dynamic chain. Normal exit: body runs, then
  POP_CC runs, then execution continues at the next instruction (resume). On
  invoke: jump to POP_CC with value on stack; POP_CC runs; continue at next
  instruction (same resume PC from block table).
- **Pushes:** KOS_DYN_CC (static extent KOS_EXT_CC).
- **Example:** (call/cc (lambda (k) (k 42))) → 42. Body captures k, pushes 42,
  CALL 1 invokes k(42); that jumps to POP_CC (index 4) with stack [..., 42];
  POP_CC runs; execution continues at 5 (resume), RETURN 1 yields 42.

| index | opcode     | arg | arg view   | comment |
|------:|------------|-----|------------|---------|
| 0     | PUSH_CC    | 0   | 0          | push CC block; resume at 5 (= pop_pc+1 from block table) |
| 1     | CAPTURE_CC | 0   | 0          | stack before: [...]; after: [..., continuation] |
| 2     | CONST      | 0   | 0 (42)     | stack: [..., continuation, 42] |
| 3     | CALL       | 1   | 1          | invoke continuation with one arg → jump to POP_CC (4) with [..., 42] |
| 4     | POP_CC     | 0   | 0          | invoke lands here with value on stack; pop CC; next pc = 5 (resume) |
| 5     | RETURN     | 1   | 1          | yield TOS (42) |

---

### KOS_OP_POP_CC

- **Style**: NOARG.
- **Stack**: Pops CC block from dynamic chain. When the captured continuation
  is invoked, the VM jumps *to* this opcode with the invoked value (or values)
  on the stack; this opcode then runs (pops the CC block) and execution
  continues at the next instruction (the PUSH_CC resume target).
- **Example:** In the PUSH_CC table above, POP_CC at index 4 is the jump target
  when the continuation is invoked; stack holds the value(s) passed to the
  continuation; after POP_CC, execution continues at index 5 (resume).

---

### KOS_OP_CAPTURE_CC

- **Style**: NOARG.
- **Stack**: Captures the continuation of the CC dynamic extent (from current pc
  until the extent is exited at POP_CC); pushes it (KosPyContinuation) onto
  stack. Stack effect: `[...] → [..., continuation]`. Invoking the continuation
  is a non-local jump to the POP_CC opcode with a value pushed onto the stack.
- **Example:** See the PUSH_CC disassembly table: at index 1, CAPTURE_CC pushes
  the continuation. Invoking it (e.g. CALL 1 with 42) jumps to POP_CC (index 4)
  with stack [..., 42]; POP_CC runs; execution continues at index 5 (resume).


## 22. Dynamic wind

### KOS_OP_PUSH_WIND

- **Style**: LABEL. Arg = after_block label (pc of code after unwind hook).
- **Status**: Stub.
- **Stack**: Pushes wind extent; before/after hooks run on entry/exit.
- **Pushes:** KOS_DYN_WIND (static extent KOS_EXT_WIND).
- **Example:** (Stub; no runtime table.)

---

### KOS_OP_POP_WIND

- **Style**: NOARG.
- **Status**: Stub.
- **Stack**: Pops wind from dynamic chain; runs unwind hook.
- **Example:** (Stub; no runtime table.)

---

### KOS_OP_END_WIND

- **Style**: NOARG.
- **Status**: Stub.
- **Stack**: Sentinel that terminates hook block execution (VM-native hooks).
- **Example:** (Stub; no runtime table.)


## 23. Region control

### KOS_OP_PUSH_REGION

- **Style**: LABEL. Arg = handler label (pc for non-local exit).
- **Status**: Stub.
- **Stack**: Pushes region for block-local control.
- **Pushes:** KOS_DYN_REGION (static extent kind and handler from arg/table).
- **Example:** (Stub.) Intended: arg = handler pc.

| index | opcode      | arg | arg view | comment |
|------:|-------------|-----|----------|---------|
| 0     | PUSH_REGION | 4   | 4 (-> 4) | push region; handler at 4 |
| 1     | …           | …   | …        | body |
| 2     | POP_REGION  | 0   | 0        | pop region |
| 4     | LABEL       | —   | —        | handler |

---

### KOS_OP_POP_REGION

- **Style**: NOARG.
- **Status**: Stub.
- **Stack**: Pops region from dynamic chain.
- **Example:** (Stub.) See PUSH_REGION.


## 24. Sentinel

### KOS_OP_THEEND

- **Style**: NOARG.
- **Status**: No runtime implementation.
- **Stack**: Unreachable for normal execution.
- **Invariants**: If execution reaches THEEND, the VM raises RuntimeError. THEEND must always be last in opcodes.incl.

**Example:** Sentinel at end of code; no legitimate execution path reaches it.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | RETURN | 1   | 1        | normal exit |
| 1     | THEEND | 0   | 0        | unreachable; VM raises if pc lands here |


---

## See also

- [interpreter/bytecode.md](interpreter/bytecode.md) — Bytecode format, KosCode layout, API
- [interpreter/bytecode-metadata-tables.md](interpreter/bytecode-metadata-tables.md) — Extents, location table, jump locality
- [interpreter/execution-invariants.md](interpreter/execution-invariants.md) — Execution model
- [interpreter/evaluator.md](interpreter/evaluator.md) — Evaluation loop
- [FEATURES.md](FEATURES.md) — Implementation status and feature list
- `koskari/lib/opcodes.incl` — Authoritative opcode enum and order
- `koskari.dis` — Runtime disassembly of KosCode objects


## Document status

This document is the design target for Koskari opcodes. When semantics or
invariants change, update this file and the implementation together. Last
substantive update: pre/post-compile representation (§1.5–1.8), LABEL/POSITION
stripping, quick-reference index, glossary, maintaining checklist, and
intended/current notes for stubs.
