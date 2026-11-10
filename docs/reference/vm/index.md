# Koskari bytecode

The instruction set of the Koskari evaluator: what a compiled code object
holds, how the evaluator runs it, and one section per opcode with its
stack effect, invariants, and a disassembly. The evaluator itself is
`koskari.lib`; the compiler that emits this bytecode is `koskari.compile`;
`koskari.dis` disassembles a code object into the tables shown here.

## 1. The evaluator

### 1.1 Model

A compiled function or expression is a **code object** (`KosCode`): a
fixed array of `(opcode, arg)` instructions, a constant pool, an extent
table describing its blocks, a position table mapping instructions back
to source, and an extra table for the few opcodes whose argument is a
tuple. Instructions are addressed by index, the **pc**.

The evaluator is stackless. A Koskari call does not consume the C stack:
each running code object has a frame on the heap with an **operand
stack**, and calls, returns, yields, awaits, and continuation jumps move
between frames without recursion in C. That is what lets tail calls
loop and any function suspend. Python's recursion limit does not count
these frames; frame depth has its own process-wide limit, a guard
against runaway memory, and an install past it raises `RecursionError`.
The limit defaults to 10000; `koskari.lib.set_frame_depth_limit(n)`
sets it from 1 up to a cap of 65535 and
`koskari.lib.get_frame_depth_limit()` reads it. The Pyodide build
defaults to 1000 with a cap of 2000. A tail call reuses its
frame. A call from Python into Koskari re-enters the evaluator on the
C stack, and the entries nested on one thread are capped as well, at
65535 natively and 200 on the Pyodide build;
`koskari.lib.set_eval_nesting_limit(n)` lowers that cap and
`koskari.lib.get_eval_nesting_limit()` reads it.

Alongside the operand stack the evaluator keeps a **dynamic chain** of
nodes: one per active frame, loop, `try`, `finally`, namespace, context
manager, prompt, continuation capture, dynamic-wind extent, and
parameterize extent. `PUSH_*` opcodes push a node and `POP_*` and
`CLEAR_*` opcodes pop one. Exceptions, `break`, `return`, and
continuation invocations walk the chain to find where control goes and
what must run on the way.

Names resolve through namespaces. A `VAR*` opcode names a variable by
constant-pool index and looks it up through the frame's namespace chain;
`convert_fast` lowers a name with a fixed slot to its `FAST*` twin, which
indexes the frame's local slots directly.

A Koskari stretch that never returns to Python's own loop still
cooperates with the interpreter: at its next cooperation point (a
backward jump, a call, or a resume) the evaluator yields the GIL to a
waiting thread, and a pending signal, a pending call added with
`Py_AddPendingCall`, or an asynchronous exception set with
`PyThreadState_SetAsyncExc` is run or raised there. An asynchronous
exception is an ordinary raise, which a Koskari `try` can catch.

### 1.2 Notation

- **Stack**: `[a, b, c]` with TOS, the top of stack, on the right.
  "Below TOS" is the second element.
- **Stack effect**: written as before and after, for example
  `[..., callable, a, b] -> [..., result]`.
- **Chain**: an opcode that pushes or pops a dynamic chain node says so
  under **Pushes** or **Pops**, with the node kind and, where relevant,
  the static extent kind.
- **Anchors**: each opcode heading is `### KOS_OP_NAME`, so
  `#kos_op_name` links to it from the per-opcode pages and elsewhere.

### 1.3 Glossary

- **bind**: establish or replace the mapping from a name to a ref. See 1.9.
- **store**: write a value through the ref that already resolves a name.
  See 1.9.
- **pc**: the instruction index.
- **TOS**: the top of the operand stack.
- **cpool**: the constant pool, the code object's read-only array of
  constants and names, indexed as `cpool[i]`.
- **RAW** (field kind): an EXTRA row field holding a number the
  instruction interprets itself, such as `CHECK_COMPARE`'s negate
  flag or a matcher instruction's stack depth, width, count, flag or
  core type ordinal. Finalize leaves it unchanged.
- **extent**: a statically described block in the code object's extent
  table, with a kind (`KOS_EXT_TRY`, `KOS_EXT_LOOP`, `KOS_EXT_FOREACH`,
  `KOS_EXT_PROMPT`, `KOS_EXT_NS`, `KOS_EXT_COLLECT`, and others), enter
  and exit pcs, and kind-specific targets.
- **dynamic chain**: the runtime chain of nodes. The node kinds are the
  `KosDynKind` enum in `koskari/lib/dynchain.h`, in enum order:
  - `KOS_DYN_FRAME` -- function activation record.
  - `KOS_DYN_PROMPT` -- delimiter for delimited continuations (`reset`).
  - `KOS_DYN_DELIM` -- return marker for a delimited invoke, carrying the
    return metadata.
  - `KOS_DYN_WIND` -- dynamic-wind extent with before and after hooks.
  - `KOS_DYN_TRY` -- try region.
  - `KOS_DYN_FINALLY` -- finally region.
  - `KOS_DYN_LOOP` -- loop region.
  - `KOS_DYN_FOREACH` -- foreach region.
  - `KOS_DYN_NS` -- namespace scope, pushed by `PUSH_NS` and popped by
    `POP_NS`.
  - `KOS_DYN_COLLECT` -- collector scope (`PUSH_COLLECT` / `POP_COLLECT`),
    which pushes its namespace on pop.
  - `KOS_DYN_CC` -- undelimited continuation block (`call/cc`).
  - `KOS_DYN_PARAM` -- parameter bindings for a parameterize extent.
  - `KOS_DYN_WITH` -- context manager (`with` and `dyn-with`). Single-shot
    versus multi-shot is a field on the node, not a second kind.
  - `KOS_DYN_SHIFT` -- shift handler bookmark. Owns the handler body's
    operand segment; no frame or namespace of its own.
  - `KOS_DYN_GEN` -- generator node. Intercepts yield, return, and raise,
    and denies shift.
  - `KOS_DYN_UNKNOWN` -- sentinel past the last kind; never a live node.
- **pre-compile, post-compile**: the instruction stream as the compiler
  emits it, with labels as strings and constants as values, and the
  bytecode after finalize, with labels as pcs and values as pool
  indices. See 1.7.
- **safety net**: `LABEL` and `POSITION` are stripped by finalize; if one
  survives it executes as a no-op that advances the pc.

### 1.4 Disassembly tables

A disassembly in this document is a table with five columns:

| Column | Meaning |
|--------|---------|
| **index** | the instruction's pc |
| **opcode** | the opcode name |
| **arg** | the raw argument, a uint16 |
| **arg view** | the decoded argument: a constant as `index (repr)`, a branch as `N (-> N)` |
| **comment** | the stack before the instruction, or its control-flow effect |

Tables are post-compile unless marked otherwise.

### 1.5 Invariants

Unless an opcode's section says otherwise:

- **Jump locality**: a jump opcode (`JUMP`, `JUMP_FALSE`, `JUMP_TRUE`,
  `JUMP_AND`, `JUMP_OR`, `ITER_NEXT`, `CHECK_*`) targets a pc inside the
  current extent or the extent's `pop_pc`, which leaves the block.

The **Invariants** bullets under each opcode, with this list, are the
contract between compiler and evaluator: the compiler emits code that
satisfies them and the evaluator relies on them. A violation is
undefined behaviour, asserted in debug builds.

### 1.6 Implementation status

Every opcode has a runtime handler except `THEEND`, the end-of-code
sentinel, which raises if executed. `LABEL` and `POSITION` exist only in
the pre-compile stream and are stripped by finalize. This document is
the record of that status; a new opcode is added to `opcodes.incl`,
given a handler, and given a section here.

### 1.7 Compilation: pre-compile and post-compile

The compiler emits a **pre-compile** stream of instructions, and a
**finalize** pass produces the **post-compile** bytecode stored in the
code object: labels become pcs, values become constant-pool indices,
the extent and position tables are derived, and `VAR` may lower to
`FAST`. Finalize is distinct from `ByteCodeProcessor`, the earlier
peephole pass over the instruction list described in 1.8. Many opcodes
mean something different by their argument in each form.

**Pipeline**

1. **Pre-compile stream**: Pairs of (opcode, arg). Labels are str or int names.
   Constants and names may appear as literal values (e.g. `CONST "hello"`, `VAR
   "x"`). LABEL and POSITION may be present.
2. **Finalize**: Resolve labels to instruction indices (pcs). Build the
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
| str or int label name  | pc (uint16)             | JUMP, JUMP_FALSE, JUMP_TRUE, JUMP_AND, JUMP_OR, ITER_NEXT, CHECK_*, PUSH_TRY, PUSH_FINALLY, PUSH_WIND |
| Literal value          | Constant pool index     | CONST |
| Name (string/symbol)   | Constant pool index     | VAR, VAR_BIND, VAR_CLEAR, VAR_TAKE, VAR_REF, VAR_STORE; ATTR*, ITEM_CONST* (key) |
| `(name, value, ...)`     | Extra-table index       | VAR_STORE_CONST (section 12); may lower to FAST_STORE_CONST (section 13) |
| Name -> slot (optional) | Fast local index       | VAR may be lowered to FAST*; then arg is slot index |
| (unchanged)            | (unchanged)             | NOARG, COUNT, etc. |
| 0 (no arg; break target derived at runtime from extent table `pop_pc + 1`) | 0 (unused) | PUSH_FOREACH, PUSH_LOOP |

**Compile-time only**: LABEL and POSITION are **omitted** in the post-compile
bytecode under normal circumstances. Finalize strips them and adjusts any branch
targets that pointed at a stripped instruction. Position data from POSITION is
recorded in the code object's position table only. If a LABEL or POSITION is
retained (e.g. by a different code path), it has a cursory no-op implementation
as a safety net; at runtime it advances PC only.

Where the pre form differs from the bytecode form, opcode sections below include
a **Compiler form** line. Disassembly tables are **post-compile** unless marked
otherwise.

### 1.8 Peephole compression: `ByteCodeProcessor`

The compiler runs `ByteCodeProcessor` over a fragment's instruction list
inside `Compiler.complete`, before the constant-pool merge,
`convert_fast`, `demote_tail_calls_if_yields`, and construction of the
code object. The implementation is `ByteCodeProcessor`, with `CHECK_SQUASHES` and
`SWITCH_SQUASHES` in `koskari/compile/processor.py`; the tests are in
`tests/compile/test_compress.py`.

**Purpose.** Shorten or simplify the pre-finalize `(opcode, arg)` stream in a
single forward pass. The pass keeps a short output deque; most rules compare the
current instruction with the **last** emitted one (`squished[-1]`). The
implementation seeds the deque with a leading **`KOS_OP_NOOP`** and drops it at
the end if any real instructions were emitted.

**Non-goals.** `ByteCodeProcessor` does **not** resolve labels to PCs, build the constant
pool, derive extent or position tables, lower VAR to FAST (**`convert_fast`**),
or demote tail calls when yields are present. Those happen later; see section 1.7.

**Transformations** (order matches the `for (op, arg) in instr` branch order in
`ByteCodeProcessor`):

| Category | Rule |
|----------|------|
| Dead stack | **`KOS_OP_CONST`** followed by **`KOS_OP_POP`** with **arg 1** -> drop both. **`KOS_OP_DUP`** with arg n followed by **`KOS_OP_POP`** with arg n -> drop both; with a larger POP arg m -> **`KOS_OP_POP`** with arg m - n; with a smaller POP arg both stay, since DUP copies its slots in order. **`KOS_OP_DUP`** or **`KOS_OP_POP`** with **arg 0** -> dropped. |
| Constant fold | **`KOS_OP_OBJECT_NOT`** with prior **`KOS_OP_CONST`** -> replace the pair with a single **`KOS_OP_CONST`** whose value is **`not`** the prior constant (Python `bool` semantics for the folded value). |
| Constant + `JUMP_TRUE` | Prior **`KOS_OP_CONST`**: if truthy, replace with **`KOS_OP_JUMP`** to the same label; if falsy, drop both (no jump). |
| Constant + `JUMP_FALSE` | Prior **`KOS_OP_CONST`**: if the constant is falsy, unconditional **`KOS_OP_JUMP`**; if truthy, drop both. |
| Compare + `JUMP_FALSE` | Prior opcode is in **`CHECK_SQUASHES`** (see table below): delete the prior instruction and replace **`JUMP_FALSE`** with the mapped single opcode (same jump target **arg**). The **`KOS_OP_OBJECT_NOT`** result is dispatched again as a **`KOS_OP_JUMP_TRUE`**, so a compare ahead of the `not` fuses into a negated **`KOS_OP_CHECK_COMPARE`**. |
| `SWITCH_EQ` fusion | Prior opcode in **`SWITCH_SQUASHES`**: replace **`KOS_OP_SWITCH_EQ`** with the mapped opcode; **arg** becomes **`(switch_label, *pargs[:3])`** if the prior **arg** was a tuple, else **`(switch_label, pargs, None, None)`** ( **`switch_label`** is this instruction's original **arg**). |
| `LABEL` vs prior jump | If the **`KOS_OP_LABEL`** name equals the prior instruction's target: for prior **`KOS_OP_JUMP`**, drop the jump; for prior **`KOS_OP_JUMP_TRUE`** / **`KOS_OP_JUMP_FALSE`**, if the instruction before the jump is **`KOS_OP_CONST`**, drop const and jump; else replace the conditional jump with **`KOS_OP_POP`** **1**. |
| Implicit `None` | **`KOS_OP_CONST`** **`None`** immediately before **`KOS_OP_RETURN`** / **`KOS_OP_BREAK`** / **`KOS_OP_CONTINUE`** / **`KOS_OP_YIELD`** with **arg 1** -> remove the const and set the control opcode's **arg** to **0**. |
| Built tuple | **`KOS_OP_BUILD_ARGS`** **n** (n >= 2) immediately before **`KOS_OP_RETURN`** / **`KOS_OP_BREAK`** / **`KOS_OP_CONTINUE`** / **`KOS_OP_YIELD`** with **arg 1** -> drop the build and set the control opcode's **arg** to **n**; its N form builds the same tuple. n of 0 and 1 stay, since `()` is not `None` and a one-tuple is not its element. |
| `CAR` / `CDR` merge | Two consecutive **same** opcodes **`KOS_OP_CAR`** or **`KOS_OP_CDR`** -> one instruction with **args summed**. |
| `VAR_STORE` fusion | Prior **`KOS_OP_CONST`** `value` immediately followed by **`KOS_OP_VAR_STORE`** `name` -> **`KOS_OP_VAR_STORE_CONST`** with arg **`(name, value, None, None)`** (drop the **`CONST`**). Only the **immediate** prior instruction counts. |
| Epilogue | If the stream does not end with **`KOS_OP_RETURN`**: if the last op is **`KOS_OP_CONST`** **`None`**, replace it with **`KOS_OP_RETURN`** **0**; otherwise append **`KOS_OP_RETURN`** **1**. |
| Control feeder (fusion pass) | In the later `fuse_feeders` pass, **`KOS_OP_RETURN`** **1** or **`KOS_OP_YIELD`** **1** fed by a bare **`KOS_OP_CONST`** becomes **`KOS_OP_RETURN_CONST`** / **`KOS_OP_YIELD_CONST`**, and fed by a bare **`KOS_OP_VAR`** becomes **`KOS_OP_RETURN_VAR`** / **`KOS_OP_YIELD_VAR`**, which lower to the FAST twins; the epilogue's appended **`KOS_OP_RETURN`** **1** fuses the same way. |
| Truth-test feeder (fusion pass) | In the later `fuse_feeders` pass, **`KOS_OP_JUMP_FALSE`** or **`KOS_OP_JUMP_TRUE`** fed by a bare **`KOS_OP_VAR`** becomes **`KOS_OP_JUMP_FALSE_VAR`** / **`KOS_OP_JUMP_TRUE_VAR`**, which lower to the FAST twins; a **`KOS_OP_CONST`** feeder was folded by the constant rules above. |
| Short-circuit feeder (fusion pass) | In the later `fuse_feeders` pass, **`KOS_OP_JUMP_AND`** or **`KOS_OP_JUMP_OR`** fed by a bare **`KOS_OP_VAR`** becomes **`KOS_OP_JUMP_AND_VAR`** / **`KOS_OP_JUMP_OR_VAR`**, which read the value from the row and push it only on the jumping path; they lower to the FAST twins. |
| Item feeders (fusion pass) | In the later `fuse_feeders` pass an item row folds the run of bare **`KOS_OP_VAR`** / **`KOS_OP_CONST`** loads directly ahead of it, which in stack order is the value, then the key, then the object, into the row that names them (section 15): **`KOS_OP_ITEM`** fed by `VAR obj, VAR key` becomes **`KOS_OP_VAR_ITEM_VAR`**, **`KOS_OP_ITEM_STORE`** fed by `VAR obj, CONST key, CONST value` becomes **`KOS_OP_VAR_ITEM_CONST_STORE_CONST`**, and so on for CLEAR, TAKE, REF, and INPLACE, whose value feeders are re-emitted ahead of the fused row and whose single CONST operand folds into the `_CONST` twin. A CONST in the object position never folds. The rows a special shapes with the value on the stack fold a value feeder alone. The named rows lower to their FAST twins through a written-out map. |

**`CHECK_SQUASHES`** (prior opcode -> **LABEL** opcode replacing the **`JUMP_FALSE`** pair; the fused opcode inherits the jump's label):

| Prior | Replaced with |
|-------|----------------|
| `KOS_OP_OBJECT_NOT` | `KOS_OP_JUMP_TRUE` |
| `KOS_OP_EXCEPTION_MATCH` | `KOS_OP_CHECK_EXCEPTION` |
| `KOS_OP_NILP` | `KOS_OP_CHECK_NIL` |

**`OBJECT_COMPARE` fusion** (selector-keyed comparison prior -> **EXTRA** `(CMP, COUNT, LABEL, RAW)` check). The prior `KOS_OP_OBJECT_COMPARE` carries its selector and operand count in a CMPCOUNT arg; the fusion preserves both, emitting **`KOS_OP_CHECK_COMPARE`** with arg `(selector, count, label, 0)` so the chained comparison survives (`a < b < c` fuses just like `a < b`); the `JUMP_TRUE` pair fuses the same way with the fourth field, the negate flag, set to 1, so the check tests the negated chain.

**`SWITCH_SQUASHES`** (prior opcode -> fused switch opcode):

| Prior | Replaced with |
|-------|----------------|
| `KOS_OP_CONST` | `KOS_OP_SWITCH_EQ_CONST` |
| `KOS_OP_VAR` | `KOS_OP_SWITCH_EQ_VAR` |
| `KOS_OP_VAR_ATTR` | `KOS_OP_SWITCH_EQ_VAR_ATTR` |

**Examples.**
- `KOS_OP_OBJECT_COMPARE` `(EQ, count)`, **`KOS_OP_JUMP_FALSE`** `L` -> one **`KOS_OP_CHECK_COMPARE`** `(EQ, count, L, 0)` (selector and count carried from the comparison, label from the jump).
- `KOS_OP_OBJECT_COMPARE` `(EQ, count)`, **`KOS_OP_JUMP_TRUE`** `L` -> one **`KOS_OP_CHECK_COMPARE`** `(EQ, count, L, 1)`, which jumps when the chain holds.

### 1.9 Bind and store

Two opcode families write to a name, and they do different things.

| Operation | Meaning |
|-----------|---------|
| **bind** | establish or replace the mapping from the name to a ref; from now on the name resolves to this ref |
| **store** | write a value through the ref the name already resolves to; the name must already be bound |

`VAR_BIND` and `FAST_BIND` introduce or rebind a name. `VAR_STORE` and
`FAST_STORE` update the value at an existing binding. When the stored
value is a constant, `ByteCodeProcessor` fuses `CONST` and `VAR_STORE`
into `VAR_STORE_CONST`, which uses no stack, and `convert_fast` lowers
that to `FAST_STORE_CONST` for a local. The same split is the surface
distinction between `var` and `set!`; see
[Bindings and refs](../../guide/bindings-and-refs.md).

## 2. Opcode families

The opcodes by family, each linking to its section.

- [3. No-op and compile-time](#3-no-op-and-compile-time) -- NOOP, LABEL, POSITION
- [4. Stack](#4-stack) -- DUP, POP, UNPACK, UNPACK_STAR, UNPACK_VAR, UNPACK_FAST
- [5. Constants](#5-constants) -- CONST, CONST2, CONST3, CONST4
- [5.1 Pair construction and access](#51-pair-construction-and-access) -- CAR, CDR, CONS, PROPER, PAIR_SPLICE, BRACKET_SPLICE
- [6. Mode control](#6-mode-control) -- AWAIT, BREAK, CONTINUE, RAISE, RETURN, YIELD, RETURN_CONST, YIELD_CONST, YIELD_FROM
- [7. Iteration](#7-iteration) -- ITER_NEXT, ITER_NEXT_STORE_VAR, ITER_NEXT_STORE_FAST, OBJECT_NEXT, VAR_NEXT, FAST_NEXT
- [8. Jumps](#8-jumps) -- JUMP, JUMP_FALSE, JUMP_TRUE, JUMP_FALSE_VAR, JUMP_TRUE_VAR, JUMP_FALSE_FAST, JUMP_TRUE_FAST, JUMP_AND, JUMP_OR, JUMP_AND_VAR, JUMP_OR_VAR, JUMP_AND_FAST, JUMP_OR_FAST
- [9. Check jumps](#9-check-jumps) -- CHECK_COMPARE, CHECK_COMPARE_VAR, CHECK_COMPARE_FAST, CHECK_EXCEPTION
- [9.1 Switch dispatch](#91-switch-dispatch) -- SWITCH_EQ, SWITCH_EQ_CONST, SWITCH_EQ_VAR, SWITCH_EQ_FAST, SWITCH_EQ_VAR_ATTR, SWITCH_EQ_FAST_ATTR
- [9.2 Pattern matching](#92-pattern-matching) -- MATCH_DESCEND, MATCH_SINGLE, MATCH_FITS, MATCH_NEXT, MATCH_BOUND, MATCH_UNBOUND, MATCH_SYNC, MATCH_TAIL, MATCH_SCOPE, MATCH_SCOPE_NEXT, MATCH_RESET, MATCH_AT, MATCH_SPAN, MATCH_STORE, MATCH_CLEAR, MATCH_CAPTURES, MATCH_PASS, MATCH_LOAD, MATCH_APPEND, MATCH_MIN, MATCH_COLUMNS, MATCH_LITERAL, MATCH_CORE, MATCH_TYPE_VAR, MATCH_TYPE_FAST, MATCH_CLASS_VAR, MATCH_CLASS_FAST, MATCH_SAME, MATCH_FIELD_POS_VAR, MATCH_FIELD_POS_FAST, MATCH_FIELD_KW, MATCH_MAPPING, MATCH_KEY, MATCH_KEY_VAR, MATCH_KEY_FAST
- [10. Introspection](#10-introspection) -- GLOBALS, LOCALS, IMPORT, IMPORT_FROM, IMPORT_STAR
- [11. Exception handling](#11-exception-handling) -- EXCEPTION, EXCEPTION_CLEAR, EXCEPTION_MATCH
- [12. Variables by name](#12-variables-by-name) -- VAR, VAR2, VAR3, VAR4, VAR_BIND, VAR_CLEAR, VAR_CLEAR2/3/4, VAR_REF, VAR_STORE, VAR_STORE_CONST, VAR_ATTR*, VAR_ITEM*, VAR_ITER, VAR_NEXT, VAR_FORMAT, VAR_FORMAT_CONST, RETURN_VAR, YIELD_VAR
- [13. Fast variables](#13-fast-variables) -- FAST, FAST2, FAST3, FAST4, FAST_BIND, FAST_CLEAR, FAST_CLEAR2/3/4, FAST_REF, FAST_STORE, FAST_STORE_CONST, FAST_ATTR*, FAST_ITEM*, FAST_ITER, FAST_NEXT, FAST_FORMAT, FAST_FORMAT_CONST, RETURN_FAST, YIELD_FAST
- [14. Attributes](#14-attributes) -- ATTR, ATTR_CLEAR, ATTR_REF, ATTR_STORE
- [15. Items](#15-items) -- ITEM, ITEM_STORE, ITEM_INPLACE, ITEM_CLEAR, ITEM_TAKE, ITEM_REF, and the rows naming their object, key, and value
- [15.1 Reference dereference](#151-reference-dereference) -- DEREF
- [16. Argument production](#16-argument-production) -- BUILD_ARGS, BUILD_FUNCTION, BUILD_KWDS, etc.
- [17. Calls](#17-calls) -- CALL, CALL_NAMES, CALL_STAR, CALL_ATTR*, TAIL_*
- [18. Block control](#18-block-control) -- PUSH_FOREACH, PUSH_LOOP, PUSH_NS, PUSH_COLLECT, PUSH_TRY, PUSH_FINALLY, CLEAR_*, POP_*
- [18.1 Context managers (with / dyn-with)](#181-context-managers-with--dyn-with) -- PUSH_WITH, POP_WITH, PUSH_DYN_WITH, POP_DYN_WITH
- [19. Object protocol](#19-object-protocol) -- OBJECT_* (comparisons, predicates, numeric, general)
- [20. Delimited continuations](#20-delimited-continuations) -- PUSH_PROMPT, PUSH_SHIFT, POP_SHIFT, POP_PROMPT
- [21. Undelimited continuations (call/cc)](#21-undelimited-continuations-callcc) -- PUSH_CC, POP_CC, CAPTURE_CC
- [22. Dynamic wind](#22-dynamic-wind) -- PUSH_WIND, POP_WIND
- [23. Parameterize extent](#23-parameterize-extent-and-parameter-reads) -- PUSH_PARAM, PARAM, POP_PARAM
- [24. Sentinel](#24-sentinel) -- THEEND

### 2.1 Quick reference

| Opcode | Style | One-line effect | Section |
|--------|-------|------------------|---------|
| NOOP | NOARG | Advance PC only | [section 3](opcodes/kos-op-noop.md) |
| LABEL | COMPILE | Jump target; stripped post-compile | [section 3](opcodes/kos-op-label.md) |
| POSITION | COMPILE | Source position; stripped post-compile | [section 3](opcodes/kos-op-position.md) |
| DUP | COUNT | Duplicate top N stack elements | [section 4](opcodes/kos-op-dup.md) |
| POP | COUNT | Pop N elements | [section 4](opcodes/kos-op-pop.md) |
| UNPACK | COUNT | Realize TOS as list; push N scalars (seq[0] is TOS) | [section 4](opcodes/kos-op-unpack.md) |
| UNPACK_STAR | EXTRA | Prefix / middle / suffix unpack (two COUNT fields) | [section 4](opcodes/kos-op-unpack-star.md) |
| UNPACK_VAR | EXTRA | STORE seq[0], seq[1] into two VAR names | [section 4](opcodes/kos-op-unpack-var.md) |
| UNPACK_FAST | EXTRA | STORE seq[0], seq[1] into two fast slots | [section 4](opcodes/kos-op-unpack-fast.md) |
| CONST | CONST | Push constant from pool | [section 5](opcodes/kos-op-const.md) |
| CONST2 | EXTRA | Push two constants in row order | [section 5](opcodes/kos-op-const2.md) |
| CONST3 | EXTRA | Push three constants in row order | [section 5](opcodes/kos-op-const3.md) |
| CONST4 | EXTRA | Push four constants in row order | [section 5](opcodes/kos-op-const4.md) |
| CAR | COUNT | Apply car **n** times (**n** >= 1); **n** = 0 invalid | [section 5.1](opcodes/kos-op-car.md) |
| CDR | COUNT | Apply cdr **n** times (**n** >= 1); **n** = 0 invalid | [section 5.1](opcodes/kos-op-cdr.md) |
| CONS | COUNT | Pop N values; push (e_0 . ... . e_{N-1}) | [section 5.1](opcodes/kos-op-cons.md) |
| PROPER | COUNT | Pop N values; push proper list (e_0 ... e_{N-1}) | [section 5.1](opcodes/kos-op-proper.md) |
| BRACKET_CONS | COUNT | Pop N values; push the bracket pair chain [e_0 . ... . e_{N-1}] | [section 5.1](opcodes/kos-op-bracket-cons.md) |
| BRACKET_PROPER | COUNT | Pop N values; push the proper bracket list [e_0 ... e_{N-1}] | [section 5.1](opcodes/kos-op-bracket-proper.md) |
| PAIR_SPLICE | EXTRA | Concatenate pair/tuple segments into one list ending in nil or a stacked tail | [section 5.1](opcodes/kos-op-pair-splice.md) |
| BRACKET_SPLICE | EXTRA | Concatenate pair/tuple segments into one list with a bracket leading cell, ending in nil or a stacked tail | [section 5.1](opcodes/kos-op-bracket-splice.md) |
| AWAIT | COUNT | Suspend on awaitable; resume with value | [section 6](opcodes/kos-op-await.md) |
| BREAK | COUNT | Jump to loop break target | [section 6](opcodes/kos-op-break.md) |
| CONTINUE | COUNT | Jump to loop continue target | [section 6](opcodes/kos-op-continue.md) |
| RAISE | COUNT | Pop N, raise exception | [section 6](opcodes/kos-op-raise.md) |
| RETURN | COUNT | Pop the return value and deliver it to the frame's parent or the host | [section 6](opcodes/kos-op-return.md) |
| YIELD | COUNT | Pop the value and suspend the frame, delivering it to the consumer | [section 6](opcodes/kos-op-yield.md) |
| RETURN_CONST | CONST | Return the pooled constant; the `CONST` + `RETURN 1` fusion | [section 6](opcodes/kos-op-return-const.md) |
| YIELD_CONST | CONST | Yield the pooled constant; the `CONST` + `YIELD 1` fusion | [section 6](opcodes/kos-op-yield-const.md) |
| YIELD_FROM | NOARG | Delegate to sub-iterator/gen; PC-stable; send/throw/close | [section 6](opcodes/kos-op-yield-from.md) |
| ITER_NEXT | LABEL | Next from region iterator; else jump | [section 7](opcodes/kos-op-iter-next.md) |
| ITER_NEXT_STORE_VAR | EXTRA | Next from region iterator stored under the name; else jump; the `ITER_NEXT` + `VAR_STORE` fusion | [section 7](opcodes/kos-op-iter-next-store-var.md) |
| ITER_NEXT_STORE_FAST | EXTRA | Next from region iterator stored into the fast slot; else jump | [section 7](opcodes/kos-op-iter-next-store-fast.md) |
| OBJECT_NEXT | COUNT | Stack-based next (1 or 2 args) | [section 7](opcodes/kos-op-object-next.md) |
| JUMP | LABEL | Unconditional jump to pc | [section 8](opcodes/kos-op-jump.md) |
| JUMP_FALSE | LABEL | Jump if TOS false | [section 8](opcodes/kos-op-jump-false.md) |
| JUMP_TRUE | LABEL | Jump if TOS true | [section 8](opcodes/kos-op-jump-true.md) |
| JUMP_FALSE_VAR | EXTRA | Jump if the named value is false | [section 8](opcodes/kos-op-jump-false-var.md) |
| JUMP_TRUE_VAR | EXTRA | Jump if the named value is true | [section 8](opcodes/kos-op-jump-true-var.md) |
| JUMP_FALSE_FAST | EXTRA | Jump if the fast slot is false | [section 8](opcodes/kos-op-jump-false-fast.md) |
| JUMP_TRUE_FAST | EXTRA | Jump if the fast slot is true | [section 8](opcodes/kos-op-jump-true-fast.md) |
| NILP | NOARG | Pop TOS; push True when it is an empty-list singleton | [section 8](opcodes/kos-op-nilp.md) |
| CHECK_NIL | LABEL | Pop TOS; jump when it is not an empty-list singleton | [section 8](opcodes/kos-op-check-nil.md) |
| JUMP_AND | LABEL | Short-circuit AND | [section 8](opcodes/kos-op-jump-and.md) |
| JUMP_OR | LABEL | Short-circuit OR | [section 8](opcodes/kos-op-jump-or.md) |
| JUMP_AND_VAR | EXTRA | Short-circuit AND over the named value | [section 8](opcodes/kos-op-jump-and-var.md) |
| JUMP_OR_VAR | EXTRA | Short-circuit OR over the named value | [section 8](opcodes/kos-op-jump-or-var.md) |
| JUMP_AND_FAST | EXTRA | Short-circuit AND over the fast slot | [section 8](opcodes/kos-op-jump-and-fast.md) |
| JUMP_OR_FAST | EXTRA | Short-circuit OR over the fast slot | [section 8](opcodes/kos-op-jump-or-fast.md) |
| CHECK_COMPARE | EXTRA | Chain-compare N operands under a KosCompare selector; jump if chain fails | [section 9](opcodes/kos-op-check-compare.md) |
| CHECK_COMPARE_VAR | EXTRA | Arity-2 fused check; operands from VAR/CONST per CMPSRC bits | [section 9](opcodes/kos-op-check-compare-var.md) |
| CHECK_COMPARE_FAST | EXTRA | Arity-2 fused check; operands from FAST/CONST per CMPSRC bits | [section 9](opcodes/kos-op-check-compare-fast.md) |
| CHECK_EXCEPTION | LABEL | Jump if exception no match | [section 9](opcodes/kos-op-check-exception.md) |
| SWITCH_EQ | LABEL | Identity-or-equality dispatch; on match pop both and jump; on mismatch pop key only, fall through | [section 9.1](opcodes/kos-op-switch-eq.md) |
| SWITCH_EQ_CONST | EXTRA | Compare TOS to constant RHS; on match pop TOS and jump; on mismatch fall through | [section 9.1](opcodes/kos-op-switch-eq-const.md) |
| SWITCH_EQ_VAR | EXTRA | Compare TOS to namespace var RHS; on match pop TOS and jump; on mismatch fall through | [section 9.1](opcodes/kos-op-switch-eq-var.md) |
| SWITCH_EQ_FAST | EXTRA | Compare TOS to fast-slot RHS; on match pop TOS and jump; on mismatch fall through | [section 9.1](opcodes/kos-op-switch-eq-fast.md) |
| SWITCH_EQ_VAR_ATTR | EXTRA | Compare TOS to getattr(var, attr); on match pop TOS and jump; on mismatch fall through | [section 9.1](opcodes/kos-op-switch-eq-var-attr.md) |
| SWITCH_EQ_FAST_ATTR | EXTRA | Compare TOS to getattr(fast, attr); on match pop TOS and jump; on mismatch fall through | [section 9.1](opcodes/kos-op-switch-eq-fast-attr.md) |
| MATCH_DESCEND | LABEL | Sequence gate on TOS; a cursor over it replaces it; a miss pops it | [section 9.2](opcodes/kos-op-match-descend.md) |
| MATCH_SINGLE | NOARG | A one-element cursor over TOS replaces it | [section 9.2](opcodes/kos-op-match-single.md) |
| MATCH_FITS | EXTRA | Miss unless the cursor at the depth has between the minimum and the maximum elements left | [section 9.2](opcodes/kos-op-match-fits.md) |
| MATCH_NEXT | EXTRA | Pull the next element from the cursor at the depth, boxed from a pair spine; a miss at the end | [section 9.2](opcodes/kos-op-match-next.md) |
| MATCH_BOUND | EXTRA | Push a cursor at the cursor's position ending the width before its end | [section 9.2](opcodes/kos-op-match-bound.md) |
| MATCH_UNBOUND | EXTRA | Push a cursor at the cursor's position ending at its subject's end | [section 9.2](opcodes/kos-op-match-unbound.md) |
| MATCH_SYNC | EXTRA | Move the cursor at the first depth to the position of the cursor at the second | [section 9.2](opcodes/kos-op-match-sync.md) |
| MATCH_TAIL | EXTRA | Push the remaining elements but the suffix width as a span, advancing the cursor | [section 9.2](opcodes/kos-op-match-tail.md) |
| MATCH_SCOPE | EXTRA | Push a mark at the cursor's position with the count of empty slots | [section 9.2](opcodes/kos-op-match-scope.md) |
| MATCH_SCOPE_NEXT | EXTRA | Push a mark at the cursor's position, then the cursor's next element; a miss leaves the mark | [section 9.2](opcodes/kos-op-match-scope-next.md) |
| MATCH_RESET | EXTRA | Move the cursor at the first depth back to the position of the mark at the second | [section 9.2](opcodes/kos-op-match-reset.md) |
| MATCH_AT | EXTRA | Push the element at the mark's position | [section 9.2](opcodes/kos-op-match-at.md) |
| MATCH_SPAN | EXTRA | Push the span from the mark to the cursor; jump when it is empty | [section 9.2](opcodes/kos-op-match-span.md) |
| MATCH_STORE | EXTRA | Pop TOS into a slot of the mark at the depth | [section 9.2](opcodes/kos-op-match-store.md) |
| MATCH_CLEAR | EXTRA | Empty the listed slots of the mark at the depth | [section 9.2](opcodes/kos-op-match-clear.md) |
| MATCH_CAPTURES | EXTRA | Push the mark's filled slots: a DatumFrame over a pair spine, a dict otherwise | [section 9.2](opcodes/kos-op-match-captures.md) |
| MATCH_PASS | EXTRA | Push a repeat pass's value: its captures, or its span when no slot is filled | [section 9.2](opcodes/kos-op-match-pass.md) |
| MATCH_LOAD | EXTRA | Push a slot of the mark at the depth; jump when it is empty | [section 9.2](opcodes/kos-op-match-load.md) |
| MATCH_APPEND | EXTRA | Pop TOS and append it to the list at the depth | [section 9.2](opcodes/kos-op-match-append.md) |
| MATCH_MIN | EXTRA | Miss unless the list at the depth holds at least the count of items | [section 9.2](opcodes/kos-op-match-min.md) |
| MATCH_COLUMNS | EXTRA | Store each name's per-pass values, as a list, into a slot of the mark | [section 9.2](opcodes/kos-op-match-columns.md) |
| MATCH_LITERAL | EXTRA | TOS, which stays, must be the constant | [section 9.2](opcodes/kos-op-match-literal.md) |
| MATCH_CORE | EXTRA | TOS, which stays, must be an instance of the core type the ordinal names | [section 9.2](opcodes/kos-op-match-core.md) |
| MATCH_TYPE_VAR / MATCH_TYPE_FAST | EXTRA | TOS, which stays, must be an instance of the type in the variable | [section 9.2](opcodes/kos-op-match-type.md) |
| MATCH_CLASS_VAR / MATCH_CLASS_FAST | EXTRA | TOS, which stays, must be an instance of the class in the variable, whose `__match_args__` must cover the positional fields | [section 9.2](opcodes/kos-op-match-class.md) |
| MATCH_SAME | EXTRA | TOS, which stays, must equal a slot of the mark at the depth; an empty slot misses | [section 9.2](opcodes/kos-op-match-same.md) |
| MATCH_FIELD_POS_VAR / MATCH_FIELD_POS_FAST | EXTRA | Push the attribute the class's `__match_args__` names at the position | [section 9.2](opcodes/kos-op-match-field-pos.md) |
| MATCH_FIELD_KW | EXTRA | Push the attribute the constant names | [section 9.2](opcodes/kos-op-match-field-kw.md) |
| MATCH_MAPPING | LABEL | The mapping gate on TOS | [section 9.2](opcodes/kos-op-match-mapping.md) |
| MATCH_KEY | EXTRA | Push the value under the constant key; an absent key misses | [section 9.2](opcodes/kos-op-match-key.md) |
| MATCH_KEY_VAR / MATCH_KEY_FAST | EXTRA | Push the value under the key in the variable; an absent key misses | [section 9.2](opcodes/kos-op-match-key-var.md) |
| GLOBALS | NOARG | Push globals dict | [section 10](opcodes/kos-op-globals.md) |
| LOCALS | NOARG | Push locals dict | [section 10](opcodes/kos-op-locals.md) |
| IMPORT | EXTRA | Import module; bind root or leaf (self-contained) | [section 10.1](opcodes/kos-op-import.md) |
| IMPORT_FROM | EXTRA | Import module; bind named attrs (self-contained) | [section 10.1](opcodes/kos-op-import-from.md) |
| IMPORT_STAR | EXTRA | Import module; bind its public names | [section 10.1](opcodes/kos-op-import-star.md) |
| IMPORT_USE | EXTRA | Import the leaf module; probe each feature stem's candidates and bind every hit under the parallel stem | [section 10.1](opcodes/kos-op-import-use.md) |
| EXCEPTION | NOARG | Push current exception | [section 11](opcodes/kos-op-exception.md) |
| EXCEPTION_CLEAR | NOARG | Clear exception state | [section 11](opcodes/kos-op-exception-clear.md) |
| EXCEPTION_MATCH | NOARG | Pop type(s), push match result | [section 11](opcodes/kos-op-exception-match.md) |
| VAR | VAR | Load by name | [section 12](opcodes/kos-op-var.md) |
| VAR2 | EXTRA | Load two names, push in row order | [section 12](opcodes/kos-op-var2.md) |
| VAR3 | EXTRA | Load three names, push in row order | [section 12](opcodes/kos-op-var3.md) |
| VAR4 | EXTRA | Load four names, push in row order | [section 12](opcodes/kos-op-var4.md) |
| VAR_BIND | VAR | BIND: use TOS ref to resolve name | [section 12](opcodes/kos-op-var-bind.md) |
| VAR_CLEAR | VAR | Delete the name from the namespace; no stack effect | [section 12](opcodes/kos-op-var-clear.md) |
| VAR_CLEAR2 | EXTRA | Clear 2 names in row order; no stack effect | [section 12](opcodes/kos-op-var-clear2.md) |
| VAR_CLEAR3 | EXTRA | Clear 3 names in row order; no stack effect | [section 12](opcodes/kos-op-var-clear3.md) |
| VAR_CLEAR4 | EXTRA | Clear 4 names in row order; no stack effect | [section 12](opcodes/kos-op-var-clear4.md) |
| VAR_TAKE | VAR | Deref the name, clear the binding, push the value; NameError when the binding is empty | [section 12](opcodes/kos-op-var-take.md) |
| VAR_REF | VAR | Push a reference to the named variable | [section 12](opcodes/kos-op-var-ref.md) |
| VAR_STORE | VAR | STORE: write TOS value into name | [section 12](opcodes/kos-op-var-store.md) |
| VAR_STORE_CONST | EXTRA | STORE: write constant into name (no stack) | [section 12](opcodes/kos-op-var-store-const.md) |
| VAR_INPLACE | EXTRA | Augmented assignment through the namespace: deref the name, reduce with the popped rhs, store back | [section 12](opcodes/kos-op-var-inplace.md) |
| VAR_INPLACE_CONST | EXTRA | Augmented assignment through the namespace with a constant rhs; no stack effect | [section 12](opcodes/kos-op-var-inplace-const.md) |
| VAR_ATTR | EXTRA | Get attribute; object loaded from the namespace by name | [section 12.1](opcodes/kos-op-var-attr.md) |
| VAR_ATTR_STORE | EXTRA | Set attribute; object from the namespace | [section 12.1](opcodes/kos-op-var-attr-store.md) |
| VAR_ATTR_CLEAR | EXTRA | Delete attribute; object from the namespace | [section 12.1](opcodes/kos-op-var-attr-clear.md) |
| VAR_ATTR_TAKE | EXTRA | Get attribute, delete it, push the value; object from the namespace | [section 12.1](opcodes/kos-op-var-attr-take.md) |
| VAR_ATTR_REF | EXTRA | Push attribute reference; object from the namespace | [section 12.1](opcodes/kos-op-var-attr-ref.md) |
| VAR_ATTR_INPLACE | EXTRA | Reduce obj.attr in place with the count values and store it back; object from the namespace | [section 12.1](opcodes/kos-op-var-attr-inplace.md) |
| VAR_ITER | VAR | Fused VAR + OBJECT_ITER: push the named iterable's iterator | [section 12](opcodes/kos-op-var-iter.md) |
| VAR_NEXT | EXTRA | Fused VAR + OBJECT_NEXT: step the named iterator; count 2 pops a default for exhaustion | [section 12](opcodes/kos-op-var-next.md) |
| VAR_FORMAT | VAR | Fused VAR + OBJECT_FORMAT: push `format(obj)` for the named object | [section 12](opcodes/kos-op-var-format.md) |
| VAR_FORMAT_CONST | EXTRA | Fused VAR + OBJECT_FORMAT_CONST: format the named object with pooled rules | [section 12](opcodes/kos-op-var-format-const.md) |
| RETURN_VAR | VAR | Return the name's value; the `VAR` + `RETURN 1` fusion | [section 12](opcodes/kos-op-return-var.md) |
| YIELD_VAR | VAR | Yield the name's value; the `VAR` + `YIELD 1` fusion | [section 12](opcodes/kos-op-yield-var.md) |
| FAST | FASTVAR | Load from slot | [section 13](opcodes/kos-op-fast.md) |
| FAST2 | EXTRA | Load two slots, push in row order | [section 13](opcodes/kos-op-fast2.md) |
| FAST3 | EXTRA | Load three slots, push in row order | [section 13](opcodes/kos-op-fast3.md) |
| FAST4 | EXTRA | Load four slots, push in row order | [section 13](opcodes/kos-op-fast4.md) |
| FAST_BIND | FASTVAR | BIND: use TOS ref to resolve slot | [section 13](opcodes/kos-op-fast-bind.md) |
| FAST_CLEAR | FASTVAR | Clear the slot; no stack effect | [section 13](opcodes/kos-op-fast-clear.md) |
| FAST_CLEAR2 | EXTRA | Clear 2 slots in row order; no stack effect | [section 13](opcodes/kos-op-fast-clear2.md) |
| FAST_CLEAR3 | EXTRA | Clear 3 slots in row order; no stack effect | [section 13](opcodes/kos-op-fast-clear3.md) |
| FAST_CLEAR4 | EXTRA | Clear 4 slots in row order; no stack effect | [section 13](opcodes/kos-op-fast-clear4.md) |
| FAST_TAKE | FASTVAR | Take the slot's value and clear it; NameError when the slot is empty | [section 13](opcodes/kos-op-fast-take.md) |
| FAST_REF | FASTVAR | Push a reference to the slot | [section 13](opcodes/kos-op-fast-ref.md) |
| FAST_STORE | FASTVAR | STORE: write TOS value into slot | [section 13](opcodes/kos-op-fast-store.md) |
| FAST_STORE_CONST | EXTRA | STORE: write constant into slot (no stack) | [section 13](opcodes/kos-op-fast-store-const.md) |
| FAST_INPLACE | EXTRA | Augmented assignment on a slot: deref, reduce with the popped rhs, store back | [section 13](opcodes/kos-op-fast-inplace.md) |
| FAST_INPLACE_CONST | EXTRA | Augmented assignment on a slot with a constant rhs; no stack effect | [section 13](opcodes/kos-op-fast-inplace-const.md) |
| FAST_ATTR | EXTRA | Get attribute; object from a fast slot | [section 13.1](opcodes/kos-op-fast-attr.md) |
| FAST_ATTR_STORE | EXTRA | Set attribute; object from a fast slot | [section 13.1](opcodes/kos-op-fast-attr-store.md) |
| FAST_ATTR_CLEAR | EXTRA | Delete attribute; object from a fast slot | [section 13.1](opcodes/kos-op-fast-attr-clear.md) |
| FAST_ATTR_TAKE | EXTRA | Get attribute, delete it, push the value; object from a fast slot | [section 13.1](opcodes/kos-op-fast-attr-take.md) |
| FAST_ATTR_REF | EXTRA | Push attribute reference; object from a fast slot | [section 13.1](opcodes/kos-op-fast-attr-ref.md) |
| FAST_ATTR_INPLACE | EXTRA | Reduce obj.attr in place with the count values and store it back; object from a fast slot | [section 13.1](opcodes/kos-op-fast-attr-inplace.md) |
| FAST_ITER | FASTVAR | As VAR_ITER, with the iterable loaded from its fast slot | [section 13](opcodes/kos-op-fast-iter.md) |
| FAST_NEXT | EXTRA | As VAR_NEXT, with the iterator loaded from its fast slot | [section 13](opcodes/kos-op-fast-next.md) |
| FAST_FORMAT | FASTVAR | As VAR_FORMAT, with the object loaded from its fast slot | [section 13](opcodes/kos-op-fast-format.md) |
| FAST_FORMAT_CONST | EXTRA | As VAR_FORMAT_CONST, with the object loaded from its fast slot | [section 13](opcodes/kos-op-fast-format-const.md) |
| RETURN_FAST | FASTVAR | Return the slot's value; lowering of RETURN_VAR | [section 13](opcodes/kos-op-return-fast.md) |
| YIELD_FAST | FASTVAR | Yield the slot's value; lowering of YIELD_VAR | [section 13](opcodes/kos-op-yield-fast.md) |
| ATTR | CONST | Get attribute | [section 14](opcodes/kos-op-attr.md) |
| ATTR_STORE | CONST | Set attribute | [section 14](opcodes/kos-op-attr-store.md) |
| ATTR_CLEAR | CONST | Delete attribute | [section 14](opcodes/kos-op-attr-clear.md) |
| ATTR_TAKE | CONST | Get attribute, delete it, push the value | [section 14](opcodes/kos-op-attr-take.md) |
| ATTR_REF | CONST | Push attribute reference | [section 14](opcodes/kos-op-attr-ref.md) |
| ATTR_INPLACE | EXTRA | Reduce obj.attr in place with the count values and store it back; pushes nothing | [section 14](opcodes/kos-op-attr-inplace.md) |
| ITEM | NOARG | Get item; object on the stack, key on the stack | [section 15](opcodes/kos-op-item.md) |
| ITEM_CLEAR | NOARG | Delete item; object on the stack, key on the stack | [section 15](opcodes/kos-op-item-clear.md) |
| ITEM_TAKE | NOARG | Take item; object on the stack, key on the stack | [section 15](opcodes/kos-op-item-take.md) |
| ITEM_REF | NOARG | Item reference; object on the stack, key on the stack | [section 15](opcodes/kos-op-item-ref.md) |
| ITEM_STORE | NOARG | Set item; object on the stack, key on the stack | [section 15](opcodes/kos-op-item-store.md) |
| ITEM_STORE_CONST | CONST | Set item to a constant; object on the stack, key on the stack | [section 15](opcodes/kos-op-item-store-const.md) |
| ITEM_STORE_VAR | VAR | Set item; object on the stack, key on the stack, value by name | [section 12](opcodes/kos-op-item-store-var.md) |
| ITEM_STORE_FAST | FASTVAR | Set item; object on the stack, key on the stack, value from a fast slot | [section 13](opcodes/kos-op-item-store-fast.md) |
| ITEM_INPLACE | OPCOUNT | Reduce item in place; object on the stack, key on the stack | [section 15](opcodes/kos-op-item-inplace.md) |
| ITEM_INPLACE_CONST | EXTRA | Reduce item in place by a constant; object on the stack, key on the stack | [section 15](opcodes/kos-op-item-inplace-const.md) |
| ITEM_CONST | CONST | Get item; object on the stack, constant key | [section 15](opcodes/kos-op-item-const.md) |
| ITEM_CONST_CLEAR | CONST | Delete item; object on the stack, constant key | [section 15](opcodes/kos-op-item-const-clear.md) |
| ITEM_CONST_TAKE | CONST | Take item; object on the stack, constant key | [section 15](opcodes/kos-op-item-const-take.md) |
| ITEM_CONST_REF | CONST | Item reference; object on the stack, constant key | [section 15](opcodes/kos-op-item-const-ref.md) |
| ITEM_CONST_STORE | CONST | Set item; object on the stack, constant key | [section 15](opcodes/kos-op-item-const-store.md) |
| ITEM_CONST_STORE_CONST | EXTRA | Set item to a constant; object on the stack, constant key | [section 15](opcodes/kos-op-item-const-store-const.md) |
| ITEM_CONST_STORE_VAR | EXTRA | Set item; object on the stack, constant key, value by name | [section 12](opcodes/kos-op-item-const-store-var.md) |
| ITEM_CONST_STORE_FAST | EXTRA | Set item; object on the stack, constant key, value from a fast slot | [section 13](opcodes/kos-op-item-const-store-fast.md) |
| ITEM_CONST_INPLACE | EXTRA | Reduce item in place; object on the stack, constant key | [section 15](opcodes/kos-op-item-const-inplace.md) |
| ITEM_CONST_INPLACE_CONST | EXTRA | Reduce item in place by a constant; object on the stack, constant key | [section 15](opcodes/kos-op-item-const-inplace-const.md) |
| ITEM_VAR | VAR | Get item; object on the stack, key by name | [section 12](opcodes/kos-op-item-var.md) |
| ITEM_VAR_CLEAR | VAR | Delete item; object on the stack, key by name | [section 12](opcodes/kos-op-item-var-clear.md) |
| ITEM_VAR_TAKE | VAR | Take item; object on the stack, key by name | [section 12](opcodes/kos-op-item-var-take.md) |
| ITEM_VAR_REF | VAR | Item reference; object on the stack, key by name | [section 12](opcodes/kos-op-item-var-ref.md) |
| ITEM_VAR_STORE | VAR | Set item; object on the stack, key by name | [section 12](opcodes/kos-op-item-var-store.md) |
| ITEM_VAR_STORE_CONST | EXTRA | Set item to a constant; object on the stack, key by name | [section 12](opcodes/kos-op-item-var-store-const.md) |
| ITEM_VAR_STORE_VAR | EXTRA | Set item; object on the stack, key by name, value by name | [section 12](opcodes/kos-op-item-var-store-var.md) |
| ITEM_VAR_INPLACE | EXTRA | Reduce item in place; object on the stack, key by name | [section 12](opcodes/kos-op-item-var-inplace.md) |
| ITEM_VAR_INPLACE_CONST | EXTRA | Reduce item in place by a constant; object on the stack, key by name | [section 12](opcodes/kos-op-item-var-inplace-const.md) |
| ITEM_FAST | FASTVAR | Get item; object on the stack, key from a fast slot | [section 13](opcodes/kos-op-item-fast.md) |
| ITEM_FAST_CLEAR | FASTVAR | Delete item; object on the stack, key from a fast slot | [section 13](opcodes/kos-op-item-fast-clear.md) |
| ITEM_FAST_TAKE | FASTVAR | Take item; object on the stack, key from a fast slot | [section 13](opcodes/kos-op-item-fast-take.md) |
| ITEM_FAST_REF | FASTVAR | Item reference; object on the stack, key from a fast slot | [section 13](opcodes/kos-op-item-fast-ref.md) |
| ITEM_FAST_STORE | FASTVAR | Set item; object on the stack, key from a fast slot | [section 13](opcodes/kos-op-item-fast-store.md) |
| ITEM_FAST_STORE_CONST | EXTRA | Set item to a constant; object on the stack, key from a fast slot | [section 13](opcodes/kos-op-item-fast-store-const.md) |
| ITEM_FAST_STORE_FAST | EXTRA | Set item; object on the stack, key from a fast slot, value from a fast slot | [section 13](opcodes/kos-op-item-fast-store-fast.md) |
| ITEM_FAST_INPLACE | EXTRA | Reduce item in place; object on the stack, key from a fast slot | [section 13](opcodes/kos-op-item-fast-inplace.md) |
| ITEM_FAST_INPLACE_CONST | EXTRA | Reduce item in place by a constant; object on the stack, key from a fast slot | [section 13](opcodes/kos-op-item-fast-inplace-const.md) |
| VAR_ITEM | VAR | Get item; object by name, key on the stack | [section 12](opcodes/kos-op-var-item.md) |
| VAR_ITEM_CLEAR | VAR | Delete item; object by name, key on the stack | [section 12](opcodes/kos-op-var-item-clear.md) |
| VAR_ITEM_TAKE | VAR | Take item; object by name, key on the stack | [section 12](opcodes/kos-op-var-item-take.md) |
| VAR_ITEM_REF | VAR | Item reference; object by name, key on the stack | [section 12](opcodes/kos-op-var-item-ref.md) |
| VAR_ITEM_STORE | VAR | Set item; object by name, key on the stack | [section 12](opcodes/kos-op-var-item-store.md) |
| VAR_ITEM_STORE_CONST | EXTRA | Set item to a constant; object by name, key on the stack | [section 12](opcodes/kos-op-var-item-store-const.md) |
| VAR_ITEM_STORE_VAR | EXTRA | Set item; object by name, key on the stack, value by name | [section 12](opcodes/kos-op-var-item-store-var.md) |
| VAR_ITEM_INPLACE | EXTRA | Reduce item in place; object by name, key on the stack | [section 12](opcodes/kos-op-var-item-inplace.md) |
| VAR_ITEM_INPLACE_CONST | EXTRA | Reduce item in place by a constant; object by name, key on the stack | [section 12](opcodes/kos-op-var-item-inplace-const.md) |
| VAR_ITEM_CONST | EXTRA | Get item; object by name, constant key | [section 12](opcodes/kos-op-var-item-const.md) |
| VAR_ITEM_CONST_CLEAR | EXTRA | Delete item; object by name, constant key | [section 12](opcodes/kos-op-var-item-const-clear.md) |
| VAR_ITEM_CONST_TAKE | EXTRA | Take item; object by name, constant key | [section 12](opcodes/kos-op-var-item-const-take.md) |
| VAR_ITEM_CONST_REF | EXTRA | Item reference; object by name, constant key | [section 12](opcodes/kos-op-var-item-const-ref.md) |
| VAR_ITEM_CONST_STORE | EXTRA | Set item; object by name, constant key | [section 12](opcodes/kos-op-var-item-const-store.md) |
| VAR_ITEM_CONST_STORE_CONST | EXTRA | Set item to a constant; object by name, constant key | [section 12](opcodes/kos-op-var-item-const-store-const.md) |
| VAR_ITEM_CONST_STORE_VAR | EXTRA | Set item; object by name, constant key, value by name | [section 12](opcodes/kos-op-var-item-const-store-var.md) |
| VAR_ITEM_CONST_INPLACE | EXTRA | Reduce item in place; object by name, constant key | [section 12](opcodes/kos-op-var-item-const-inplace.md) |
| VAR_ITEM_CONST_INPLACE_CONST | EXTRA | Reduce item in place by a constant; object by name, constant key | [section 12](opcodes/kos-op-var-item-const-inplace-const.md) |
| VAR_ITEM_VAR | EXTRA | Get item; object by name, key by name | [section 12](opcodes/kos-op-var-item-var.md) |
| VAR_ITEM_VAR_CLEAR | EXTRA | Delete item; object by name, key by name | [section 12](opcodes/kos-op-var-item-var-clear.md) |
| VAR_ITEM_VAR_TAKE | EXTRA | Take item; object by name, key by name | [section 12](opcodes/kos-op-var-item-var-take.md) |
| VAR_ITEM_VAR_REF | EXTRA | Item reference; object by name, key by name | [section 12](opcodes/kos-op-var-item-var-ref.md) |
| VAR_ITEM_VAR_STORE | EXTRA | Set item; object by name, key by name | [section 12](opcodes/kos-op-var-item-var-store.md) |
| VAR_ITEM_VAR_STORE_CONST | EXTRA | Set item to a constant; object by name, key by name | [section 12](opcodes/kos-op-var-item-var-store-const.md) |
| VAR_ITEM_VAR_STORE_VAR | EXTRA | Set item; object by name, key by name, value by name | [section 12](opcodes/kos-op-var-item-var-store-var.md) |
| VAR_ITEM_VAR_INPLACE | EXTRA | Reduce item in place; object by name, key by name | [section 12](opcodes/kos-op-var-item-var-inplace.md) |
| VAR_ITEM_VAR_INPLACE_CONST | EXTRA | Reduce item in place by a constant; object by name, key by name | [section 12](opcodes/kos-op-var-item-var-inplace-const.md) |
| FAST_ITEM | FASTVAR | Get item; object from a fast slot, key on the stack | [section 13](opcodes/kos-op-fast-item.md) |
| FAST_ITEM_CLEAR | FASTVAR | Delete item; object from a fast slot, key on the stack | [section 13](opcodes/kos-op-fast-item-clear.md) |
| FAST_ITEM_TAKE | FASTVAR | Take item; object from a fast slot, key on the stack | [section 13](opcodes/kos-op-fast-item-take.md) |
| FAST_ITEM_REF | FASTVAR | Item reference; object from a fast slot, key on the stack | [section 13](opcodes/kos-op-fast-item-ref.md) |
| FAST_ITEM_STORE | FASTVAR | Set item; object from a fast slot, key on the stack | [section 13](opcodes/kos-op-fast-item-store.md) |
| FAST_ITEM_STORE_CONST | EXTRA | Set item to a constant; object from a fast slot, key on the stack | [section 13](opcodes/kos-op-fast-item-store-const.md) |
| FAST_ITEM_STORE_FAST | EXTRA | Set item; object from a fast slot, key on the stack, value from a fast slot | [section 13](opcodes/kos-op-fast-item-store-fast.md) |
| FAST_ITEM_INPLACE | EXTRA | Reduce item in place; object from a fast slot, key on the stack | [section 13](opcodes/kos-op-fast-item-inplace.md) |
| FAST_ITEM_INPLACE_CONST | EXTRA | Reduce item in place by a constant; object from a fast slot, key on the stack | [section 13](opcodes/kos-op-fast-item-inplace-const.md) |
| FAST_ITEM_CONST | EXTRA | Get item; object from a fast slot, constant key | [section 13](opcodes/kos-op-fast-item-const.md) |
| FAST_ITEM_CONST_CLEAR | EXTRA | Delete item; object from a fast slot, constant key | [section 13](opcodes/kos-op-fast-item-const-clear.md) |
| FAST_ITEM_CONST_TAKE | EXTRA | Take item; object from a fast slot, constant key | [section 13](opcodes/kos-op-fast-item-const-take.md) |
| FAST_ITEM_CONST_REF | EXTRA | Item reference; object from a fast slot, constant key | [section 13](opcodes/kos-op-fast-item-const-ref.md) |
| FAST_ITEM_CONST_STORE | EXTRA | Set item; object from a fast slot, constant key | [section 13](opcodes/kos-op-fast-item-const-store.md) |
| FAST_ITEM_CONST_STORE_CONST | EXTRA | Set item to a constant; object from a fast slot, constant key | [section 13](opcodes/kos-op-fast-item-const-store-const.md) |
| FAST_ITEM_CONST_STORE_FAST | EXTRA | Set item; object from a fast slot, constant key, value from a fast slot | [section 13](opcodes/kos-op-fast-item-const-store-fast.md) |
| FAST_ITEM_CONST_INPLACE | EXTRA | Reduce item in place; object from a fast slot, constant key | [section 13](opcodes/kos-op-fast-item-const-inplace.md) |
| FAST_ITEM_CONST_INPLACE_CONST | EXTRA | Reduce item in place by a constant; object from a fast slot, constant key | [section 13](opcodes/kos-op-fast-item-const-inplace-const.md) |
| FAST_ITEM_FAST | EXTRA | Get item; object from a fast slot, key from a fast slot | [section 13](opcodes/kos-op-fast-item-fast.md) |
| FAST_ITEM_FAST_CLEAR | EXTRA | Delete item; object from a fast slot, key from a fast slot | [section 13](opcodes/kos-op-fast-item-fast-clear.md) |
| FAST_ITEM_FAST_TAKE | EXTRA | Take item; object from a fast slot, key from a fast slot | [section 13](opcodes/kos-op-fast-item-fast-take.md) |
| FAST_ITEM_FAST_REF | EXTRA | Item reference; object from a fast slot, key from a fast slot | [section 13](opcodes/kos-op-fast-item-fast-ref.md) |
| FAST_ITEM_FAST_STORE | EXTRA | Set item; object from a fast slot, key from a fast slot | [section 13](opcodes/kos-op-fast-item-fast-store.md) |
| FAST_ITEM_FAST_STORE_CONST | EXTRA | Set item to a constant; object from a fast slot, key from a fast slot | [section 13](opcodes/kos-op-fast-item-fast-store-const.md) |
| FAST_ITEM_FAST_STORE_FAST | EXTRA | Set item; object from a fast slot, key from a fast slot, value from a fast slot | [section 13](opcodes/kos-op-fast-item-fast-store-fast.md) |
| FAST_ITEM_FAST_INPLACE | EXTRA | Reduce item in place; object from a fast slot, key from a fast slot | [section 13](opcodes/kos-op-fast-item-fast-inplace.md) |
| FAST_ITEM_FAST_INPLACE_CONST | EXTRA | Reduce item in place by a constant; object from a fast slot, key from a fast slot | [section 13](opcodes/kos-op-fast-item-fast-inplace-const.md) |
| DEREF | NOARG | Pop Ref, call ref.deref(), push result | [section 15.1](opcodes/kos-op-deref.md) |
| BUILD_ARGS | COUNT | Pop N elements and push a tuple of them | [section 16](opcodes/kos-op-build-args.md) |
| BUILD_FUNCTION | EXTRA | Build function from spec, with keyword defaults and annotations from names tuples | [section 16](opcodes/kos-op-build-function.md) |
| BUILD_KWDS | PAIR_COUNT | Pop 2N elements as key/value pairs and push a dict | [section 16](opcodes/kos-op-build-kwds.md) |
| BUILD_KWDS_NAMES | CONST | Pop N values, pair them with the pooled names tuple, push a dict | [section 16](opcodes/kos-op-build-kwds-names.md) |
| BUILD_LIST | COUNT | Pop N elements into a new list and push it | [section 16](opcodes/kos-op-build-list.md) |
| BUILD_LIST_CONST | CONST | Push a fresh list built from the pooled sequence | [section 16](opcodes/kos-op-build-list-const.md) |
| VAR_LIST_APPEND | VAR | Append TOS to the list bound to the named variable, in place | [section 16](opcodes/kos-op-var-list-append.md) |
| FAST_LIST_APPEND | FASTVAR | Fast-slot form of VAR_LIST_APPEND | [section 16](opcodes/kos-op-fast-list-append.md) |
| BUILD_LIST_APPEND | COUNT | Append the top N values to the list beneath them, in place; leave the list | [section 16](opcodes/kos-op-build-list-append.md) |
| BUILD_LIST_APPEND_CONST | CONST | Append the pooled value to the TOS list, in place | [section 16](opcodes/kos-op-build-list-append-const.md) |
| BUILD_LIST_EXTEND | NOARG | Extend the list in place from the TOS iterable; leave the list | [section 16](opcodes/kos-op-build-list-extend.md) |
| BUILD_LIST_EXTEND_CONST | CONST | Extend the TOS list in place from the pooled sequence | [section 16](opcodes/kos-op-build-list-extend-const.md) |
| COMBINE_ARGS | COUNT | Concatenate the M sequences into a single tuple | [section 16](opcodes/kos-op-combine-args.md) |
| COMBINE_KWDS | COUNT | Merge the M dicts, later entries winning on a key clash | [section 16](opcodes/kos-op-combine-kwds.md) |
| CALL | COUNT | Call with N positional args | [section 17](opcodes/kos-op-call.md) |
| CALL_NAMES | COUNT | Call with keyword names on stack | [section 17](opcodes/kos-op-call-names.md) |
| CALL_STAR | STAR | Call with *args and/or **kwargs | [section 17](opcodes/kos-op-call-star.md) |
| CALL_VAR | EXTRA | Call callable from namespace by name | [section 17](opcodes/kos-op-call-var.md) |
| CALL_FAST | EXTRA | Call callable from fast local slot | [section 17](opcodes/kos-op-call-fast.md) |
| CALL_VAR_NAMES | EXTRA | Call by name with keyword args | [section 17](opcodes/kos-op-call-var-names.md) |
| CALL_FAST_NAMES | EXTRA | Call from fast slot with keyword args | [section 17](opcodes/kos-op-call-fast-names.md) |
| CALL_VAR_STAR | EXTRA | Call by name with *args/**kwargs | [section 17](opcodes/kos-op-call-var-star.md) |
| CALL_FAST_STAR | EXTRA | Call from fast slot with *args/**kwargs | [section 17](opcodes/kos-op-call-fast-star.md) |
| CALL_ATTR | EXTRA | Call obj.name with N positional args | [section 17](opcodes/kos-op-call-attr.md) |
| CALL_ATTR_NAMES | EXTRA | Call obj.name with positional + keyword args | [section 17](opcodes/kos-op-call-attr-names.md) |
| CALL_FAST_ATTR | EXTRA | Call obj.name with N positional args; obj from fast slot | [section 17](opcodes/kos-op-call-fast-attr.md) |
| CALL_VAR_ATTR | EXTRA | Call obj.name with N positional args; obj from VAR | [section 17](opcodes/kos-op-call-var-attr.md) |
| CALL_FAST_ATTR_NAMES | EXTRA | Call obj.name with positional + keyword args; obj from fast slot | [section 17](opcodes/kos-op-call-fast-attr-names.md) |
| CALL_VAR_ATTR_NAMES | EXTRA | Call obj.name with positional + keyword args; obj from VAR | [section 17](opcodes/kos-op-call-var-attr-names.md) |
| CALL_ATTR_STAR | EXTRA | Call obj.name with *args/**kwargs | [section 17](opcodes/kos-op-call-attr-star.md) |
| CALL_VAR_ATTR_STAR | EXTRA | Call obj.name with *args/**kwargs; obj from VAR | [section 17](opcodes/kos-op-call-var-attr-star.md) |
| CALL_FAST_ATTR_STAR | EXTRA | Call obj.name with *args/**kwargs; obj from fast slot | [section 17](opcodes/kos-op-call-fast-attr-star.md) |
| TAIL_CALL | COUNT | Tail call (replace frame) | [section 17](opcodes/kos-op-tail-call.md) |
| TAIL_CALL_NAMES | COUNT | Tail call with keyword names | [section 17](opcodes/kos-op-tail-call-names.md) |
| TAIL_CALL_STAR | STAR | Tail call with *args and/or **kwargs | [section 17](opcodes/kos-op-tail-call-star.md) |
| TAIL_CALL_ATTR | EXTRA | Tail call obj.name with N positional args | [section 17](opcodes/kos-op-tail-call-attr.md) |
| TAIL_CALL_ATTR_NAMES | EXTRA | Tail call obj.name with positional + keyword args | [section 17](opcodes/kos-op-tail-call-attr-names.md) |
| TAIL_CALL_ATTR_STAR | EXTRA | Tail call obj.name with *args/**kwargs | [section 17](opcodes/kos-op-tail-call-attr-star.md) |
| TAIL_CALL_VAR | EXTRA | Tail call callable from namespace by name | [section 17](opcodes/kos-op-tail-call-var.md) |
| TAIL_CALL_VAR_NAMES | EXTRA | Tail call by name with keyword args | [section 17](opcodes/kos-op-tail-call-var-names.md) |
| TAIL_CALL_VAR_STAR | EXTRA | Tail call by name with *args/**kwargs | [section 17](opcodes/kos-op-tail-call-var-star.md) |
| TAIL_CALL_VAR_ATTR | EXTRA | Tail call obj.name; obj from VAR | [section 17](opcodes/kos-op-tail-call-var-attr.md) |
| TAIL_CALL_VAR_ATTR_NAMES | EXTRA | Tail call obj.name with keyword args; obj from VAR | [section 17](opcodes/kos-op-tail-call-var-attr-names.md) |
| TAIL_CALL_VAR_ATTR_STAR | EXTRA | Tail call obj.name with *args/**kwargs; obj from VAR | [section 17](opcodes/kos-op-tail-call-var-attr-star.md) |
| TAIL_CALL_FAST | EXTRA | Tail call callable from fast local slot | [section 17](opcodes/kos-op-tail-call-fast.md) |
| TAIL_CALL_FAST_NAMES | EXTRA | Tail call from fast slot with keyword args | [section 17](opcodes/kos-op-tail-call-fast-names.md) |
| TAIL_CALL_FAST_STAR | EXTRA | Tail call from fast slot with *args/**kwargs | [section 17](opcodes/kos-op-tail-call-fast-star.md) |
| TAIL_CALL_FAST_ATTR | EXTRA | Tail call obj.name; obj from fast slot | [section 17](opcodes/kos-op-tail-call-fast-attr.md) |
| TAIL_CALL_FAST_ATTR_NAMES | EXTRA | Tail call obj.name with keyword args; obj from fast slot | [section 17](opcodes/kos-op-tail-call-fast-attr-names.md) |
| TAIL_CALL_FAST_ATTR_STAR | EXTRA | Tail call obj.name with *args/**kwargs; obj from fast slot | [section 17](opcodes/kos-op-tail-call-fast-attr-star.md) |
| PUSH_FOREACH | NOARG | Push foreach region; break target from extent table | [section 18](opcodes/kos-op-push-foreach.md) |
| PUSH_LOOP | NOARG | Push loop region; break target from extent table | [section 18](opcodes/kos-op-push-loop.md) |
| POP_FOREACH | NOARG | Pop foreach region; push loop result | [section 18](opcodes/kos-op-pop-foreach.md) |
| POP_LOOP | NOARG | Pop loop region; push loop result | [section 18](opcodes/kos-op-pop-loop.md) |
| CLEAR_LOOP | NOARG | Disable top LOOP region (before POP_LOOP) | [section 18](opcodes/kos-op-clear-loop.md) |
| CLEAR_FOREACH | NOARG | Disable top FOREACH region (before POP_FOREACH) | [section 18](opcodes/kos-op-clear-foreach.md) |
| PUSH_NS | EXTRA | Push namespace scope (nspec + names in extra row) | [section 18](opcodes/kos-op-push-ns.md) |
| POP_NS | NOARG | Pop the NS scope; the revealed node's namespace becomes active and leftover values are delivered to its segment | [section 18](opcodes/kos-op-pop-ns.md) |
| PUSH_COLLECT | CONST | Push collector namespace scope | [section 18](opcodes/kos-op-push-collect.md) |
| POP_COLLECT | NOARG | Pop collect scope, push ns | [section 18](opcodes/kos-op-pop-collect.md) |
| BUILD_CLASS | NOARG | Consume pos+ns, call metaclass | [section 18](opcodes/kos-op-build-class.md) |
| PUSH_TRY | LABEL | Push try region | [section 18](opcodes/kos-op-push-try.md) |
| PUSH_FINALLY | LABEL | Push finally region and its segment | [section 18](opcodes/kos-op-push-finally.md) |
| CLEAR_TRY | NOARG | Disable the top TRY region on non-local exit; POP_TRY still drops the segment | [section 18](opcodes/kos-op-clear-try.md) |
| CLEAR_FINALLY | NOARG | Disable the top FINALLY region on non-local exit; POP_FINALLY still runs at the target | [section 18](opcodes/kos-op-clear-finally.md) |
| POP_TRY | NOARG | Promote the one value on the try segment, drop the segment, pop the region | [section 18](opcodes/kos-op-pop-try.md) |
| POP_FINALLY | NOARG | As POP_TRY, for the finally segment | [section 18](opcodes/kos-op-pop-finally.md) |
| PUSH_WITH | CONST | Pop the manager, call enter, bind the result, push a single-shot WITH node and a fresh segment | [section 18.1](opcodes/kos-op-push-with.md) |
| POP_WITH | NOARG | Call exit, pop the owned segment and the node, push None | [section 18.1](opcodes/kos-op-pop-with.md) |
| PUSH_DYN_WITH | CONST | As PUSH_WITH, with a multi-shot node that takes part in continuation transfer plans | [section 18.1](opcodes/kos-op-push-dyn-with.md) |
| POP_DYN_WITH | NOARG | As POP_WITH, for the multi-shot node | [section 18.1](opcodes/kos-op-pop-dyn-with.md) |
| OBJECT_COMPARE | CMPCOUNT | Chain-compare N stack values under the packed selector and push the result | [section 19](opcodes/kos-op-object-compare.md) |
| OBJECT_COMPARE_VAR | EXTRA | The ternary bounds idiom with the middle operand resolved by name | [section 19](opcodes/kos-op-object-compare-var.md) |
| OBJECT_COMPARE_FAST | EXTRA | The ternary bounds idiom with the middle operand from a fast slot | [section 19](opcodes/kos-op-object-compare-fast.md) |
| OBJECT_OPERAND | OPCOUNT | Reduce the N operands left to right under the operator | [section 19](opcodes/kos-op-object-operand.md) |
| OBJECT_OPERAND_VAR | EXTRA | The stack top under the operator with a named right operand | [section 19](opcodes/kos-op-object-operand.md) |
| OBJECT_OPERAND_FAST | EXTRA | The stack top under the operator with a fast-slot right operand | [section 19](opcodes/kos-op-object-operand.md) |
| VAR_OPERAND_VAR | EXTRA | Two named operands under the operator, the result pushed | [section 19](opcodes/kos-op-object-operand.md) |
| FAST_OPERAND_FAST | EXTRA | Two fast-slot operands under the operator, the result pushed | [section 19](opcodes/kos-op-object-operand.md) |
| OBJECT_INPLACE | OPCOUNT | In-place reduce the N operands left to right, through the `PyNumber_InPlace*` family | [section 19](opcodes/kos-op-object-inplace.md) |
| OBJECT_BIT_INVERT | NOARG | Pop TOS, push its bitwise inverse | [section 19](opcodes/kos-op-object-bit-invert.md) |
| OBJECT_DIR | NOARG | Pop TOS, push `dir(TOS)` | [section 19](opcodes/kos-op-object-dir.md) |
| OBJECT_DIVMOD | COUNT | Pop right then left, push `divmod(left, right)` | [section 19](opcodes/kos-op-object-divmod.md) |
| OBJECT_ITER | NOARG | Acquire an iterator over either protocol; a Koskari async generator passes through as itself | [section 19](opcodes/kos-op-object-iter.md) |
| OBJECT_LEN | NOARG | Pop TOS, push `len(TOS)` | [section 19](opcodes/kos-op-object-len.md) |
| OBJECT_NOT | NOARG | Pop TOS, push its logical negation | [section 19](opcodes/kos-op-object-not.md) |
| OBJECT_REPR | NOARG | Pop TOS, push `repr(TOS)` | [section 19](opcodes/kos-op-object-repr.md) |
| OBJECT_STR | COUNT | Build a string from the top N values (f-string assembly) | [section 19](opcodes/kos-op-object-str.md) |
| OBJECT_FORMAT | COUNT | Format the object; arg 2 takes the rules from the stack, arg 1 uses none | [section 19](opcodes/kos-op-object-format.md) |
| OBJECT_FORMAT_CONST | CONST | Format the object with format rules from the pool | [section 19](opcodes/kos-op-object-format-const.md) |
| OBJECT_TRUTH | NOARG | Pop TOS, push its truth value | [section 19](opcodes/kos-op-object-truth.md) |
| OBJECT_TYPE | NOARG | Pop TOS, push `type(TOS)` | [section 19](opcodes/kos-op-object-type.md) |
| OBJECT_VARS | NOARG | Pop TOS, push its `__dict__` | [section 19](opcodes/kos-op-object-vars.md) |
| PUSH_PROMPT | CONST | Push prompt (delimited cont.) | [section 20](opcodes/kos-op-push-prompt.md) |
| PUSH_SHIFT | CONST | Capture continuation, enter shift block | [section 20](opcodes/kos-op-push-shift.md) |
| POP_SHIFT | NOARG | Return from shift block without invoking k | [section 20](opcodes/kos-op-pop-shift.md) |
| POP_PROMPT | NOARG | Pop prompt | [section 20](opcodes/kos-op-pop-prompt.md) |
| PUSH_CC | NOARG | Push an undelimited continuation block onto the dynamic chain | [section 21](opcodes/kos-op-push-cc.md) |
| CAPTURE_CC | COUNT | Capture the CC extent's continuation and push it; 1 sets the callee on TOS aside for the capture | [section 21](opcodes/kos-op-capture-cc.md) |
| POP_CC | NOARG | Pop the CC block; also the landing site when a captured continuation is invoked | [section 21](opcodes/kos-op-pop-cc.md) |
| PUSH_WIND | COUNT | Pop the before (and after) thunks, push the wind extent, run before | [section 22](opcodes/kos-op-push-wind.md) |
| POP_WIND | NOARG | Pop the body result and the wind node, run the after thunk, deliver the result | [section 22](opcodes/kos-op-pop-wind.md) |
| PUSH_PARAM | CONST | Pop N values, bind them to the pooled names, enter the parameterize extent | [section 23](opcodes/kos-op-push-param.md) |
| POP_PARAM | NOARG | Pop the param extent and restore the cells it displaced | [section 23](opcodes/kos-op-pop-param.md) |
| PARAM | EXTRA | Read a parameter by name; count 1 supplies a stacked default, count 0 raises NameError on a miss | [section 23](opcodes/kos-op-param.md) |
| PARAM_CONST | EXTRA | Read a parameter by name with a pooled default; never raises for an unbound name | [section 23](opcodes/kos-op-param-const.md) |
| THEEND | NOARG | Unreachable; no runtime | [section 24](opcodes/kos-op-theend.md) |

**Completeness:** Every member of the `KosOpCode` enum in `koskari/lib/opcodes.incl` has a dedicated `### KOS_OP_*` heading in this document (some sections group related opcodes under one narrative first, then list each name). Search for `### KOS_OP_` to enumerate. Opcodes not listed in `opcodes.incl` are
not documented here until they are added to the enum.

## 3. No-op and compile-time

### KOS_OP_NOOP

- **Style**: NOARG. Argument unused.
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
| 2     | LABEL  | 0   | --        | target "done"; no-op if executed |
| 3     | RETURN | 1   | 1        | |

---

### KOS_OP_POSITION

- **Style**: COMPILE. Compile-time only.
- **Stack**: No change.
- **Invariants**: Position is recorded in the code object's position table during finalize. The POSITION instruction itself is normally omitted from the bytecode.
- **Behavior**: Source position marker. Finalize records the position (line, col) in the position table and **strips** the POSITION instruction. If a POSITION is retained, it advances PC only (safety net).
- **Compiler form:** Pre-compile arg may be (line, col) or a position token; finalize records position in the position table and strips POSITION. Post-compile: **normally omitted**; if retained, arg typically 0 or unused.

**Example:** Normally POSITION is stripped; position is stored in the position table only, so the bytecode would be: 0 CONST, 1 RETURN. If POSITION were retained (safety net), it would look like:

| index | opcode  | arg | arg view | comment |
|------:|---------|-----|----------|---------|
| 0     | POSITION| 0   | --        | line 1, col 0 (in position table); no-op if executed |
| 1     | CONST   | 0   | 0 (1)    | stack: [..., 1] |
| 2     | RETURN  | 1   | 1        | |

## 4. Stack

### KOS_OP_DUP

- **Style**: COUNT. Arg = number of elements to duplicate.
- **Stack**: `[..., a_1, ..., a_n] -> [..., a_1, ..., a_n, a_1, ..., a_n]` (top `arg` elements duplicated).
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
- **Stack**: `[..., a_1, ..., a_n] -> [...]` (pop `arg` elements).
- **Invariants**: Stack must have at least `arg` elements.

**Example:** POP 1: discard TOS.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (1)    | stack: [..., 1] |
| 1     | CONST  | 1   | 1 (2)    | stack: [..., 1, 2] |
| 2     | POP    | 1   | 1        | stack: [..., 1] |
| 3     | RETURN | 1   | 1        | |

### KOS_OP_UNPACK
- **Style**: COUNT. Arg = `n` (number of values to push).
- **Stack**: `[..., raw] -> [..., v_{n-1}, ..., v_1, v_0]` where `v_i` is the
  `i`-th element of the realized sequence and **TOS = v_0 = seq[0]** (so the
  first binding in source order can use `VAR_STORE` / `FAST_STORE` on TOS
  first). A tuple or list is read in place; any other iterable is pulled for
  `n` items plus one, so an over-long iterable is refused at its extra item
  and never drained.
- **Invariants**: `n >= 0`. For `n == 0`, TOS must be an empty iterable.
- **Errors**: `ValueError` if the count is not `n`; `TypeError` when TOS is
  not iterable.
- **Compiler form:** Pre-compile / post-compile arg = `n` (uint16).

**Example:** Unpack `(1, 2, 3)` so TOS is `1`, then return three values (stack
bottom to top matches push order).

| index | opcode  | arg | arg view | comment |
|------:|---------|-----|----------|---------|
| 0     | CONST   | 0   | 0 ((1,2,3)) | stack: [..., (1,2,3)] |
| 1     | UNPACK  | 3   | 3        | stack: [..., 3, 2, 1]; TOS = 1 |
| 2     | RETURN  | 3   | 3        | |

---

### KOS_OP_UNPACK_STAR
- **Style**: EXTRA (`COUNT`, `COUNT`, `NOARG`, `NOARG`). Instruction arg =
  index into the code object's **extra** table; that row has `a0 = left`,
  `a1 = right` (both non-negative counts).
- **Semantics**: Pop TOS, `PySequence_List(raw)`. Require `left + right > 0`
  and `len(seq) >= left + right`. Let `L = len(seq)`. The **middle** slice is
  `seq[left : L - right]` (inclusive start, exclusive end), materialized as a
  **tuple** (new reference). Pushes are ordered so that after all pushes,
  **TOS is the first prefix element** `seq[0]` (when `left > 0`):
  1. **Suffix** scalars: indices `L-1, L-2, ..., L-right` (each via
     `KosStack_PushRef`).
  2. **Middle**: one value -- the tuple -- via `KosStack_PUSH` (**stolen**
     reference from `PySequence_Tuple`).
  3. **Prefix** scalars: indices `left-1, ..., 0` (each via `PushRef`).
- **Stack (TOS right)**: `[..., raw] -> [..., seq[L-1], ..., seq[L-right],
  middle, seq[left-1], ..., seq[0]]` when all parts are non-empty; adjust when
  `left` or `right` is zero (no prefix or suffix pushes for that side).
- **Invariants**: Extra row matches schema; `left + right` fits in uint16;
  realized value must be a valid sequence for `PySequence_List` / slicing.
- **Errors**: `ValueError` if `left + right == 0` or `len(seq) < left + right`.
- **Compiler form:** Pre-compile extra operand is a 4-tuple
  `(left, right, 0, 0)`; `NOARG` slots are ignored by the assembler. Same
  pattern as other `EXTRA` opcodes (see section 1.7).

**Example:** `left=2`, `right=1`, seq `(1,2,3,4)`: suffix `[4]`, middle
`(3,)`, prefix `2, 1` with TOS `1`.

| index | opcode       | arg | arg view   | comment |
|------:|--------------|-----|------------|---------|
| 0     | CONST        | 0   | 0 ((1,2,3,4)) | |
| 1     | UNPACK_STAR  | 0   | 0 (2, 1, ...) | extra row: left=2, right=1 |
| 2     | RETURN       | 4   | 4          | stack bottom..top: 4, (3,), 2, 1 |

---

### KOS_OP_UNPACK_VAR
- **Style**: EXTRA (`VAR`, `VAR`, `NOARG`, `NOARG`). Row: `a0`, `a1` = constant
  pool indices of the two names (strings).
- **Stack**: `[..., raw] -> [...]`. No values pushed; **STORE** semantics only
  (same as `VAR_STORE` / `KosNamespace_Store` through `frame_ns_store`): `seq[0]`
  into the first name, `seq[1]` into the second. Pop TOS, `PySequence_List`,
  require `len == 2`, then store; release the temporary list.
- **Invariants**: Both names must already be bound in the frame namespace (see
  section 1.9 STORE vs BIND).
- **Errors**: `ValueError` if length != 2.
- **Compiler form:** Pre-compile extra tuple `(name0, name1, 0, 0)`; names
  intern to cpool like other `VAR` extra fields.

---

### KOS_OP_UNPACK_FAST
- **Style**: EXTRA (`FASTVAR`, `FASTVAR`, `NOARG`, `NOARG`). Row: `a0`, `a1` =
  fast local slot indices.
- **Stack**: `[..., raw] -> [...]`. Pop TOS, `PySequence_List`, require `len == 2`,
  then `KosNamespace_FastStore` for slot `a0` with `seq[0]` and slot `a1` with
  `seq[1]` (same ownership rules as `FAST_STORE`: namespace increments ref as
  needed).
- **Invariants**: Frame namespace supports fast locals; both indices in range
  (same as `FAST_STORE`).
- **Errors**: `ValueError` if length != 2; `RuntimeError` / `NameError` if fast
  namespace invalid or index out of bounds.
- **Compiler form:** Pre-compile extra tuple `(slot0, slot1, 0, 0)` with
  integer slot indices.

---

## 5. Constants

### KOS_OP_CONST

- **Style**: CONST. Arg = constant pool index (post-compile).
- **Stack**: `[...] -> [..., c]` where `c = cpool[arg]`.
- **Invariants**: `arg < cpool_len`.
- **Compiler form:** Pre-compile arg = literal value (e.g. `CONST "hello"`, `CONST
  42`). Finalize accumulates values into the constant pool and rewrites arg to
  the pool index.

**Example:** Load constant at index 0 (value 42). Pool: 0 = 42.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (42)   | stack: [..., 42] |
| 1     | RETURN | 1   | 1        | |

### KOS_OP_CONST2

- **Style**: EXTRA (CONST, CONST, NOARG, NOARG). Row = two constant pool indices (post-compile).
- **Stack**: `[...] -> [..., a, b]`. Push the two constants in row order. A constant is
  a borrowed pool reference, so a failed push leaves nothing to release.
- **Invariants**: Indices in the constant pool.
- **Compiler form:** Never emitted by specials. Produced only by
  **`ByteCodeProcessor.pack_runs`** (the trailing pack phase), which combines a
  run of consecutive bare CONST loads: quads, then one triple or one pair,
  then a bare load for a remainder of one. Constant folding runs earlier,
  so only loads the folder kept apart, such as a call's arguments, pack.
- **Implementation**: `koskari/lib/opcode/CONST2.c`
- **Covered by**: `tests/compile/test_combine_const.py`,
  `tests/reproducers/test_var_pack.py`

**Example:** Load constants a, b. Pool: 0 = a, 1 = b.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST2 | 0   | (0 (a), 1 (b), 0, 0) | stack: [..., a, b] |
| 1     | RETURN | 1   | 1        | |

### KOS_OP_CONST3

- **Style**: EXTRA (CONST, CONST, CONST, NOARG). Row = three constant pool indices (post-compile).
- **Stack**: `[...] -> [..., a, b, c]`. Push the three constants in row order. A constant is
  a borrowed pool reference, so a failed push leaves nothing to release.
- **Invariants**: Indices in the constant pool.
- **Compiler form:** Never emitted by specials. Produced only by
  **`ByteCodeProcessor.pack_runs`** (the trailing pack phase), which combines a
  run of consecutive bare CONST loads: quads, then one triple or one pair,
  then a bare load for a remainder of one. Constant folding runs earlier,
  so only loads the folder kept apart, such as a call's arguments, pack.
- **Implementation**: `koskari/lib/opcode/CONST3.c`
- **Covered by**: `tests/compile/test_combine_const.py`,
  `tests/reproducers/test_var_pack.py`

**Example:** Load constants a, b, c. Pool: 0 = a, 1 = b, 2 = c.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST3 | 0   | (0 (a), 1 (b), 2 (c), 0) | stack: [..., a, b, c] |
| 1     | RETURN | 1   | 1        | |

### KOS_OP_CONST4

- **Style**: EXTRA (CONST, CONST, CONST, CONST). Row = four constant pool indices (post-compile).
- **Stack**: `[...] -> [..., a, b, c, d]`. Push the four constants in row order. A constant is
  a borrowed pool reference, so a failed push leaves nothing to release.
- **Invariants**: Indices in the constant pool.
- **Compiler form:** Never emitted by specials. Produced only by
  **`ByteCodeProcessor.pack_runs`** (the trailing pack phase), which combines a
  run of consecutive bare CONST loads: quads, then one triple or one pair,
  then a bare load for a remainder of one. Constant folding runs earlier,
  so only loads the folder kept apart, such as a call's arguments, pack.
- **Implementation**: `koskari/lib/opcode/CONST4.c`
- **Covered by**: `tests/compile/test_combine_const.py`,
  `tests/reproducers/test_var_pack.py`

**Example:** Load constants a, b, c, d. Pool: 0 = a, 1 = b, 2 = c, 3 = d.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST4 | 0   | (0 (a), 1 (b), 2 (c), 3 (d)) | stack: [..., a, b, c, d] |
| 1     | RETURN | 1   | 1        | |

### 5.1 Pair construction and access

### KOS_OP_CAR
- **Style**: COUNT. Arg = repeat count: apply **car** that many times to the
  stack top (each step pops one pair and pushes its car).
- **Invariants**: **`n` >= 1**. **`n` <= 0** (including **CAR 0**) is invalid:
  **RuntimeError** (`CAR invoked with invalid argument count: ...`); the stack is
  not modified because the handler returns before popping TOS.
- **Stack**: With valid **`n` >= 1**, same effect as **`n`** chained **CAR 1**
  instructions (one stack pop of the initial value, local walk, one push). If
  any intermediate value is not a pair or is nil, **TypeError** (message
  includes step index and type / `nil`).

### KOS_OP_CDR
- **Style**: COUNT. Arg = repeat count: apply **cdr** that many times to the
  stack top (each step pops one pair and pushes its cdr).
- **Invariants**: **`n` >= 1**. **`n` <= 0** (including **CDR 0**) is invalid:
  **RuntimeError** (`CDR invoked with invalid argument count: ...`); the stack is
  not modified because the handler returns before popping TOS.
- **Stack**: With valid **`n` >= 1**, same effect as **`n`** chained **CDR 1**
  instructions (one stack pop, local walk, one push). If any intermediate value
  is not a pair or is nil, **TypeError** (message includes step index and type /
  `nil`).

### KOS_OP_CONS
- **Style**: COUNT. Arg = number of values on stack (bottom = first element,
  top = tail).
- **Stack**: `[..., e_0, ..., e_{count-1}] -> [..., result]` where result =
  (e_0 . (e_1 . ... (e_{count-1}) ...)). Implemented via KosStack_TailPtr,
  KosPair_ConsFromPtr, KosStack_Drop, then push result. count 0 -> push
  nil (INCREF).

### KOS_OP_PROPER
- **Style**: COUNT. Arg = number of element values on stack.
- **Stack**: `[..., e_0, ..., e_{count-1}] -> [..., result]` where result is the
  proper list `(e_0 e_1 ... e_{count-1})`. nil is implicitly appended as the
  tail. count 0 -> push nil directly.
- **Invariants**: Each `e_i` may be any Python object; none need be a pair.
- **Use-case**: Emitted by the quasiquote compiler for segments of non-splice
  elements. Equivalent to CONS with an extra `CONST nil` on the stack, but more
  compact: saves one instruction per list.

**Example:** Build the proper list `(a b c)` from three constants.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (sym a)  | stack: [..., a] |
| 1     | CONST  | 1   | 1 (sym b)  | stack: [..., a, b] |
| 2     | CONST  | 2   | 2 (sym c)  | stack: [..., a, b, c] |
| 3     | PROPER | 3   | 3          | stack: [..., (a b c)] |

### KOS_OP_BRACKET_CONS
- **Style**: COUNT. Arg = number of values on stack (bottom = first element,
  top = tail).
- **Stack**: `[..., e_0, ..., e_{count-1}] -> [..., result]` where result is the
  bracket pair chain `[e_0 . [e_1 . ... [e_{count-1}] ...]]`. The twin of
  [CONS](#kos_op_cons) over bracket pairs, built with
  KosBracketPair_ConsFromPtr. count 0 -> push bracket-nil.
- **Use-case**: Emitted by `compile/bracket-cons` and by the quasiquote
  compiler for a `[ ]` form with an explicit tail.

### KOS_OP_BRACKET_PROPER
- **Style**: COUNT. Arg = number of element values on stack.
- **Stack**: `[..., e_0, ..., e_{count-1}] -> [..., result]` where result is the
  proper bracket list `[e_0 e_1 ... e_{count-1}]`. bracket-nil is implicitly
  appended as the tail. count 0 -> push bracket-nil directly.
- **Use-case**: Emitted by `compile/bracket-proper` and by the quasiquote
  compiler for the segments of a `[ ]` form; the twin of **PROPER**.

### KOS_OP_PAIR_SPLICE
- **Style**: EXTRA (COUNT, COUNT). Row = count of stacked values (bottom =
  first), tail flag (0 or 1).
- **Stack**: tail 0: `[..., seg_0, ..., seg_{count-1}] -> [..., result]` where
  result is the concatenation of all segments as a single proper list. tail 1:
  `[..., seg_0, ..., seg_{count-2}, tail] -> [..., result]` where the top value
  is the tail and the count minus one values below it are the segments; the last
  cell's cdr is the tail. Each segment may be: nil (contributes nothing), a pair
  (proper or improper; its elements are appended), a Python tuple (its elements
  are appended), or any iterable. count 0 -> push nil.
- **Invariants**: All segments must be iterable; non-iterable values raise
  TypeError. With tail 0 the result is a proper pair list, or nil if all
  segments are empty. With tail 1 the result ends in the tail, or is the tail
  itself if all segments are empty. Dotted-pair segments are walked until
  nil; the implicit nil tail of the last segment is not included.
- **Use-case**: Emitted by the quasiquote compiler when at least one
  `unquote-splicing` site is present in a `( )` list; a `[ ]` list uses
  **BRACKET_SPLICE**. Each constant-only segment is folded into a single
  `CONST tuple` (which PAIR_SPLICE accepts natively), reducing instruction
  count significantly for mostly-literal lists.

**Example:** Splice a variable list `b` between constants.
`` `(a ,@b c) `` compiles to roughly:

| index | opcode      | arg | arg view        | comment |
|------:|-------------|-----|-----------------|---------|
| 0     | CONST       | 0   | 0 ((sym a,))    | segment: tuple `(a,)` |
| 1     | VAR         | 1   | 1 (b)           | splice value |
| 2     | CONST       | 2   | 2 ((sym c,))    | segment: tuple `(c,)` |
| 3     | PAIR_SPLICE | 0   | 0 (3, 0)        | stack: [..., (a . (b_0 ... b_n . (c)))] |

### KOS_OP_BRACKET_SPLICE
- **Style**: EXTRA (COUNT, COUNT). Row = count of stacked values (bottom =
  first), tail flag (0 or 1).
- **Stack**: tail 0: `[..., seg_0, ..., seg_{count-1}] -> [..., result]` where
  result is the concatenation of all segments, with a bracket pair as its
  leading cell. tail 1: `[..., seg_0, ..., seg_{count-2}, tail] -> [...,
  result]` where the top value is the tail and the last cell's cdr is that tail.
  The twin of [PAIR_SPLICE](#kos_op_pair_splice) for bracket lists; segments and
  the tail flag are accepted as PAIR_SPLICE accepts them. count 0 -> push
  bracket-nil.
- **Invariants**: Only the leading cell is a bracket pair; the rest of the
  spine is plain pairs ending in nil, or in the tail with tail 1. If every
  segment is empty the result is bracket-nil with tail 0, or the tail itself
  with tail 1.
- **Use-case**: Emitted by the quasiquote compiler for a `[ ]` list holding at
  least one `unquote-splicing` site. No name is looked up, so a binding of any
  name leaves the result unchanged.

## 6. Mode control

### KOS_OP_AWAIT

- **Style**: COUNT (stack element count).
- **Arg**: `1` -- pop TOS (awaitable), dispatch; may return `EVAL_AWAIT(val)` or push the resolved value; on resume from a Python awaitable, result is pushed onto the operand stack. `0` -- bare cooperative suspend: no stack pop or push; returns `EVAL_AWAIT(Py_None)` (no `py_await_iter`); `revive_await` does not push the resume value onto the operand stack (e.g. next instruction may be `CONST None` for `(await)`).

**Example:** Await a coroutine; TOS is the awaitable. On resume, result is pushed.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (coro)  | stack: [..., awaitable] |
| 1     | AWAIT  | 1   | 1        | pop one; suspend; on resume stack: [..., value] |
| 2     | RETURN | 1   | 1        | |

---

### KOS_OP_BREAK

- **Style**: COUNT. Arg = number of stack elements to pop before jumping.
- **Stack**: Pops `arg` elements; control transfers to break target of innermost LOOP/FOREACH extent.
- **Invariants**: Must be inside a LOOP or FOREACH extent.

**Example:** Inside a loop; pop 0 and jump to extent break_pc (instruction after the matching POP_LOOP or POP_FOREACH).

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 2     | BREAK  | 0   | 0        | pop 0, jump to break target (e.g. pc 6) |
| ...     | ...      | ...   | ...        | |
| 6     | POP_LOOP | 0 | 0        | break target (LOOP extent; use POP_FOREACH for FOREACH) |

---

### KOS_OP_CONTINUE

- **Style**: COUNT. Arg = number of stack elements to pop before jumping.
- **Stack**: Pops `arg` elements; control transfers to continue target of innermost LOOP/FOREACH extent.
- **Invariants**: Must be inside a LOOP or FOREACH extent.

**Example:** Inside a loop; pop 0 and jump to extent continue_pc (e.g. loop head).

| index | opcode   | arg | arg view | comment |
|------:|---------|-----|----------|---------|
| 4     | CONTINUE| 0   | 0        | pop 0, jump to continue target (e.g. pc 1) |
| ...     | ...       | ...   | ...        | |

---

### KOS_OP_RAISE

- **Style**: COUNT. Arg = number of values to pop (e.g. 1 for exception instance).
- **Stack**: Pops `arg` values; raises with the popped exception.
- **Invariants**: Popped value(s) form the exception to raise.

**Example:** Raise exception from TOS (arg = 1). Pool: 0 = ValueError("bad").

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (ValueError(...)) | stack: [..., exc] |
| 1     | RAISE  | 1   | 1        | pop 1, raise exc; stack unwound |

---

### KOS_OP_RETURN

- **Style**: COUNT. Arg = number of values to package as the return value: `0` returns `None`, `1` returns the single popped value, and `N > 1` pops `N` values and returns them as a tuple.
- **Stack**: Pops the return value(s) per `arg`. The returning frame's dynamic extent unwinds, finally blocks and wind thunks included, and the frame is popped. The value goes where the frame's parent expects it: pushed onto a calling Koskari frame, delivered to the iterator or native handler driving the frame, or handed back to the host when the frame is the outermost one or a generator's. A return across a delimited continuation boundary delivers to that boundary.
- **Invariants**: See execution-invariants: in-loop flattening for RETURN.
- **See also**: [compile/return](../specials/compile-return.md).

**Example:** Return TOS (arg = 1). Pool: 0 = 42.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (42)   | stack: [..., 42] |
| 1     | RETURN | 1   | 1        | return 42 to the caller |

---

### KOS_OP_YIELD

- **Style**: COUNT. Arg = number of values to package as the yielded value: `0` yields `None`, `1` yields the single popped value, and `N > 1` pops `N` values and yields them as a tuple.
- **Stack**: Pops the yielded value(s). Sets **`KOS_CODE_YIELDS`** on the code object. The handler freezes the running generator's span of the dynamic chain into its generator node. A flattened generator, one consumed inside the same evaluator, then hands the value to the consuming frame (a for-each step, a next request, or a yield-from waiting on it) and continues in that frame without stopping the evaluator. A generator run from outside stops the evaluator with the yielded value.
- **Invariants**: Code is treated as async generator when YIELDS flag set.
- **See also**: [compile/yield](../specials/compile-yield.md); **`koskari/lib/opcode/YIELD.c`**.

**Example:** Yield TOS (arg = 1). Pool: 0 = 100.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (100)  | stack: [..., 100] |
| 1     | YIELD  | 1   | 1        | yield 100; on resume continue |

---

### KOS_OP_RETURN_CONST
- **Style**: CONST. Arg = pool index of the constant to return.
- **Stack**: `[...] -> [...]` (no stack effect). Returns the constant exactly as **`KOS_OP_RETURN`** **1** returns a popped value: the same plain, region and resume deliveries, else the RETURN signal to the trampoline.
- **Compiler form:** Produced by `ByteCodeProcessor.fuse_feeders` when **`KOS_OP_RETURN`** **1** is fed by a bare **`KOS_OP_CONST`**; a `None` constant takes **`KOS_OP_RETURN`** **0** in the squish pass instead.
- **Implementation**: `koskari/lib/opcode/RETURN_CONST.c`

---

### KOS_OP_YIELD_CONST
- **Style**: CONST. Arg = pool index of the constant to yield.
- **Stack**: `[...] -> [...]` (no stack effect). Yields the constant exactly as **`KOS_OP_YIELD`** **1** yields a popped value: a flattened generator delivers to its consumer and threads into it, any other returns the YIELD signal. Marks the code as a generator, as **`KOS_OP_YIELD`** does.
- **Compiler form:** Produced by `ByteCodeProcessor.fuse_feeders` when **`KOS_OP_YIELD`** **1** is fed by a bare **`KOS_OP_CONST`**; a `None` constant takes **`KOS_OP_YIELD`** **0** in the squish pass instead.
- **Implementation**: `koskari/lib/opcode/YIELD_CONST.c`

---

### KOS_OP_YIELD_FROM

- **Style**: NOARG.
- **Stack**: Pops TOS, the value to delegate to. Pushes nothing. Each value the delegate produces is yielded out of the enclosing generator as that generator's own yield. When the delegate is exhausted, execution continues at the next instruction; the delegate's return value is discarded. `compile/yield-from` emits `CONST None` after the opcode so the form has a value.
- **Execution model**: Single-opcode delegation primitive (conduit); not loop sugar. Full send/throw/close propagation; async flattening for Python and Koskari async gens. The delegate is held as the frame's resume state for the whole delegation; every suspension of the delegation is a resume on the generator's frame, and what arrives at the generator (a sent value, a thrown exception, a close) arrives at that resume.
- **Delegate kinds** (chosen at first entry by normalizing TOS): **sync iterator** (PyIter_Next); **sync generator** (send/throw/close); **Python async iterator** (__anext__); **Python async generator** (__anext__/asend/athrow/aclose); **Koskari async generator** (driven in-process via koskari_eval). Callables are called once; the return value is classified as above. Fallback: `iter(obj)` for sync iteration.
- **Rejected**: Coroutines (Koskari or Python). Normalization raises `TypeError: "coroutine object is not async iterable"`. Delegate results (yielded or final value) are never re-awaited; they are emitted as values only.
- **Invariants**: TOS on first entry must be an iterable or async-iterable (or callable returning one).
- **Implementation**: Handler, normalization, the per-kind drive resumes and the delegate's close on teardown live in `koskari/lib/opcode/YIELD_FROM.c` (included into `eval.c`). See `koskari/lib/opcode/README.md` for the opcode-fragment layout.
- **See also**: [compile/yield-from](../specials/compile-yield-from.md)
- **Covered by**: `tests/lib/opcode/test_yield_from.py` (sync/async delegates, Koskari async gen, send/throw/close, final values, type errors).

**Example:** Delegate to a sync iterator; each yielded value is emitted as the generator's own, and nothing is pushed when the iterator exhausts. Pool: 0 = [1, 2, 3].

| index | opcode   | arg | arg view | comment |
|------:|---------|-----|----------|---------|
| 0     | CONST   | 0   | 0 ([1,2,3]) | stack: [..., iterable] |
| 1     | YIELD_FROM | 0 | 0        | consume TOS as the delegate; the instruction halts on its resume, which re-yields each element and answers each send, until the delegate ends; stack: [...] |
| 2     | RETURN  | 1   | 1        | |

## 7. Iteration

### KOS_OP_ITER_NEXT

- **Style**: LABEL. Arg = else branch pc (post-compile).
- **Stack**: Uses region iterator (PUSH_FOREACH). If next available: push value; else jump to `arg`. Auto-await if iterator is async.
- **Invariants**: Must be inside a PUSH_FOREACH extent. Jump target `arg` must be local.
- **Compiler form:** Pre-compile arg = string label (else / exhausted target); post-compile arg = pc.

**Example:** For-each body: get next from region iterator; if exhausted jump to 6.

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 0     | CONST    | 0   | 0 ([1,2,3]) | stack: [..., iterable] |
| 1     | PUSH_FOREACH | 0 | 0        | push region; iterator from iterable; break target from extent table |
| 2     | ITER_NEXT| 6   | 6 (-> 6)  | next from region; if exhausted -> pc=6. stack: [..., val] or jump |
| 3     | POP      | 1   | 1        | consume value (body) |
| 4     | JUMP     | 2   | 2 (-> 2) | loop back |
| 5     | POP_FOREACH | 0 | 0        | push loop result (None if unset) |
| 6     | LABEL    | --   | --        | else (exhausted) |

---

### KOS_OP_ITER_NEXT_STORE_FAST
- **Style**: EXTRA. Fields: FASTVAR (slot), LABEL (else branch pc, post-compile).
- **Stack**: Uses region iterator (PUSH_FOREACH). If next available: store the element into the fast slot, stack unchanged; else jump to the label. Auto-await if the iterator is async: a deferred element is stored into the slot when it arrives.
- **Invariants**: Must be inside a PUSH_FOREACH extent. The label must be local. The slot must be a fast slot of the current frame namespace.
- **Compiler form:** Produced by the processor from `ITER_NEXT` followed by `VAR_STORE`, as `ITER_NEXT_STORE_VAR`, then lowered to this opcode when the name has a fast slot. The for-each special's binding form compiles to it.

**Example:** For-each body: next from the region iterator into slot 0; if exhausted jump to 5.

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 0     | CONST    | 0   | 0 ([1,2,3]) | stack: [..., iterable] |
| 1     | PUSH_FOREACH | 0 | 0        | push region; iterator from iterable |
| 2     | ITER_NEXT_STORE_FAST | 0 | (0, -> 5) | next from region into slot 0; if exhausted -> pc=5. stack unchanged |
| 3     | CONTINUE | 0   | 0        | back to the step |
| 4     | POP_FOREACH | 0 | 0        | push loop result (None if unset) |
| 5     | LABEL    | --   | --        | else (exhausted) |

---

### KOS_OP_ITER_NEXT_STORE_VAR
- **Style**: EXTRA. Fields: VAR (name), LABEL (else branch pc, post-compile).
- **Stack**: Uses region iterator (PUSH_FOREACH). If next available: store the element under the name in the current frame namespace, stack unchanged; else jump to the label. Auto-await if the iterator is async: a deferred element is stored under the name when it arrives.
- **Invariants**: Must be inside a PUSH_FOREACH extent. The label must be local.
- **Compiler form:** Produced by the processor from `ITER_NEXT` followed by `VAR_STORE`. A name with a fast slot lowers the opcode to `ITER_NEXT_STORE_FAST`; this form remains where the name has none, such as a module-level loop.

**Example:** Module-level for-each: next from the region iterator into `x`; if exhausted jump to 5.

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 0     | CONST    | 0   | 0 ([1,2,3]) | stack: [..., iterable] |
| 1     | PUSH_FOREACH | 0 | 0        | push region; iterator from iterable |
| 2     | ITER_NEXT_STORE_VAR | 0 | (x, -> 5) | next from region into x; if exhausted -> pc=5. stack unchanged |
| 3     | CONTINUE | 0   | 0        | back to the step |
| 4     | POP_FOREACH | 0 | 0        | push loop result (None if unset) |
| 5     | LABEL    | --   | --        | else (exhausted) |

---

### KOS_OP_OBJECT_NEXT

- **Style**: COUNT. Arg = 1 or 2: 1 = TOS is iterator or async iterator (StopIteration raises on exhaustion); 2 = TOS = default, below = iterator or async iterator (StopIteration pushes default).
- **Stack**: Stack-based next from iterator or async iterator. May suspend on async iterator.
- **Invariants**: TOS (or below-TOS for arg=2) must be a sync iterator or an async iterator. A bare iterable (e.g. list, tuple) is not acceptable; use OBJECT_ITER or equivalent to obtain an iterator first. Stack layout per arg (1 or 2 elements as described).

**Example (arg=1):** TOS = iterator or async iterator; push next or raise StopIteration.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (iter) | stack: [..., iterator or async iterator] |
| 1     | OBJECT_NEXT | 1 | 1      | stack: [..., next_val] or raise StopIteration |

**Example (arg=2):** TOS = default, below = iterator or async iterator; push next or default.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (iter) | stack: [..., iterator or async iterator] |
| 1     | CONST  | 1   | 1 (99)   | stack: [..., iterator or async iterator, default] |
| 2     | OBJECT_NEXT | 2 | 2      | stack: [..., next_val] or [..., 99] if exhausted |

### KOS_OP_VAR_NEXT
- **Style**: EXTRA (VAR, COUNT). Row = iterator name (pool), n (1 or 2).
- **Stack**: n=1: `[...] -> [..., value]`; n=2: `[..., default] -> [..., value-or-default]`. Fused VAR + OBJECT_NEXT: resolve the iterator name and take the next element as OBJECT_NEXT does. A Koskari async generator runs flattened in the same evaluator; an awaited step suspends the frame and completes the instruction when it resumes. 1-arg raises StopIteration on exhaustion; 2-arg delivers the default.
- **Invariants**: The fused name resolution runs after the default row (the fusion requires a bare default feeder, so the reorder is unobservable beyond NameError timing).
- **Compiler form:** Produced by `ByteCodeProcessor.fuse_feeders` when OBJECT_NEXT's iterator feeder is a bare VAR load (the 2-arg form also requires a bare VAR/CONST default). Lowers to FAST_NEXT through the generic schema-driven `convert_fast` path.
- **Implementation**: `koskari/lib/opcode/VAR_NEXT.c` (cold handler)
- **Covered by**: `tests/compile/test_fuse_feeders.py`,
  `tests/lib/opcode/test_object_next.py`

### KOS_OP_FAST_NEXT
- **Style**: EXTRA (FASTVAR, COUNT). Row = iterator slot, n (1 or 2).
- **Stack**: As VAR_NEXT, with the iterator loaded from its fast slot.
- **Compiler form:** Post-compile only: the generic `convert_fast` lowering of VAR_NEXT when the iterator name has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_NEXT.c` (cold handler)
- **Covered by**: `tests/compile/test_fuse_feeders.py`,
  `tests/lib/opcode/test_object_next.py`

## 8. Jumps

### KOS_OP_JUMP

- **Style**: LABEL. Arg = target pc (post-compile).
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
- **Stack**: Pops one value; if false, jump to `arg`; else advance PC.
- **Invariants**: Jump locality for target.
- **Compiler form:** Pre-compile arg = string label; post-compile arg = pc.

**Example:** Jump if TOS is false (e.g. "else" branch). Pool: 0 = False, 1 = 42, 2 = 0.

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 0     | CONST      | 0   | 0 (False)| stack: [..., False] |
| 1     | JUMP_FALSE | 4   | 4 (-> 4) | pop TOS; if false -> pc=4 |
| 2     | CONST      | 1   | 1 (42)   | stack: [..., 42] |
| 3     | JUMP       | 5   | 5 (-> 5) | |
| 4     | LABEL      | --   | --        | else |
| 5     | CONST      | 2   | 2 (0)    | stack: [..., 0] |
| 6     | RETURN     | 1   | 1        | |

---

### KOS_OP_JUMP_TRUE

- **Style**: LABEL. Arg = target pc (post-compile).
- **Stack**: Pops one value; if true, jump to `arg`; else advance PC.
- **Invariants**: Jump locality for target.
- **Compiler form:** Pre-compile arg = string label; post-compile arg = pc.

**Example:** Jump if TOS is true (e.g. "then" branch).

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 0     | VAR        | 0   | 0 (x)    | stack: [..., x] |
| 1     | JUMP_TRUE  | 3   | 3 (-> 3) | pop TOS; if true -> pc=3 |
| 2     | CONST      | 0   | 0 (0)    | else path |
| 3     | LABEL      | --   | --        | then path |
| 4     | RETURN     | 1   | 1        | |

---

### KOS_OP_JUMP_FALSE_VAR

- **Style**: EXTRA (VAR, LABEL, NOARG, NOARG). Row = name, target pc.
- **Stack**: No stack effect. Read the value the name resolves to in the
  current frame's namespace; if false, jump to the target, else advance.
  Raises `NameError` when the name is unbound.
- **Invariants**: Jump locality for target.
- **Compiler form:** Produced by `ByteCodeProcessor.fuse_feeders`
  absorbing a bare `VAR` load into the `JUMP_FALSE` it feeds; a `CONST`
  feeder was folded in the squish. Lowers to
  [JUMP_FALSE_FAST](#kos_op_jump_false_fast) when the name has a fast
  slot.

**Example:** `(when x ...)` with `x` resolved by name.

| index | opcode         | arg | arg view                | comment |
|------:|----------------|-----|-------------------------|---------|
| 0     | JUMP_FALSE_VAR | 0   | (v#0 ('x'), -> 3, 0, 0) | if x is false -> pc=3 |
| 1     | CONST          | 1   | 1 (1)                   | body |
| 2     | JUMP           | 4   | 4 (-> 4)                | |
| 3     | CONST          | 2   | 2 (None)                | |
| 4     | RETURN         | 1   | 1                       | |

---

### KOS_OP_JUMP_TRUE_VAR

- **Style**: EXTRA (VAR, LABEL, NOARG, NOARG). Row = name, target pc.
- **Stack**: No stack effect. Read the value the name resolves to in the
  current frame's namespace; if true, jump to the target, else advance.
  Raises `NameError` when the name is unbound.
- **Invariants**: Jump locality for target.
- **Compiler form:** Produced by `ByteCodeProcessor.fuse_feeders`
  absorbing a bare `VAR` load into the `JUMP_TRUE` it feeds; a `CONST`
  feeder was folded in the squish. Lowers to
  [JUMP_TRUE_FAST](#kos_op_jump_true_fast) when the name has a fast
  slot.

---

### KOS_OP_JUMP_FALSE_FAST

- **Style**: EXTRA (FASTVAR, LABEL, NOARG, NOARG). Row = fast slot,
  target pc.
- **Stack**: No stack effect. Read the fast slot; if false, jump to the
  target, else advance. Raises `NameError` when the slot is empty.
- **Invariants**: Jump locality for target.
- **Compiler form:** Post-compile only: the `convert_fast` lowering of
  [JUMP_FALSE_VAR](#kos_op_jump_false_var) when the name has a fast
  slot. `(if x ...)`, `(when x ...)`, `(while flag ...)` and a
  bare-variable clause of a threaded `and`/`or` test take one dispatch.

**Example:** `(when x ...)` with `x` in fast slot 0.

| index | opcode          | arg | arg view                   | comment |
|------:|-----------------|-----|----------------------------|---------|
| 0     | JUMP_FALSE_FAST | 0   | (fast 0 ("x"), -> 3, 0, 0) | if x is false -> pc=3 |
| 1     | CONST           | 0   | 0 (1)                      | body |
| 2     | JUMP            | 4   | 4 (-> 4)                   | |
| 3     | CONST           | 1   | 1 (None)                   | |
| 4     | RETURN          | 1   | 1                          | |

---

### KOS_OP_JUMP_TRUE_FAST

- **Style**: EXTRA (FASTVAR, LABEL, NOARG, NOARG). Row = fast slot,
  target pc.
- **Stack**: No stack effect. Read the fast slot; if true, jump to the
  target, else advance. Raises `NameError` when the slot is empty.
- **Invariants**: Jump locality for target.
- **Compiler form:** Post-compile only: the `convert_fast` lowering of
  [JUMP_TRUE_VAR](#kos_op_jump_true_var) when the name has a fast slot.
  `(if (not x) ...)` and an `or` clause of a threaded test over a bare
  variable take one dispatch.

---

### KOS_OP_NILP

- **Style**: NOARG.
- **Stack**: `[..., x] -> [..., bool]`. Pop TOS; push `True` when it is an
  empty-list singleton (`nil` or bracket-nil), else `False`.
- **Compiler form:** `ByteCodeProcessor` lowers NILP followed by JUMP_FALSE
  into one [CHECK_NIL](#kos_op_check_nil).
- **Implementation**: `koskari/lib/opcode/NILP.c`

### KOS_OP_CHECK_NIL

- **Style**: LABEL. Arg = target pc (post-compile).
- **Stack**: `[..., x] -> [...]`. Pop TOS; branch to arg when it is not an
  empty-list singleton; fall through when it is.
- **Invariants**: Jump locality for target. The squashed form of
  [NILP](#kos_op_nilp) followed by JUMP_FALSE.
- **Compiler form:** Produced by `ByteCodeProcessor`; pre-compile arg = string
  label, post-compile arg = pc.
- **Implementation**: `koskari/lib/opcode/CHECK_NIL.c`

### KOS_OP_JUMP_AND

- **Style**: LABEL. Short-circuit AND: jump if TOS is false.
- **Stack**: Pops one value; if false, push it back and jump to `arg`; if true,
  discard it and advance. Either way TOS is consumed on the fall-through path.
- **Invariants**: Jump locality for target.
- **Compiler form:** Pre-compile arg = string label; post-compile arg = pc.

**Compiler use-case:** `compile/and` is the canonical consumer
(`koskari/compile/specials/conditionals.py`). For `(and A B C)` the
compiler emits one `JUMP_AND done` after each non-last term; the last term is
compiled without a jump. Because `JUMP_AND` already discards the value on the
fall-through path, no `POP` is needed between terms.

```
eval A
JUMP_AND done   ; falsy -> push A back, jump to done
eval B          ; A was already discarded on fall-through
JUMP_AND done
eval C          ; last term -- value stays on stack
done:
```

**Example:** Short-circuit and: if first operand false, jump to "fail".

| index | opcode    | arg | arg view | comment |
|------:|-----------|-----|----------|---------|
| 0     | CONST     | 0   | 0 (True) | stack: [..., a] |
| 1     | JUMP_AND  | 4   | 4 (-> 4) | if not a -> pc=4 |
| 2     | CONST     | 1   | 1 (True) | stack: [..., b] |
| 3     | JUMP_AND  | 4   | 4 (-> 4) | if not b -> pc=4 |
| 4     | LABEL     | --   | --        | fail (or result) |

---

### KOS_OP_JUMP_OR

- **Style**: LABEL. Short-circuit OR: jump if TOS is true.
- **Stack**: Pops one value; if true, push it back and jump to `arg`; if false,
  discard it and advance. Either way TOS is consumed on the fall-through path.
- **Invariants**: Jump locality for target.
- **Compiler form:** Pre-compile arg = string label; post-compile arg = pc.

**Compiler use-case:** `compile/or` is the canonical consumer
(`koskari/compile/specials/conditionals.py`). For `(or A B C)` the
compiler emits one `JUMP_OR done` after each non-last term; the last term is
compiled without a jump. Because `JUMP_OR` already discards the value on the
fall-through path, no `POP` is needed between terms.

```
eval A
JUMP_OR done    ; truthy -> push A back, jump to done
eval B          ; A was already discarded on fall-through
JUMP_OR done
eval C          ; last term -- value stays on stack
done:
```

**Example:** Short-circuit or: if first operand true, jump to "ok".

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 0     | CONST    | 0   | 0 (False)| stack: [..., a] |
| 1     | JUMP_OR  | 4   | 4 (-> 4) | if a -> pc=4 |
| 2     | CONST    | 1   | 1 (False)| stack: [..., b] |
| 3     | JUMP_OR  | 4   | 4 (-> 4) | if b -> pc=4 |
| 4     | LABEL    | --   | --        | ok |
| 5     | RETURN   | 1   | 1        | |

### KOS_OP_JUMP_AND_VAR

- **Style**: EXTRA (VAR, LABEL, NOARG, NOARG). Row = name, target pc.
- **Stack**: `[...] -> [..., x]` on the jump, `[...] -> [...]` on the
  fall-through. Read the value the name resolves to in the current
  frame's namespace; if false, push it and jump to the target; if true,
  advance with nothing pushed. Raises `NameError` when the name is
  unbound.
- **Invariants**: Jump locality for target.
- **Compiler form:** Produced by `ByteCodeProcessor.fuse_feeders`
  absorbing a bare `VAR` load into the `JUMP_AND` it feeds. Lowers to
  [JUMP_AND_FAST](#kos_op_jump_and_fast) when the name has a fast slot.

---

### KOS_OP_JUMP_OR_VAR

- **Style**: EXTRA (VAR, LABEL, NOARG, NOARG). Row = name, target pc.
- **Stack**: `[...] -> [..., x]` on the jump, `[...] -> [...]` on the
  fall-through. Read the value the name resolves to in the current
  frame's namespace; if true, push it and jump to the target; if false,
  advance with nothing pushed. Raises `NameError` when the name is
  unbound.
- **Invariants**: Jump locality for target.
- **Compiler form:** Produced by `ByteCodeProcessor.fuse_feeders`
  absorbing a bare `VAR` load into the `JUMP_OR` it feeds. Lowers to
  [JUMP_OR_FAST](#kos_op_jump_or_fast) when the name has a fast slot.

---

### KOS_OP_JUMP_AND_FAST

- **Style**: EXTRA (FASTVAR, LABEL, NOARG, NOARG). Row = fast slot,
  target pc.
- **Stack**: `[...] -> [..., x]` on the jump, `[...] -> [...]` on the
  fall-through. Read the fast slot; if false, push the value and jump
  to the target; if true, advance with nothing pushed and no reference
  taken. Raises `NameError` when the slot is empty.
- **Invariants**: Jump locality for target.
- **Compiler form:** Post-compile only: the `convert_fast` lowering of
  [JUMP_AND_VAR](#kos_op_jump_and_var) when the name has a fast slot.

---

### KOS_OP_JUMP_OR_FAST

- **Style**: EXTRA (FASTVAR, LABEL, NOARG, NOARG). Row = fast slot,
  target pc.
- **Stack**: `[...] -> [..., x]` on the jump, `[...] -> [...]` on the
  fall-through. Read the fast slot; if true, push the value and jump to
  the target; if false, advance with nothing pushed and no reference
  taken. Raises `NameError` when the slot is empty.
- **Invariants**: Jump locality for target.
- **Compiler form:** Post-compile only: the `convert_fast` lowering of
  [JUMP_OR_VAR](#kos_op_jump_or_var) when the name has a fast slot.

**Example:** `(or x default)` in value position, both in fast slots.

| index | opcode       | arg | arg view                   | comment |
|------:|--------------|-----|----------------------------|---------|
| 0     | JUMP_OR_FAST | 0   | (fast 0 ("x"), -> 2, 0, 0) | x true: push x, pc=2 |
| 1     | FAST         | 1   | 1 (local "default")        | stack: [..., default] |
| 2     | RETURN       | 1   | 1                          | |

## 9. Check jumps

Check jumps: jump to the target label when the predicate does not hold. Jump locality applies to every target. The comparison check is the selector-keyed **CHECK_COMPARE**, **EXTRA** with a `(CMP, COUNT, LABEL, RAW)` row: `a0` is a KosCompare selector, `a1` the operand count (>= 2), `a2` the target pc, `a3` the negate flag. It is variadic -- the top `COUNT` operands are chain-compared left-to-right under the selector (e.g. `a < b < c`), negated when `a3` is set, and the jump is taken when the chain fails. IS, IS_NOT, IN and NOT_IN are strictly binary at this opcode; IS_INSTANCE and IS_SUBCLASS test the first operand against every operand after it. The remaining checks (exception, nil) take a single **LABEL** arg.

### KOS_OP_CHECK_COMPARE
- **Style**: EXTRA (CMP, COUNT, LABEL, RAW). Row = KosCompare selector,
  operand count, target pc, negate flag.
- **Stack**: `[..., a1, ..., aN] -> [...]`. Chain-compare the top COUNT
  operands under the selector, negated when the negate flag is set;
  jump to LABEL when the chain fails. The operands are dropped
  either way. Raises `TypeError` when `COUNT < 2`.
- **Invariants**: Selector is a valid KosCompare ordinal. The richcompare
  six (LT/LE/EQ/NE/GT/GE, ordinals 0-5 == CPython's Py_LT..Py_GE) chain
  over any COUNT; IS, IS_NOT, IN and NOT_IN require COUNT == 2;
  IS_INSTANCE and IS_SUBCLASS take any COUNT >= 2.
- **Compiler form:** Produced by `ByteCodeProcessor` fusing
  `OBJECT_COMPARE` + `JUMP_FALSE` into one instruction, and
  `OBJECT_COMPARE` + `JUMP_TRUE` into the same with the flag set. Never
  emitted directly by
  specials.

**Example:** Branch unless `1 < x < 100`. Selector LT = 0.

| index | opcode        | arg | arg view          | comment |
|------:|---------------|-----|-------------------|---------|
| 0     | CONST         | 0   | 0 (1)             | stack: [..., 1] |
| 1     | FAST          | 0   | 0 (x)             | stack: [..., 1, x] |
| 2     | CONST         | 1   | 1 (100)           | stack: [..., 1, x, 100] |
| 3     | CHECK_COMPARE | 0   | (LT, 3, -> 6, 0)  | jump to 6 unless 1 < x < 100 |

---

### KOS_OP_CHECK_COMPARE_VAR
- **Style**: EXTRA (CMPSRC, VAR, VAR, LABEL). Row = packed selector +
  source bits, two operand slots, target pc; the negate bit makes the check test the negated comparison. A set source bit overrides
  that operand slot from its declared VAR kind to CONST (pool index).
- **Stack**: No stack effect. Arity-2 fused comparison branch: load both
  operands per the source bits (frame namespace name lookup, or constant pool), compare
  under the selector, jump to LABEL when the comparison, negated under a set negate bit, fails.
- **Invariants**: Selector is a valid KosCompare ordinal; both-CONST rows
  are never emitted (that is a folding case).
- **Compiler form:** Produced by `ByteCodeProcessor.fuse_feeders` (a late `ByteCodeProcessor` pass) absorbing two bare VAR/CONST feeder rows into an arity-2 [CHECK_COMPARE](#kos_op_check_compare). Runs before `pack_var_runs` so this fusion gets first claim on the feeders. Lowers to CHECK_COMPARE_FAST via the CMPSRC-aware `convert_fast` case (all-or-nothing on the name slots).
- **Implementation**: `koskari/lib/opcode/CHECK_COMPARE_VAR.c` (cold handler)
- **Covered by**: `tests/compile/test_fuse_feeders.py`,
  `tests/lib/opcode/test_compare.py`

**Example:** Loop condition `i < n` on fast slots 1 and 0.

| index | opcode             | arg | arg view                          | comment |
|------:|--------------------|-----|-----------------------------------|---------|
| 3     | CHECK_COMPARE_VAR | 2 | (LT, v#1 ('i'), v#0 ('n'), -> 9) | jump to 9 unless i < n |

---

### KOS_OP_CHECK_COMPARE_FAST
- **Style**: EXTRA (CMPSRC, FASTVAR, FASTVAR, LABEL). Row = packed selector +
  source bits, two operand slots, target pc; the negate bit makes the check test the negated comparison. A set source bit overrides
  that operand slot from its declared FASTVAR kind to CONST (pool index).
- **Stack**: No stack effect. Arity-2 fused comparison branch: load both
  operands per the source bits (fast local slot, or constant pool), compare
  under the selector, jump to LABEL when the comparison, negated under a set negate bit, fails.
- **Invariants**: Selector is a valid KosCompare ordinal; both-CONST rows
  are never emitted (that is a folding case).
- **Compiler form:** Post-compile only: the CMPSRC-aware `convert_fast` lowering of CHECK_COMPARE_VAR when every name-sourced slot has a fast slot. Constant-bit slots pass through untouched.
- **Implementation**: `koskari/lib/opcode/CHECK_COMPARE_FAST.c`
- **Covered by**: `tests/compile/test_fuse_feeders.py`,
  `tests/lib/opcode/test_compare.py`

**Example:** Loop condition `i < n` on fast slots 1 and 0.

| index | opcode             | arg | arg view                          | comment |
|------:|--------------------|-----|-----------------------------------|---------|
| 3     | CHECK_COMPARE_FAST | 2 | (LT, fast 1 ("i"), fast 0 ("n"), -> 9) | jump to 9 unless i < n |

---

### KOS_OP_CHECK_EXCEPTION

- **Style**: LABEL. Arg = target pc (post-compile).
- **Stack**: Pop TOS (exception type or tuple of types). If current exception matches that value, leave PC unchanged; else set PC to arg (branch to next handler).
- **Invariants**: Jump locality for target. Used inside exception handling path.
- **Compiler form:** Pre-compile arg = string label (next handler or re-raise); post-compile arg = pc.

**Example:** In except block: type(s) on stack; pop and match; jump if no match.

| index | opcode          | arg | arg view | comment |
|------:|-----------------|-----|----------|---------|
| 3     | CONST           | 0   | 0 (typ)  | stack: [..., typ] |
| 4     | CHECK_EXCEPTION | 7   | 7 (-> 7) | pop typ; if exc not match -> pc=7 |
| 5     | POP             | 1   | 1        | consume exc |
| 6     | RETURN          | 1   | 1        | handle |
| 7     | LABEL           | --   | --        | re-raise or next handler |

## 9.1 Switch dispatch

Switch dispatch opcodes implement multi-way **identity-or-equality** branching. A typical
**checks** region runs comparisons in order: each comparison **falls through**
when it does not match so the next check can run without re-pushing the
scrutinee. A **match** jumps into a shared **body** (or the same body for
several keys).

**`KOS_OP_SWITCH_EQ`** compares two stack operands, discriminant **`left`**
below key **`right`** (TOS). On **match**, both are popped and the PC is set
to **`arg`** (the body). On **mismatch**, only **`right`** is popped; **`left`**
remains so the following code can push another key and use another
**`SWITCH_EQ`**.

The **single-slot** variants (**`SWITCH_EQ_CONST`**, **`SWITCH_EQ_VAR`**, ...)
compare the scrutinee **`s`** at TOS to an RHS from the extra row. On
**match**, **`s`** is popped and the PC jumps to **`a0`**. On **mismatch**,
the stack is **unchanged** and execution falls through.

See [compile/switch](../specials/compile-switch.md) for the surface form and
[Switch dispatch (section 9.1)](#91-switch-dispatch) for the opcode family.

### KOS_OP_SWITCH_EQ

- **Style**: LABEL. Arg = target pc (post-compile).
- **Stack**: `[..., left, right]` (right = TOS). Matches when `left` and
  `right` are the same object or `left == right` is true
  (`PyObject_RichCompareBool` with `Py_EQ`); an identical pair matches without
  calling `__eq__`. [CHECK_COMPARE](#kos_op_check_compare) EQ asks the type even for an
  identical pair, so a NaN fails there and matches here.
  - **Match**: pop both operands; `pc = arg`. Stack after: `[...]`.
  - **Mismatch**: pop `right` only; fall through. Stack after: `[..., left]`.
- **Invariants**: Jump target is the matching body. After a mismatch, bytecode
  must leave `left` correct for the next comparison (push a new `right`) or
  reach cleanup that drops `left`.
- **Compiler form:** Pre-compile arg = string label (body); post-compile arg =
  pc.

**Example:** Two-arm chain then else. Pool: 0 = 10, 1 = 1, 2 = 10, 3 = 2, 4 =
0. Discriminant 10: first key 1 (mismatch -- pop key, fall through with `[10]`);
second key 10 (match -- pop both, jump to body); body returns 2. Else drops
disc and returns 0. Covered by `tests/lib/opcode/test_switch.py`.

| index | opcode    | arg | arg view  | comment |
|------:|-----------|-----|-----------|---------|
| 0     | CONST     | 0   | 0 (10)    | stack: [..., 10] |
| 1     | CONST     | 1   | 1 (1)     | stack: [..., 10, 1] |
| 2     | SWITCH_EQ | 8   | 8 (-> 8)  | match -> pop2, pc=8 (body); mismatch -> [..., 10], next insn |
| 3     | CONST     | 2   | 2 (10)    | stack: [..., 10, 10] |
| 4     | SWITCH_EQ | 8   | 8 (-> 8)  | match -> body; mismatch -> [..., 10], next insn |
| 5     | POP       | 1   | 1         | else: drop discriminant |
| 6     | CONST     | 4   | 4 (0)     | else result |
| 7     | RETURN    | 1   | 1         | |
| 8     | LABEL     | --   | --         | body |
| 9     | CONST     | 3   | 3 (2)     | stack: [..., 2] |
| 10    | RETURN    | 1   | 1         | |

### KOS_OP_SWITCH_EQ_CONST

- **Style**: EXTRA. Schema `(LABEL, CONST, NOARG, NOARG)`.
- **Stack**: `[..., s]`. Match when `s` is `cpool[a1]` or `s == cpool[a1]`, with `Py_EQ`.
  - **Match**: pop `s`; `pc = a0`.
  - **Mismatch**: fall through; stack unchanged (`s` still TOS).
- **Compiler form:** tuple arg `("label", const_value, 0, 0)` pre-compile;
  post-compile row stores pc in `a0` and constant index in `a1`.
- **Implementation**: `koskari/lib/opcode/SWITCH_EQ_CONST.c`

### KOS_OP_SWITCH_EQ_VAR

- **Style**: EXTRA. Schema `(LABEL, VAR, NOARG, NOARG)`.
- **Stack**: `[..., s]`. Read `rhs` from the frame namespace by the name in `a1`,
  then match when `s` is `rhs` or `s == rhs`.
  - **Match**: pop `s`; `pc = a0`.
  - **Mismatch**: fall through; stack unchanged.
- **Compiler form:** tuple arg `("label", "name", 0, 0)` pre-compile.

### KOS_OP_SWITCH_EQ_FAST

- **Style**: EXTRA. Schema `(LABEL, FASTVAR, NOARG, NOARG)`.
- **Stack**: `[..., s]`. Resolve `rhs = KosNamespace_FastDeref(ns, a1)`, then
  match when `s` is `rhs` or `s == rhs`, with `Py_EQ`.
  - **Match**: pop `s`; `pc = a0`.
  - **Mismatch**: fall through; stack unchanged.
- **Compiler form:** tuple arg `("label", slot_index, 0, 0)` pre-compile.
- **Implementation**: `koskari/lib/opcode/SWITCH_EQ_FAST.c`

### KOS_OP_SWITCH_EQ_VAR_ATTR

- **Style**: EXTRA. Schema `(LABEL, VAR, CONST, NOARG)`.
- **Stack**: `[..., s]`. Read `obj` from the frame namespace by the name in `a1`, then
  `rhs = getattr(obj, attr_name)` with the name in `a2`, then match when `s` is `rhs` or `s == rhs`.
  - **Match**: pop `s`; `pc = a0`.
  - **Mismatch**: fall through; stack unchanged.
- **Compiler form:** tuple arg `("label", "obj_name", "attr_name", 0)` pre-compile.

### KOS_OP_SWITCH_EQ_FAST_ATTR

- **Style**: EXTRA. Schema `(LABEL, FASTVAR, CONST, NOARG)`.
- **Stack**: `[..., s]`. Resolve `obj = KosNamespace_FastDeref(ns, a1)`,
  `rhs = getattr(obj, attr_name)` from `a2`, then match when `s` is `rhs` or `s == rhs`.
  - **Match**: pop `s`; `pc = a0`.
  - **Mismatch**: fall through; stack unchanged.
- **Compiler form:** tuple arg `("label", slot_index, "attr_name", 0)` pre-compile.
- **Implementation**: `koskari/lib/opcode/SWITCH_EQ_FAST_ATTR.c`


## 9.2 Pattern matching

The `MATCH_*` instructions carry a compiled pattern. A `match` arm, a
special form's pattern and a `syntax-rules` rule compile to a run of
these instructions in the code that uses them; no pattern object is
interpreted at run time.

- **Walk entries.** A walk keeps its state on the operand stack as
  entries of three kinds. A **cursor** walks one subject -- a pair
  spine, a tuple, list or other sequence, or a single value as a
  one-element subject -- and has a position and an end. A **mark** has
  the position of the cursor it was made at, which never moves, and a
  fixed count of slots, each empty or holding one capture; it is the
  record of one capture scope. An **items list** collects a repeat's
  passes; the repeat pushes it empty with `BUILD_LIST 0`.
- **Depths.** A RAW field names a walk entry by its depth: the count of
  entries above it as the instruction finds the stack, 0 the top before
  the instruction pops or pushes. A depth that names a cursor or mark
  where the entry is of another kind raises `SystemError`.
- **Copy on write.** An instruction that moves a cursor, writes a mark's
  slots or appends to an items list writes the entry it names. When
  another stack entry holds the same object -- after a `DUP`, or in a
  continuation captured during the walk -- the instruction stores a copy
  at its entry and writes the copy, so a resumed continuation sees the
  cursors, marks and items it captured.
- **Misses.** An instruction with a LABEL field jumps there on a miss.
- **Tests.** `MATCH_LITERAL`, `MATCH_CORE`, `MATCH_TYPE_*`,
  `MATCH_CLASS_*`, `MATCH_SAME` and `MATCH_MAPPING` read TOS and leave
  it on a hit and on a miss; the code after the test removes the value.
- **Boxing.** An element pulled from a pair spine is a `Datum` carrying
  its cell's position; an element of a tuple, list or single value is
  the item itself. Every test reads through `Datum`.
- **Twins.** A type, class or mapping key the pattern names is a
  variable of the enclosing code. An instruction that reads one has a
  `_VAR` form, with a VAR field read by name, and a `_FAST` form, with
  a FASTVAR field read by fast index; `convert_fast` lowers the one to
  the other as for every `VAR` instruction. An instruction with no such
  field has one form under its bare name.
- **Calls.** A predicate or extractor is a variable called with
  `CALL_VAR` from the same frame, and flattens as any Koskari call does;
  a predicate's result is tested with `JUMP_FALSE`.
- **Core types.** Seven type names are not variables: `pair`, `symbol`,
  `keyword`, `int`, `str`, `tuple` and `list` in a pattern lower to
  `MATCH_CORE`, which carries the type as an ordinal in a RAW field. A
  binding of one of those names in scope is never read by a pattern.
- **Widths.** Width and count fields are 16 bits; in `MATCH_FITS` a
  maximum of `0xFFFF` is unbounded.

| Instruction | Reference |
|---|---|
| MATCH_DESCEND | [kos-op-match-descend](opcodes/kos-op-match-descend.md) |
| MATCH_SINGLE | [kos-op-match-single](opcodes/kos-op-match-single.md) |
| MATCH_FITS | [kos-op-match-fits](opcodes/kos-op-match-fits.md) |
| MATCH_NEXT | [kos-op-match-next](opcodes/kos-op-match-next.md) |
| MATCH_BOUND | [kos-op-match-bound](opcodes/kos-op-match-bound.md) |
| MATCH_UNBOUND | [kos-op-match-unbound](opcodes/kos-op-match-unbound.md) |
| MATCH_SYNC | [kos-op-match-sync](opcodes/kos-op-match-sync.md) |
| MATCH_TAIL | [kos-op-match-tail](opcodes/kos-op-match-tail.md) |
| MATCH_SCOPE | [kos-op-match-scope](opcodes/kos-op-match-scope.md) |
| MATCH_SCOPE_NEXT | [kos-op-match-scope-next](opcodes/kos-op-match-scope-next.md) |
| MATCH_RESET | [kos-op-match-reset](opcodes/kos-op-match-reset.md) |
| MATCH_AT | [kos-op-match-at](opcodes/kos-op-match-at.md) |
| MATCH_SPAN | [kos-op-match-span](opcodes/kos-op-match-span.md) |
| MATCH_STORE | [kos-op-match-store](opcodes/kos-op-match-store.md) |
| MATCH_CLEAR | [kos-op-match-clear](opcodes/kos-op-match-clear.md) |
| MATCH_CAPTURES | [kos-op-match-captures](opcodes/kos-op-match-captures.md) |
| MATCH_PASS | [kos-op-match-pass](opcodes/kos-op-match-pass.md) |
| MATCH_LOAD | [kos-op-match-load](opcodes/kos-op-match-load.md) |
| MATCH_APPEND | [kos-op-match-append](opcodes/kos-op-match-append.md) |
| MATCH_MIN | [kos-op-match-min](opcodes/kos-op-match-min.md) |
| MATCH_COLUMNS | [kos-op-match-columns](opcodes/kos-op-match-columns.md) |
| MATCH_LITERAL | [kos-op-match-literal](opcodes/kos-op-match-literal.md) |
| MATCH_CORE | [kos-op-match-core](opcodes/kos-op-match-core.md) |
| MATCH_TYPE_VAR / MATCH_TYPE_FAST | [kos-op-match-type](opcodes/kos-op-match-type.md) |
| MATCH_CLASS_VAR / MATCH_CLASS_FAST | [kos-op-match-class](opcodes/kos-op-match-class.md) |
| MATCH_SAME | [kos-op-match-same](opcodes/kos-op-match-same.md) |
| MATCH_FIELD_POS_VAR / MATCH_FIELD_POS_FAST | [kos-op-match-field-pos](opcodes/kos-op-match-field-pos.md) |
| MATCH_FIELD_KW | [kos-op-match-field-kw](opcodes/kos-op-match-field-kw.md) |
| MATCH_MAPPING | [kos-op-match-mapping](opcodes/kos-op-match-mapping.md) |
| MATCH_KEY | [kos-op-match-key](opcodes/kos-op-match-key.md) |
| MATCH_KEY_VAR / MATCH_KEY_FAST | [kos-op-match-key-var](opcodes/kos-op-match-key-var.md) |


## 10. Introspection

### KOS_OP_GLOBALS

- **Style**: NOARG.
- **Stack**: `[...] -> [..., globals_dict]`.
- **Invariants**: Frame has globals namespace.

**Example:** Push frame's globals dict.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | GLOBALS| 0   | 0        | stack: [..., globals_dict] |
| 1     | RETURN | 1   | 1        | |

---

### KOS_OP_LOCALS

- **Style**: NOARG.
- **Stack**: `[...] -> [..., locals_dict]`.
- **Invariants**: Frame has locals namespace.

**Example:** Push frame's locals dict.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | LOCALS | 0   | 0        | stack: [..., locals_dict] |
| 1     | RETURN | 1   | 1        | |

---

### 10.1 Module import

The three import opcodes each implement one Koskari import surface form. They
are **self-contained and stack-neutral**: each carries the module name and
relative-import level in its EXTRA row, imports its own module, and stores its
own bindings into the current frame namespace. None of them touch the operand
stack, chain a module between instructions, or need a compensating POP. See
`koskari/compile/specials/imports.py` (`special_import`, `special_import_from`,
`special_import_all_from`) for the compiler forms.

For a relative import (`level > 0`), the resolver builds a temporary globals
dict holding `__name__` / `__package__` from the frame's globals namespace so
`PyImport_ImportModuleLevelObject` can resolve the anchor package. A leaf
import passes the sentinel `_import_fromlist = ("*",)` to return the deepest
submodule; a root import passes a NULL fromlist to return the root package
while still importing the full dotted chain (matching a plain Python
`import foo.bar`).

### KOS_OP_IMPORT
- **Style**: EXTRA (`CONST`, `COUNT`, `COUNT`, `CONST`). Row: `a0` = cpool
  module name str (or None for a bare relative import), `a1` = level (leading
  dots; 0 = absolute), `a2` = leaf flag (0 = bind root, 1 = bind leaf), `a3` =
  cpool binding name str.
- **Stack**: no effect.
- **Semantics**: Import the module (root or leaf per the flag) and store it into
  the current frame namespace under the binding name. With the leaf flag clear
  the root package is bound while the full dotted chain is still imported --
  plain `(import foo.bar)` binds `foo`; with it set the deepest submodule is
  bound -- the `(import [foo.bar as: fbar])` member form. Relative imports are
  always leaf-style. Self-contained: no stack effect, no separate VAR_BIND.
- **Invariants**: `a0` is a str or None; `a3` is a str.
- **Implementation**: `koskari/lib/opcode/IMPORT.c`

**Example:** `(import os.path)` -- import the `os.path` chain, bind the root
`os`. Extra row: `("os.path", 0, 0, "os")`.

| index | opcode | arg | arg view                  | comment |
|------:|--------|-----|---------------------------|---------|
| 0     | IMPORT | 0   | 0 ("os.path", 0, 0, "os") | bind "os"; stack: [...] |
| 1     | CONST  | 1   | 1 (None)                  | statement value; stack: [..., None] |
| 2     | RETURN | 1   | 1                         | |

Member form `(import [os.path as: p])` uses row `("os.path", 0, 1, "p")` to
bind the leaf; relative `(import .foo)` uses `("foo", 1, 1, "foo")`.

---

### KOS_OP_IMPORT_FROM
- **Style**: EXTRA (`CONST`, `COUNT`, `CONST`, `CONST`). Row: `a0` = cpool
  module name str (or None), `a1` = level, `a2` = cpool tuple of attribute
  names, `a3` = cpool tuple of binding names (parallel to the attribute names;
  the same tuple object when there are no renames).
- **Stack**: no effect.
- **Semantics**: Import the leaf module, then for each attribute name `getattr`
  it and store the value into the current frame namespace under the
  corresponding binding name. Self-contained: no module on the stack, no
  compensating POP.
- **Submodule fallback**: when the `getattr` raises `AttributeError`, import
  `"{module.__name__}.{attr}"` and retry -- matching CPython's
  `from pkg import sub` behavior for submodules the package `__init__` does not
  import. Other exceptions propagate.
- **Invariants**: `a2` and `a3` are equal-length tuples of str.
- **Implementation**: `koskari/lib/opcode/IMPORT_FROM.c`

**Example:** `(import-from os path [sep as: s])` -- import `os`, bind `path` and
bind `sep` as `s`. Extra row: `("os", 0, ("path", "sep"), ("path", "s"))`.

| index | opcode      | arg | arg view                       | comment |
|------:|-------------|-----|--------------------------------|---------|
| 0     | IMPORT_FROM | 0   | 0 ("os", 0, ("path","sep"), ...) | bind "path", "s"; stack: [...] |
| 1     | CONST       | 1   | 1 (None)                       | statement value |
| 2     | RETURN      | 1   | 1                              | |

---

### KOS_OP_IMPORT_STAR
- **Style**: EXTRA (`CONST`, `COUNT`, `NOARG`, `NOARG`). Row: `a0` = cpool
  module name str (or None), `a1` = level.
- **Stack**: no effect.
- **Semantics**: Import the leaf module and bind its public names into the
  current frame namespace, matching Python's `from MODULE import *`. Name
  selection follows CPython's `import_all_from`: when the module defines
  `__all__` (a sequence of str; a non-str entry is a `TypeError`) exactly those
  names are bound; otherwise every `dir(module)` name that does not start with
  an underscore. Existing bindings are overwritten.
- **Compiler restriction**: the `import-all-from` special emits this opcode
  only at module top level; it is a compile error inside a function, lambda,
  `let`, or class body.
- **Invariants**: `a0` is a str or None.
- **Implementation**: `koskari/lib/opcode/IMPORT_STAR.c`

**Example:** `(import-all-from os.path)` -- bind `os.path`'s public names. Extra
row: `("os.path", 0, 0, 0)`.

| index | opcode      | arg | arg view            | comment |
|------:|-------------|-----|---------------------|---------|
| 0     | IMPORT_STAR | 0   | 0 ("os.path", 0, ...) | bind public names; stack: [...] |
| 1     | CONST       | 1   | 1 (None)            | statement value |
| 2     | RETURN      | 1   | 1                   | |

### KOS_OP_IMPORT_USE
- **Style**: EXTRA (`CONST`, `COUNT`, `CONST`, `CONST`). Row: `a0` = cpool
  module name str (or None), `a1` = level, `a2` = cpool tuple of feature stems,
  `a3` = cpool tuple of bind stems (parallel to the feature stems; the same
  tuple object when there are no renames).
- **Stack**: no effect.
- **Semantics**: Import the leaf module, then for each feature stem probe its
  candidate set -- the stem itself, `compile/{stem}`, and `layout/{stem}` -- and
  store each hit into the current frame namespace under the parallel bind stem
  with the same facet prefix applied, so a rename substitutes the whole family.
  A stem already carrying a facet prefix is literal: one candidate, bound as
  written. Self-contained: no module on the stack, no compensating POP.
- **Miss handling**: a per-facet miss is tolerated. The bare candidate uses
  the same member fetch as `IMPORT_FROM` (submodule fallback included); its
  crafted missing-member `ImportError` counts as a miss, while a
  `ModuleNotFoundError` from inside a submodule's own import propagates. A
  stem with zero hits raises `ImportError` naming the spellings tried.
- **Compiler restriction**: the `use` special emits this opcode only at
  module top level.
- **Invariants**: `a2` and `a3` are equal-length tuples of str.
- **Implementation**: `koskari/lib/opcode/IMPORT_USE.c`

**Example:** `(use tacotruck tacos)` where the module defines `tacos` and
`compile/tacos` -- bind both. Extra row: `("tacotruck", 0, ("tacos",),
("tacos",))`.

| index | opcode     | arg | arg view                          | comment |
|------:|------------|-----|-----------------------------------|---------|
| 0     | IMPORT_USE | 0   | 0 ("tacotruck", 0, ("tacos",), ...) | bind "tacos", "compile/tacos"; stack: [...] |
| 1     | CONST      | 1   | 1 (None)                          | statement value |
| 2     | RETURN     | 1   | 1                                 | |

---

**Note:** There is **no** `KOS_OP_CAPTURE_FRAME` (or similar) in `koskari/lib/opcodes.incl`; a future opcode would be added there first, then documented here.

## 11. Exception handling

### KOS_OP_EXCEPTION

- **Style**: NOARG.
- **Stack**: `[...] -> [..., exception]`. Push current exception (in handler).
- **Invariants**: Used when an exception is being handled.

**Example:** In except block: load current exception.

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 2     | EXCEPTION| 0   | 0        | stack: [..., exc] |
| 3     | VAR_STORE| 0   | 0 (e)    | store exc in name "e" |

---

### KOS_OP_EXCEPTION_CLEAR

- **Style**: NOARG.
- **Stack**: No change. Clear current exception state.
- **Invariants**: --
**Example:** After handling, clear exception before continuing.

| index | opcode         | arg | arg view | comment |
|------:|----------------|-----|----------|---------|
| 4     | EXCEPTION_CLEAR| 0   | 0        | clear exception state |
| 5     | RETURN         | 1   | 1        | |

---

### KOS_OP_EXCEPTION_MATCH

- **Style**: NOARG.
- **Stack**: Pop TOS (exception type or tuple of types). Push True if current exception matches that value, else False.
- **Invariants**: Used in handler. Compiler typically emits BUILD_ARGS before this to pack multiple types into one tuple.

**Example:** Match current exception against one type; push True/False.

| index | opcode          | arg | arg view | comment |
|------:|-----------------|-----|----------|---------|
| 2     | CONST           | 0   | 0 (ValueError) | stack: [..., typ] |
| 3     | EXCEPTION_MATCH | 0   | 0        | stack: [..., result] |
| 4     | JUMP_FALSE      | 7   | 7 (-> 7) | if no match, next handler |

## 12. Variables by name

### KOS_OP_VAR

- **Style**: VAR. Arg = constant pool index of variable name (post-compile).
- **Stack**: `[...] -> [..., value]`. Load value bound to name in frame namespace.
- **Invariants**: Name in constant pool.
- **Compiler form:** Pre-compile arg = name (string/symbol); finalize stores name in pool and rewrites arg to pool index. May be lowered to FAST with slot index for locals.

**Example:** Load variable "x". Pool: 0 = "x".

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | VAR    | 0   | 0 ('x')  | stack: [..., value] |
| 1     | RETURN | 1   | 1        | |

---

### KOS_OP_VAR2

- **Style**: EXTRA (VAR, VAR, NOARG, NOARG). Row = two constant pool name
  indices (post-compile).
- **Stack**: `[...] -> [..., a, b]`. Load both names from the frame namespace
  and push in row order. Both names resolve before either push, so a missing
  name errors with nothing pushed.
- **Invariants**: Names in constant pool.
- **Compiler form:** Never emitted by specials. Produced only by
  **`ByteCodeProcessor.pack_runs`** (the trailing pack phase), which combines
  a run of two consecutive bare **VAR** loads (quads first, then one triple
  or one pair). **`convert_fast`** lowers to **FAST2** when both names have
  fast slots (all-or-nothing; a mixed-eligibility row stays VAR2).
- **Implementation**: `koskari/lib/opcode/VAR2.c` (cold handler)
- **Covered by**: `tests/compile/test_pack_var_runs.py`,
  `tests/reproducers/test_var_pack.py`

**Example:** Load "x" then "y". Pool: 0 = "x", 1 = "y".

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | VAR2   | 0   | (0 ('x'), 1 ('y'), 0, 0) | stack: [..., x, y] |
| 1     | RETURN | 1   | 1        | |

### KOS_OP_VAR3

- **Style**: EXTRA (VAR, VAR, VAR, NOARG). Row = three constant pool name indices
  (post-compile).
- **Stack**: `[...] -> [..., a, b, c]`. Load the three names from the frame namespace and
  push in row order. Every name resolves before any push, so a missing name
  errors with nothing pushed.
- **Invariants**: Names in constant pool.
- **Compiler form:** As VAR2: produced only by **`ByteCodeProcessor.pack_runs`**
  from a run of three consecutive bare VAR loads (quads first, then one triple
  or one pair). **`convert_fast`** lowers to FAST3 when all three names have fast
  slots (all-or-nothing; a mixed-eligibility row stays VAR3).
- **Implementation**: `koskari/lib/opcode/VAR3.c` (cold handler)
- **Covered by**: `tests/compile/test_pack_var_runs.py`,
  `tests/reproducers/test_var_pack.py`

**Example:** Load "a", "b", "c". Pool: 0-2 = names.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | VAR3   | 0   | (0 ('a'), 1 ('b'), 2 ('c'), 0) | stack: [..., a, b, c] |
| 1     | RETURN | 1   | 1        | |

### KOS_OP_VAR4

- **Style**: EXTRA (VAR, VAR, VAR, VAR). Row = four constant pool name
  indices (post-compile).
- **Stack**: `[...] -> [..., a, b, c, d]`. Load all four names from the frame
  namespace and push in row order. All names resolve before any push, so a
  missing name errors with nothing pushed.
- **Invariants**: Names in constant pool.
- **Compiler form:** As **VAR2**: produced only by **`pack_runs`** from a run
  of four (or more, greedily) consecutive bare **VAR** loads.
  **`convert_fast`** lowers to **FAST4** when all four names have fast slots.
- **Implementation**: `koskari/lib/opcode/VAR4.c` (cold handler)
- **Covered by**: `tests/compile/test_pack_var_runs.py`,
  `tests/reproducers/test_var_pack.py`

**Example:** Load "a", "b", "c", "d". Pool: 0-3 = names.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | VAR4   | 0   | (0 ('a'), 1 ('b'), 2 ('c'), 3 ('d')) | stack: [..., a, b, c, d] |
| 1     | RETURN | 1   | 1        | |

### KOS_OP_VAR_BIND

- **Style**: VAR. Arg = constant pool index of name (post-compile).
- **Stack**: `[..., ref] -> [...]`. **BIND**: use this reference to resolve the
  given name in the frame namespace. See section 1.9.
- **Invariants**: Name in constant pool.
- **Compiler form:** Pre-compile arg = name (string/symbol); post-compile arg = pool index.

**Example:** Bind name "n" to the reference at TOS. Pool: 0 = "n".

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 0     | CONST    | 0   | 0 (42)   | stack: [..., ref] |
| 1     | VAR_BIND | 1   | 1 ('n')  | BIND: name "n" resolves to this ref; stack: [...] |

---

### KOS_OP_VAR_CLEAR

- **Style**: VAR. Arg = constant pool index of name (post-compile).
- **Stack**: No stack effect. Delete name from namespace.
- **Invariants**: Name in constant pool.
- **Compiler form:** Pre-compile arg = name; post-compile arg = pool index.

**Example:** Delete name "tmp". Pool: 0 = "tmp".

| index | opcode    | arg | arg view | comment |
|------:|-----------|-----|----------|---------|
| 2     | VAR_CLEAR | 0   | 0 ('tmp')| delete "tmp" from namespace |

---

### KOS_OP_VAR_CLEAR2

- **Style**: EXTRA (VAR, VAR, NOARG, NOARG). Row = 2 constant pool name indices (post-compile).
- **Stack**: No stack effect. Clear the 2 names in row order, as
  2 `VAR_CLEAR` would; an already empty one stays empty.
- **Compiler form:** Never emitted by specials. Produced only by
  `ByteCodeProcessor.pack_runs`, which combines a run of consecutive
  `VAR_CLEAR` instructions (quads first, then one triple or one pair).

### KOS_OP_VAR_CLEAR3

- **Style**: EXTRA (VAR, VAR, VAR, NOARG). Row = 3 constant pool name indices (post-compile).
- **Stack**: No stack effect. Clear the 3 names in row order, as
  3 `VAR_CLEAR` would; an already empty one stays empty.
- **Compiler form:** Never emitted by specials. Produced only by
  `ByteCodeProcessor.pack_runs`, which combines a run of consecutive
  `VAR_CLEAR` instructions (quads first, then one triple or one pair).

### KOS_OP_VAR_CLEAR4

- **Style**: EXTRA (VAR, VAR, VAR, VAR). Row = 4 constant pool name indices (post-compile).
- **Stack**: No stack effect. Clear the 4 names in row order, as
  4 `VAR_CLEAR` would; an already empty one stays empty.
- **Compiler form:** Never emitted by specials. Produced only by
  `ByteCodeProcessor.pack_runs`, which combines a run of consecutive
  `VAR_CLEAR` instructions (quads first, then one triple or one pair).

### KOS_OP_VAR_TAKE
- **Style**: VAR. Arg = constant pool index of name (post-compile).
- **Stack**: `[...] -> [..., value]`. Deref name via `frame_ns_take`, clear the
  binding, push the value. **Errors with NameError if the binding is empty**
  (contrast **VAR_CLEAR**, which is idempotent on an already-empty declared
  slot).
- **Invariants**: Name in constant pool; binding must hold a value.
- **Compiler form:** Pre-compile arg = name; post-compile arg = pool index.
  Emitted by [compile/takeq](../specials/compile-takeq.md). May lower to
  **FAST_TAKE** via fast-local lowering.

**Example:** Take and clear name "tmp". Pool: 0 = "tmp".

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 2     | VAR_TAKE | 0   | 0 ('tmp')| take "tmp"; stack: [..., value] |

---

### KOS_OP_VAR_REF

- **Style**: VAR. Arg = constant pool index of name (post-compile).
- **Stack**: `[...] -> [..., ref]`. Push a reference to the named variable (e.g. for closure).
- **Invariants**: Name in constant pool.
- **Compiler form:** Pre-compile arg = name; post-compile arg = pool index.

**Example:** Push reference for "cell". Pool: 0 = "cell".

| index | opcode | arg | arg view  | comment |
|------:|--------|-----|-----------|---------|
| 0     | VAR_REF| 0   | 0 ('cell')| stack: [..., ref] |

---

### KOS_OP_VAR_STORE

- **Style**: VAR. Arg = constant pool index of name (post-compile).
- **Stack**: `[..., value] -> [...]`. **STORE**: store this value in the given
  name (through the reference that already resolves it). See section 1.9.
- **Invariants**: Name must already be bound.
- **Compiler form:** Pre-compile arg = name; post-compile arg = pool index.

**Example:** Store value 10 into name "x". Pool: 0 = "x".

| index | opcode    | arg | arg view | comment |
|------:|-----------|-----|----------|---------|
| 0     | CONST     | 0   | 0 (10)   | stack: [..., 10] |
| 1     | VAR_STORE | 1   | 1 ('x')  | STORE: write 10 into name "x"; stack: [...] |

---

### KOS_OP_VAR_STORE_CONST
- **Style**: EXTRA (VAR, CONST, NOARG, NOARG). Arg = extra-table index (post-compile).
- **Stack**: `[...] -> [...]` (no stack effect). **STORE**: store the constant value into the given name via `frame_ns_store`. Same semantics as **VAR_STORE**, but the value is taken from the constant pool (extra row field `a1`), not from TOS.
- **Peephole**: **`ByteCodeProcessor`** fuses **`KOS_OP_CONST`** `value` immediately followed by **`KOS_OP_VAR_STORE`** `name` into this opcode with pre-compile arg **`(name, value, None, None)`**; the **`CONST`** is removed from the stream. See section 1.8.
- **Lowering**: **`Compiler.convert_fast`** (when the name has a fixed fast slot in FUNCTION mode) may replace **`VAR_STORE_CONST`** with **`FAST_STORE_CONST`**, rewriting the VAR field to a slot index and preserving the constant operand.
- **Invariants**: Name must already be bound. Name and value are internalized into the extra row at finalize.
- **Compiler form:** Pre-compile arg = 4-tuple `(name, value, None, None)`; post-compile arg = extra index.
- **Implementation**: `koskari/lib/opcode/VAR_STORE_CONST.c`
- **Covered by**: `tests/compile/test_compress.py`, `tests/compile/test_convert_fast.py`, `tests/lib/opcode/test_store_const.py`

**Example:** Store constant 42 into name `"x"` without pushing 42. Pre-compile stream after `ByteCodeProcessor`: one **`VAR_STORE_CONST`**; pool/extra filled at finalize. Post-compile arg view uses extra index **0** (illustrative).

| index | opcode          | arg | arg view              | comment |
|------:|-----------------|-----|-----------------------|---------|
| 0     | VAR_STORE_CONST | 0   | 0 -> ('x', 42, ...)      | STORE 42 into "x"; stack unchanged |
| 1     | RETURN          | 1   | 1                     | |

**Example (fusion source):** `(setq x 42)` emits value, **`VAR_STORE`**, **`CONST`** `None`; `ByteCodeProcessor` turns **`CONST`** `42` + **`VAR_STORE`** `x` into **`VAR_STORE_CONST`**.

### KOS_OP_VAR_INPLACE

- **Style**: EXTRA (OPERAND, VAR). Row a0 = the bare KosOperand
  selector; a1 = constant pool index of the variable name.
- **Stack**: `[..., rhs] -> [...]`. Augmented assignment through the frame
  namespace: obj = deref(name); obj OP= rhs; store back. No value pushed.
- **Compiler form:** Emitted by **`compile/inplace`** when the target is a symbol. May lower to **FAST_INPLACE**; a single constant operand squashes into **VAR_INPLACE_CONST**.
- **Implementation**: `koskari/lib/opcode/VAR_INPLACE.c`

### KOS_OP_VAR_INPLACE_CONST

- **Style**: EXTRA (OPERAND, VAR, CONST). Row a0 = bare KosOperand
  selector (the count is fixed at 1 by the shape), a1 = constant pool
  index of the name, a2 = constant pool index of the operand.
- **Stack**: No stack effect. Augmented assignment with a constant rhs
  through the frame namespace, completely stack-free: obj = deref(name);
  obj OP= const; store back.
- **Peephole**: the compressed form of **CONST** followed by **VAR_INPLACE** at a count of one, as VAR_STORE_CONST is of CONST and VAR_STORE. May lower to **FAST_INPLACE_CONST**.
- **Implementation**: `koskari/lib/opcode/VAR_INPLACE_CONST.c`

### 12.1 Attribute access on variables (VAR_ATTR_*)

These opcodes perform attribute get, set, clear, take, or reference on an object fetched from the frame namespace by name. The object is not on the stack; it is read from the frame namespace by name. Each uses EXTRA schema: VAR (obj name), CONST (attr name).

- **Compiler form:** Emitted by attr/del-attr/take-attr/set-attr specials when the object expression is a symbol (variable reference). May be lowered to FAST_ATTR_* when the name has a fixed slot.

### KOS_OP_VAR_ATTR
- **Style**: EXTRA (VAR, CONST, NOARG, NOARG).
- **Stack**: `[...] -> [..., value]`. Get attribute on object held in namespace by name. Equivalent to ATTR but the object is loaded from the frame namespace, not from TOS.
- **Implementation**: `koskari/lib/opcode/VAR_ATTR.c`

### KOS_OP_VAR_ATTR_STORE
- **Style**: EXTRA (VAR, CONST, NOARG, NOARG).
- **Stack**: `[..., value] -> [...]`. Set attribute. Equivalent to ATTR_STORE with object from namespace.
- **Implementation**: `koskari/lib/opcode/VAR_ATTR_STORE.c`

### KOS_OP_VAR_ATTR_CLEAR
- **Style**: EXTRA (VAR, CONST, NOARG, NOARG).
- **Stack**: No stack effect. Delete attribute on object from namespace. Equivalent to ATTR_CLEAR with object from namespace.
- **Implementation**: `koskari/lib/opcode/VAR_ATTR_CLEAR.c`

### KOS_OP_VAR_ATTR_TAKE
- **Style**: EXTRA (VAR, CONST, NOARG, NOARG).
- **Stack**: `[...] -> [..., value]`. Get attribute on object from namespace,
  delete it, push value. Errors if attribute missing (same as **VAR_ATTR** +
  **VAR_ATTR_CLEAR** sequence).
- **Compiler form:** Emitted by [compile/take-attr](../specials/compile-take-attr.md)
  when OBJ is a symbol. May lower to **FAST_ATTR_TAKE**.

### KOS_OP_VAR_ATTR_REF
- **Style**: EXTRA (VAR, CONST, NOARG, NOARG).
- **Stack**: `[...] -> [..., attr_ref]`. Push attribute reference. Equivalent to ATTR_REF with object from namespace.
- **Implementation**: `koskari/lib/opcode/VAR_ATTR_REF.c`

### KOS_OP_VAR_ATTR_INPLACE

- **Style**: EXTRA (OPCOUNT, VAR, CONST, NOARG). Row a0 = operator selector and count, a1 = the object name, a2 = the attribute name.
- **Stack**: `[..., v0 .. v_{n-1}] -> [...]`. Resolve the object by name, reduce obj.attr in place with the count values left to right, store it back; pushes nothing (the special appends CONST None). Cold: the operator switch lives in the shared `kos_inplace_reduce`.
- **Compiler form:** Emitted by **`compile/inplace-attr`** when OBJ is a symbol. May lower to **FAST_ATTR_INPLACE**.
- **Implementation**: `koskari/lib/opcode/VAR_ATTR_INPLACE.c`

### KOS_OP_ITEM_STORE_VAR
- **Style**: VAR (the value's name).
- **Stack**: `[..., obj, key] -> [...]`. Set obj[key] = value with the object on the stack, the key on the stack and the value resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by `ByteCodeProcessor` from `VAR value, ITEM_STORE`. May lower to **ITEM_STORE_FAST**.
- **Implementation**: `koskari/lib/opcode/ITEM_STORE_VAR.c`

### KOS_OP_ITEM_CONST_STORE_VAR
- **Style**: EXTRA (CONST, VAR, NOARG, NOARG).
- **Stack**: `[..., obj] -> [...]`. Set obj[key] = value with the object on the stack, the key a pool constant and the value resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by `ByteCodeProcessor` from `CONST key, VAR value, ITEM_STORE`, or from `VAR value` ahead of the shaped **ITEM_CONST_STORE**. May lower to **ITEM_CONST_STORE_FAST**.
- **Implementation**: `koskari/lib/opcode/ITEM_CONST_STORE_VAR.c`

### KOS_OP_ITEM_VAR
- **Style**: VAR (the key's name).
- **Stack**: `[..., obj] -> [..., value]`. Get obj[key] with the object on the stack and the key resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by `ByteCodeProcessor` from `VAR key, ITEM`. May lower to **ITEM_FAST**.
- **Implementation**: `koskari/lib/opcode/ITEM_VAR.c`

### KOS_OP_ITEM_VAR_CLEAR
- **Style**: VAR (the key's name).
- **Stack**: `[..., obj] -> [...]`. Delete obj[key] with the object on the stack and the key resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by `ByteCodeProcessor` from `VAR key, ITEM_CLEAR`. May lower to **ITEM_FAST_CLEAR**.
- **Implementation**: `koskari/lib/opcode/ITEM_VAR_CLEAR.c`

### KOS_OP_ITEM_VAR_TAKE
- **Style**: VAR (the key's name).
- **Stack**: `[..., obj] -> [..., value]`. Get obj[key], delete it, and push the value with the object on the stack and the key resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by `ByteCodeProcessor` from `VAR key, ITEM_TAKE`. May lower to **ITEM_FAST_TAKE**.
- **Implementation**: `koskari/lib/opcode/ITEM_VAR_TAKE.c`

### KOS_OP_ITEM_VAR_REF
- **Style**: VAR (the key's name).
- **Stack**: `[..., obj] -> [..., value]`. Push an ItemRef over obj[key] with the object on the stack and the key resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by `ByteCodeProcessor` from `VAR key, ITEM_REF`. May lower to **ITEM_FAST_REF**.
- **Implementation**: `koskari/lib/opcode/ITEM_VAR_REF.c`

### KOS_OP_ITEM_VAR_STORE
- **Style**: VAR (the key's name).
- **Stack**: `[..., obj, value] -> [...]`. Set obj[key] = value with the object on the stack, the key resolved by name and the value on the stack.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Shaped by **`compile/set-item`** when OBJ is a symbol or KEY is a feeder and a value is not, so the fold could not reach the named operands. May lower to **ITEM_FAST_STORE**.
- **Implementation**: `koskari/lib/opcode/ITEM_VAR_STORE.c`

### KOS_OP_ITEM_VAR_STORE_CONST
- **Style**: EXTRA (VAR, CONST, NOARG, NOARG).
- **Stack**: `[..., obj] -> [...]`. Set obj[key] = value with the object on the stack, the key resolved by name and the value a pool constant.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by `ByteCodeProcessor` from `VAR key, CONST value, ITEM_STORE`, or from `CONST value` ahead of the shaped **ITEM_VAR_STORE**. May lower to **ITEM_FAST_STORE_CONST**.
- **Implementation**: `koskari/lib/opcode/ITEM_VAR_STORE_CONST.c`

### KOS_OP_ITEM_VAR_STORE_VAR
- **Style**: EXTRA (VAR, VAR, NOARG, NOARG).
- **Stack**: `[..., obj] -> [...]`. Set obj[key] = value with the object on the stack, the key resolved by name and the value resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by `ByteCodeProcessor` from `VAR key, VAR value, ITEM_STORE`, or from `VAR value` ahead of the shaped **ITEM_VAR_STORE**. May lower to **ITEM_FAST_STORE_FAST**.
- **Implementation**: `koskari/lib/opcode/ITEM_VAR_STORE_VAR.c`

### KOS_OP_ITEM_VAR_INPLACE
- **Style**: EXTRA (OPCOUNT, VAR, NOARG, NOARG).
- **Stack**: `[..., obj, v0 .. v_{n-1}] -> [...]`. Reduce obj[key] in place with the count values left to right and store it back with the object on the stack and the key resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Shaped by **`compile/inplace-item`** when OBJ is a symbol or KEY is a feeder and a value is not, so the fold could not reach the named operands. May lower to **ITEM_FAST_INPLACE**.
- **Implementation**: `koskari/lib/opcode/ITEM_VAR_INPLACE.c`

### KOS_OP_ITEM_VAR_INPLACE_CONST
- **Style**: EXTRA (OPERAND, VAR, CONST, NOARG).
- **Stack**: `[..., obj] -> [...]`. Reduce obj[key] in place with the pool constant and store it back with the object on the stack and the key resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by `ByteCodeProcessor` from `VAR key, CONST rhs, ITEM_INPLACE` at a count of one, or from `CONST rhs` ahead of the shaped **ITEM_VAR_INPLACE**. May lower to **ITEM_FAST_INPLACE_CONST**.
- **Implementation**: `koskari/lib/opcode/ITEM_VAR_INPLACE_CONST.c`

### KOS_OP_VAR_ITEM
- **Style**: VAR (the object's name).
- **Stack**: `[..., key] -> [..., value]`. Get obj[key] with the object resolved by name and the key on the stack.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Shaped by **`compile/item`** when OBJ is a symbol and KEY is not a feeder, since no fold reaches an object load behind a computed key. May lower to **FAST_ITEM**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM.c`

### KOS_OP_VAR_ITEM_CLEAR
- **Style**: VAR (the object's name).
- **Stack**: `[..., key] -> [...]`. Delete obj[key] with the object resolved by name and the key on the stack.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Shaped by **`compile/del-item`** when OBJ is a symbol and KEY is not a feeder, since no fold reaches an object load behind a computed key. May lower to **FAST_ITEM_CLEAR**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_CLEAR.c`

### KOS_OP_VAR_ITEM_TAKE
- **Style**: VAR (the object's name).
- **Stack**: `[..., key] -> [..., value]`. Get obj[key], delete it, and push the value with the object resolved by name and the key on the stack.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Shaped by **`compile/take-item`** when OBJ is a symbol and KEY is not a feeder, since no fold reaches an object load behind a computed key. May lower to **FAST_ITEM_TAKE**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_TAKE.c`

### KOS_OP_VAR_ITEM_REF
- **Style**: VAR (the object's name).
- **Stack**: `[..., key] -> [..., value]`. Push an ItemRef over obj[key] with the object resolved by name and the key on the stack.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Shaped by **`compile/ref-item`** when OBJ is a symbol and KEY is not a feeder, since no fold reaches an object load behind a computed key. May lower to **FAST_ITEM_REF**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_REF.c`

### KOS_OP_VAR_ITEM_STORE
- **Style**: VAR (the object's name).
- **Stack**: `[..., key, value] -> [...]`. Set obj[key] = value with the object resolved by name, the key on the stack and the value on the stack.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Shaped by **`compile/set-item`** when OBJ is a symbol and KEY is not a feeder, since no fold reaches an object load behind a computed key. May lower to **FAST_ITEM_STORE**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_STORE.c`

### KOS_OP_VAR_ITEM_STORE_CONST
- **Style**: EXTRA (VAR, CONST, NOARG, NOARG).
- **Stack**: `[..., key] -> [...]`. Set obj[key] = value with the object resolved by name, the key on the stack and the value a pool constant.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by `ByteCodeProcessor` from `CONST value` ahead of **VAR_ITEM_STORE**, the row **`compile/set-item`** shapes for a symbol object under a computed key. May lower to **FAST_ITEM_STORE_CONST**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_STORE_CONST.c`

### KOS_OP_VAR_ITEM_STORE_VAR
- **Style**: EXTRA (VAR, VAR, NOARG, NOARG).
- **Stack**: `[..., key] -> [...]`. Set obj[key] = value with the object resolved by name, the key on the stack and the value resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by `ByteCodeProcessor` from `VAR value` ahead of **VAR_ITEM_STORE**, the row **`compile/set-item`** shapes for a symbol object under a computed key. May lower to **FAST_ITEM_STORE_FAST**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_STORE_VAR.c`

### KOS_OP_VAR_ITEM_INPLACE
- **Style**: EXTRA (OPCOUNT, VAR, NOARG, NOARG).
- **Stack**: `[..., key, v0 .. v_{n-1}] -> [...]`. Reduce obj[key] in place with the count values left to right and store it back with the object resolved by name and the key on the stack.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Shaped by **`compile/inplace-item`** when OBJ is a symbol and KEY is not a feeder, since no fold reaches an object load behind a computed key. May lower to **FAST_ITEM_INPLACE**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_INPLACE.c`

### KOS_OP_VAR_ITEM_INPLACE_CONST
- **Style**: EXTRA (OPERAND, VAR, CONST, NOARG).
- **Stack**: `[..., key] -> [...]`. Reduce obj[key] in place with the pool constant and store it back with the object resolved by name and the key on the stack.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by `ByteCodeProcessor` from `CONST operand` ahead of **VAR_ITEM_INPLACE** at a count of one, the row **`compile/inplace-item`** shapes for a symbol object under a computed key. May lower to **FAST_ITEM_INPLACE_CONST**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_INPLACE_CONST.c`

### KOS_OP_VAR_ITEM_CONST
- **Style**: EXTRA (VAR, CONST, NOARG, NOARG).
- **Stack**: `[...] -> [..., value]`. Get obj[key] with the object resolved by name and the key a pool constant.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by `ByteCodeProcessor` from `VAR obj, CONST key, ITEM`. May lower to **FAST_ITEM_CONST**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_CONST.c`

### KOS_OP_VAR_ITEM_CONST_CLEAR
- **Style**: EXTRA (VAR, CONST, NOARG, NOARG).
- **Stack**: No stack effect. Delete obj[key] with the object resolved by name and the key a pool constant.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by `ByteCodeProcessor` from `VAR obj, CONST key, ITEM_CLEAR`. May lower to **FAST_ITEM_CONST_CLEAR**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_CONST_CLEAR.c`

### KOS_OP_VAR_ITEM_CONST_TAKE
- **Style**: EXTRA (VAR, CONST, NOARG, NOARG).
- **Stack**: `[...] -> [..., value]`. Get obj[key], delete it, and push the value with the object resolved by name and the key a pool constant.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by `ByteCodeProcessor` from `VAR obj, CONST key, ITEM_TAKE`. May lower to **FAST_ITEM_CONST_TAKE**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_CONST_TAKE.c`

### KOS_OP_VAR_ITEM_CONST_REF
- **Style**: EXTRA (VAR, CONST, NOARG, NOARG).
- **Stack**: `[...] -> [..., value]`. Push an ItemRef over obj[key] with the object resolved by name and the key a pool constant.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by `ByteCodeProcessor` from `VAR obj, CONST key, ITEM_REF`. May lower to **FAST_ITEM_CONST_REF**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_CONST_REF.c`

### KOS_OP_VAR_ITEM_CONST_STORE
- **Style**: EXTRA (VAR, CONST, NOARG, NOARG).
- **Stack**: `[..., value] -> [...]`. Set obj[key] = value with the object resolved by name, the key a pool constant and the value on the stack.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Shaped by **`compile/set-item`** when OBJ is a symbol, KEY is a feeder and a value is not, so the fold could not reach the named operands. May lower to **FAST_ITEM_CONST_STORE**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_CONST_STORE.c`

### KOS_OP_VAR_ITEM_CONST_STORE_CONST
- **Style**: EXTRA (VAR, CONST, CONST, NOARG).
- **Stack**: No stack effect. Set obj[key] = value with the object resolved by name, the key a pool constant and the value a pool constant.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by `ByteCodeProcessor` from `VAR obj, CONST key, CONST value, ITEM_STORE`, or from `CONST value` ahead of the shaped **VAR_ITEM_CONST_STORE**. May lower to **FAST_ITEM_CONST_STORE_CONST**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_CONST_STORE_CONST.c`

### KOS_OP_VAR_ITEM_CONST_STORE_VAR
- **Style**: EXTRA (VAR, CONST, VAR, NOARG).
- **Stack**: No stack effect. Set obj[key] = value with the object resolved by name, the key a pool constant and the value resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by `ByteCodeProcessor` from `VAR obj, CONST key, VAR value, ITEM_STORE`, or from `VAR value` ahead of the shaped **VAR_ITEM_CONST_STORE**. May lower to **FAST_ITEM_CONST_STORE_FAST**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_CONST_STORE_VAR.c`

### KOS_OP_VAR_ITEM_CONST_INPLACE
- **Style**: EXTRA (OPCOUNT, VAR, CONST, NOARG).
- **Stack**: `[..., v0 .. v_{n-1}] -> [...]`. Reduce obj[key] in place with the count values left to right and store it back with the object resolved by name and the key a pool constant.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Shaped by **`compile/inplace-item`** when OBJ is a symbol, KEY is a feeder and a value is not, so the fold could not reach the named operands. May lower to **FAST_ITEM_CONST_INPLACE**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_CONST_INPLACE.c`

### KOS_OP_VAR_ITEM_CONST_INPLACE_CONST
- **Style**: EXTRA (OPERAND, VAR, CONST, CONST).
- **Stack**: No stack effect. Reduce obj[key] in place with the pool constant and store it back with the object resolved by name and the key a pool constant.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by `ByteCodeProcessor` from `VAR obj, CONST key, CONST rhs, ITEM_INPLACE` at a count of one, or from `CONST rhs` ahead of the shaped **VAR_ITEM_CONST_INPLACE**. May lower to **FAST_ITEM_CONST_INPLACE_CONST**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_CONST_INPLACE_CONST.c`

### KOS_OP_VAR_ITEM_VAR
- **Style**: EXTRA (VAR, VAR, NOARG, NOARG).
- **Stack**: `[...] -> [..., value]`. Get obj[key] with the object resolved by name and the key resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by `ByteCodeProcessor` from `VAR obj, VAR key, ITEM`. May lower to **FAST_ITEM_FAST**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_VAR.c`

### KOS_OP_VAR_ITEM_VAR_CLEAR
- **Style**: EXTRA (VAR, VAR, NOARG, NOARG).
- **Stack**: No stack effect. Delete obj[key] with the object resolved by name and the key resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by `ByteCodeProcessor` from `VAR obj, VAR key, ITEM_CLEAR`. May lower to **FAST_ITEM_FAST_CLEAR**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_VAR_CLEAR.c`

### KOS_OP_VAR_ITEM_VAR_TAKE
- **Style**: EXTRA (VAR, VAR, NOARG, NOARG).
- **Stack**: `[...] -> [..., value]`. Get obj[key], delete it, and push the value with the object resolved by name and the key resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by `ByteCodeProcessor` from `VAR obj, VAR key, ITEM_TAKE`. May lower to **FAST_ITEM_FAST_TAKE**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_VAR_TAKE.c`

### KOS_OP_VAR_ITEM_VAR_REF
- **Style**: EXTRA (VAR, VAR, NOARG, NOARG).
- **Stack**: `[...] -> [..., value]`. Push an ItemRef over obj[key] with the object resolved by name and the key resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by `ByteCodeProcessor` from `VAR obj, VAR key, ITEM_REF`. May lower to **FAST_ITEM_FAST_REF**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_VAR_REF.c`

### KOS_OP_VAR_ITEM_VAR_STORE
- **Style**: EXTRA (VAR, VAR, NOARG, NOARG).
- **Stack**: `[..., value] -> [...]`. Set obj[key] = value with the object resolved by name, the key resolved by name and the value on the stack.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Shaped by **`compile/set-item`** when OBJ is a symbol, KEY is a feeder and a value is not, so the fold could not reach the named operands. May lower to **FAST_ITEM_FAST_STORE**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_VAR_STORE.c`

### KOS_OP_VAR_ITEM_VAR_STORE_CONST
- **Style**: EXTRA (VAR, VAR, CONST, NOARG).
- **Stack**: No stack effect. Set obj[key] = value with the object resolved by name, the key resolved by name and the value a pool constant.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by `ByteCodeProcessor` from `VAR obj, VAR key, CONST value, ITEM_STORE`, or from `CONST value` ahead of the shaped **VAR_ITEM_VAR_STORE**. May lower to **FAST_ITEM_FAST_STORE_CONST**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_VAR_STORE_CONST.c`

### KOS_OP_VAR_ITEM_VAR_STORE_VAR
- **Style**: EXTRA (VAR, VAR, VAR, NOARG).
- **Stack**: No stack effect. Set obj[key] = value with the object resolved by name, the key resolved by name and the value resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by `ByteCodeProcessor` from `VAR obj, VAR key, VAR value, ITEM_STORE`, or from `VAR value` ahead of the shaped **VAR_ITEM_VAR_STORE**. May lower to **FAST_ITEM_FAST_STORE_FAST**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_VAR_STORE_VAR.c`

### KOS_OP_VAR_ITEM_VAR_INPLACE
- **Style**: EXTRA (OPCOUNT, VAR, VAR, NOARG).
- **Stack**: `[..., v0 .. v_{n-1}] -> [...]`. Reduce obj[key] in place with the count values left to right and store it back with the object resolved by name and the key resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Shaped by **`compile/inplace-item`** when OBJ is a symbol, KEY is a feeder and a value is not, so the fold could not reach the named operands. May lower to **FAST_ITEM_FAST_INPLACE**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_VAR_INPLACE.c`

### KOS_OP_VAR_ITEM_VAR_INPLACE_CONST
- **Style**: EXTRA (OPERAND, VAR, VAR, CONST).
- **Stack**: No stack effect. Reduce obj[key] in place with the pool constant and store it back with the object resolved by name and the key resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by `ByteCodeProcessor` from `VAR obj, VAR key, CONST rhs, ITEM_INPLACE` at a count of one, or from `CONST rhs` ahead of the shaped **VAR_ITEM_VAR_INPLACE**. May lower to **FAST_ITEM_FAST_INPLACE_CONST**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_VAR_INPLACE_CONST.c`

## 13. Fast variables

### KOS_OP_FAST

- **Style**: FASTVAR. Arg = fast local slot index.
- **Stack**: `[...] -> [..., value]`. Load from slot.
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

### KOS_OP_FAST2

- **Style**: EXTRA (FASTVAR, FASTVAR, NOARG, NOARG). Row = two fast local
  slot indices.
- **Stack**: `[...] -> [..., a, b]`. Load both slots and push in row order.
  Both slots resolve before either push, so an unbound slot errors with
  nothing pushed.
- **Invariants**: Slot indices in range for frame.
- **Compiler form:** Post-compile only: **`convert_fast`** lowering of
  **VAR2** when both names have fast slots. See section 13 **FAST** for the slot
  model.
- **Implementation**: `koskari/lib/opcode/FAST2.c`
- **Covered by**: `tests/compile/test_pack_var_runs.py`,
  `tests/reproducers/test_var_pack.py`

**Example:** Load fast locals 0 and 1.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | FAST2  | 0   | (fast 0 (a), fast 1 (b), 0, 0) | stack: [..., a, b] |
| 1     | RETURN | 1   | 1        | |

### KOS_OP_FAST3

- **Style**: EXTRA (FASTVAR, FASTVAR, FASTVAR, NOARG). Row = three fast local slot indices.
- **Stack**: `[...] -> [..., a, b, c]`. Load the three slots and push in row order. Every
  slot resolves before any push, so an unbound slot errors with nothing
  pushed.
- **Invariants**: Slot indices in range for frame.
- **Compiler form:** Post-compile only: **`convert_fast`** lowering of VAR3
  when all three names have fast slots, or **`ByteCodeProcessor.pack_runs`** on a
  run of three bare FAST loads.
- **Implementation**: `koskari/lib/opcode/FAST3.c`
- **Covered by**: `tests/compile/test_pack_var_runs.py`,
  `tests/reproducers/test_var_pack.py`

**Example:** Load fast locals 0-2.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | FAST3  | 0   | (fast 0 (a), fast 1 (b), fast 2 (c), 0) | stack: [..., a, b, c] |
| 1     | RETURN | 1   | 1        | |

### KOS_OP_FAST4

- **Style**: EXTRA (FASTVAR, FASTVAR, FASTVAR, FASTVAR). Row = four fast
  local slot indices.
- **Stack**: `[...] -> [..., a, b, c, d]`. Load all four slots and push in row
  order. All slots resolve before any push, so an unbound slot errors with
  nothing pushed.
- **Invariants**: Slot indices in range for frame.
- **Compiler form:** Post-compile only: **`convert_fast`** lowering of
  **VAR4** when all four names have fast slots.
- **Implementation**: `koskari/lib/opcode/FAST4.c`
- **Covered by**: `tests/compile/test_pack_var_runs.py`,
  `tests/reproducers/test_var_pack.py`

**Example:** Load fast locals 0-3.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | FAST4  | 0   | (fast 0 (a), fast 1 (b), fast 2 (c), fast 3 (d)) | stack: [..., a, b, c, d] |
| 1     | RETURN | 1   | 1        | |

### KOS_OP_FAST_BIND

- **Style**: FASTVAR. Arg = fast local slot index.
- **Stack**: `[..., ref] -> [...]`. **BIND**: use this reference to resolve the
  given slot (name). See section 1.9. Not interchangeable with FAST_STORE.
- **Invariants**: Slot index in range.
- **Compiler form:** Typically post-compile (from VAR_BIND lowering) or compiler emits with slot index.

**Example:** Bind slot 0 to the reference at TOS.

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 0     | CONST    | 0   | 0 (1)    | stack: [..., ref] |
| 1     | FAST_BIND| 0   | 0 (a)    | BIND: slot 0 resolves to this ref; stack: [...] |

---

### KOS_OP_FAST_CLEAR

- **Style**: FASTVAR. Arg = fast local slot index.
- **Stack**: No stack effect. Clear slot.
- **Invariants**: Slot index in range.

**Example:** Clear slot 1.

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 2     | FAST_CLEAR | 1   | 1        | clear slot 1 |

---

### KOS_OP_FAST_CLEAR2

- **Style**: EXTRA (FASTVAR, FASTVAR, NOARG, NOARG). Row = 2 fast slot indices (post-compile).
- **Stack**: No stack effect. Clear the 2 fast slots in row order, as
  2 `FAST_CLEAR` would; an already empty one stays empty.
- **Compiler form:** Never emitted by specials. Produced only by
  `ByteCodeProcessor.pack_runs`, which combines a run of consecutive
  `FAST_CLEAR` rows (quads first, then one triple or one pair).

### KOS_OP_FAST_CLEAR3

- **Style**: EXTRA (FASTVAR, FASTVAR, FASTVAR, NOARG). Row = 3 fast slot indices (post-compile).
- **Stack**: No stack effect. Clear the 3 fast slots in row order, as
  3 `FAST_CLEAR` would; an already empty one stays empty.
- **Compiler form:** Never emitted by specials. Produced only by
  `ByteCodeProcessor.pack_runs`, which combines a run of consecutive
  `FAST_CLEAR` rows (quads first, then one triple or one pair).

### KOS_OP_FAST_CLEAR4

- **Style**: EXTRA (FASTVAR, FASTVAR, FASTVAR, FASTVAR). Row = 4 fast slot indices (post-compile).
- **Stack**: No stack effect. Clear the 4 fast slots in row order, as
  4 `FAST_CLEAR` would; an already empty one stays empty.
- **Compiler form:** Never emitted by specials. Produced only by
  `ByteCodeProcessor.pack_runs`, which combines a run of consecutive
  `FAST_CLEAR` rows (quads first, then one triple or one pair).

### KOS_OP_FAST_TAKE
- **Style**: FASTVAR. Arg = fast local slot index.
- **Stack**: `[...] -> [..., value]`. Take value from slot via
  `KosNamespace_FastTake` (steal pointer on raw slots without extra INCREF),
  clear slot. **Errors with NameError if slot is empty** (contrast
  **FAST_CLEAR**, idempotent on empty declared slots).
- **Invariants**: Slot index in range; slot must hold a value.
- **Compiler form:** Lowered from **VAR_TAKE** when name has a fixed slot.

**Example:** Take slot 1.

| index | opcode    | arg | arg view | comment |
|------:|-----------|-----|----------|---------|
| 2     | FAST_TAKE | 1   | 1        | take slot 1; stack: [..., value] |

---

### KOS_OP_FAST_REF

- **Style**: FASTVAR. Arg = fast local slot index.
- **Stack**: `[...] -> [..., ref]`. Push reference to slot.
- **Invariants**: Slot index in range.

**Example:** Push reference to slot 0.

| index | opcode  | arg | arg view | comment |
|------:|---------|-----|----------|---------|
| 0     | FAST_REF| 0   | 0 (a)    | stack: [..., ref] |

---

### KOS_OP_FAST_STORE

- **Style**: FASTVAR. Arg = fast local slot index.
- **Stack**: `[..., value] -> [...]`. **STORE**: store this value in the given
  slot (through the reference that already resolves it). See section 1.9. Not
  interchangeable with FAST_BIND.
- **Invariants**: Slot must already be bound. Slot index in range.

**Example:** Store value 99 in slot 0.

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 0     | CONST      | 0   | 0 (99)   | stack: [..., 99] |
| 1     | FAST_STORE | 0   | 0 (a)    | STORE: write 99 into slot 0; stack: [...] |

---

### KOS_OP_FAST_STORE_CONST
- **Style**: EXTRA (FASTVAR, CONST, NOARG, NOARG). Arg = extra-table index (post-compile).
- **Stack**: `[...] -> [...]` (no stack effect). **STORE**: store the constant value into the given fast slot via `KosNamespace_FastStore`. Same semantics as **FAST_STORE**, but the value is from the constant pool (extra row `a1`), not TOS.
- **Peephole**: Not fused directly from **`CONST`** + **`FAST_STORE`** in `ByteCodeProcessor` today; typically appears when **`VAR_STORE_CONST`** is lowered by **`convert_fast`** for a local or closure name.
- **Invariants**: Slot must already be bound; slot index in range; frame namespace must support fast locals (`KosNamespace_SupportsFast`).
- **Compiler form:** Pre-compile arg = 4-tuple `(slot, value, None, None)` (slot may be integer after **`convert_fast`**); post-compile arg = extra index.
- **Implementation**: `koskari/lib/opcode/FAST_STORE_CONST.c`
- **Covered by**: `tests/compile/test_convert_fast.py`, `tests/lib/opcode/test_store_const.py`

**Example:** Store constant 100 in slot 0 without pushing 100.

| index | opcode           | arg | arg view           | comment |
|------:|------------------|-----|--------------------|---------|
| 0     | FAST_STORE_CONST | 0   | 0 -> (0, 100, ...)    | STORE 100 in slot 0; stack unchanged |
| 1     | FAST             | 0   | 0 (a)              | stack: [..., 100] |
| 2     | RETURN           | 1   | 1                  | |

### KOS_OP_FAST_INPLACE

- **Style**: EXTRA (OPERAND, FASTVAR). Row a0 = the bare KosOperand
  selector; a1 = fast slot index.
- **Stack**: `[..., rhs] -> [...]`. Augmented assignment on a fast slot:
  obj = deref(slot); obj OP= rhs; store back. No value pushed.
- **Compiler form:** The lowered form of **VAR_INPLACE**.
- **Implementation**: `koskari/lib/opcode/FAST_INPLACE.c`

### KOS_OP_FAST_INPLACE_CONST

- **Style**: EXTRA (OPERAND, FASTVAR, CONST). Row a0 = bare KosOperand
  selector (the count is fixed at 1 by the shape), a1 = fast slot index,
  a2 = constant pool index of the operand.
- **Stack**: No stack effect. Augmented assignment with a constant rhs on
  a fast slot, completely stack-free: obj = deref(slot); obj OP= const;
  store back. A counted loop's back-edge increment is this single
  dispatch.
- **Compiler form:** The lowered form of **VAR_INPLACE_CONST**.
- **Implementation**: `koskari/lib/opcode/FAST_INPLACE_CONST.c`

### 13.1 Attribute access on fast locals (FAST_ATTR_*)

These opcodes perform attribute get, set, clear, take, or reference on an object fetched from the fast local slot. The object is not on the stack; it is resolved via the fast slot. Each uses EXTRA schema: FASTVAR (slot), CONST (attr name).

- **Compiler form:** Emitted when VAR_ATTR_* is lowered (name has fixed slot).

### KOS_OP_FAST_ATTR
- **Style**: EXTRA (FASTVAR, CONST, NOARG, NOARG).
- **Stack**: `[...] -> [..., value]`. Get attribute on object in fast slot. Equivalent to ATTR but object from slot.
- **Implementation**: `koskari/lib/opcode/FAST_ATTR.c`

### KOS_OP_FAST_ATTR_STORE
- **Style**: EXTRA (FASTVAR, CONST, NOARG, NOARG).
- **Stack**: `[..., value] -> [...]`. Set attribute on object in fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ATTR_STORE.c`

### KOS_OP_FAST_ATTR_CLEAR
- **Style**: EXTRA (FASTVAR, CONST, NOARG, NOARG).
- **Stack**: No stack effect. Delete attribute on object in fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ATTR_CLEAR.c`

### KOS_OP_FAST_ATTR_TAKE
- **Style**: EXTRA (FASTVAR, CONST, NOARG, NOARG).
- **Stack**: `[...] -> [..., value]`. Get attribute on object in fast slot,
  delete it, push value.

### KOS_OP_FAST_ATTR_REF
- **Style**: EXTRA (FASTVAR, CONST, NOARG, NOARG).
- **Stack**: `[...] -> [..., attr_ref]`. Push attribute reference for object in fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ATTR_REF.c`

### KOS_OP_FAST_ATTR_INPLACE

- **Style**: EXTRA (OPCOUNT, FASTVAR, CONST, NOARG). Row a0 = operator selector and count, a1 = the object's fast slot, a2 = the attribute name.
- **Stack**: `[..., v0 .. v_{n-1}] -> [...]`. Read the object from its slot, reduce obj.attr in place with the count values left to right, store it back; pushes nothing. Hot: prologue only, tail-calling the body the elector slot holds, which guards through the field slot and reads, computes, and stores through its location; an empty elector slot or an ungated site takes the electing function, and the generic path runs `kos_attr_inplace_cached` through the field slot.
- **Compiler form:** The lowered form of **VAR_ATTR_INPLACE**.
- **Implementation**: `koskari/lib/opcode/FAST_ATTR_INPLACE.c`

### KOS_OP_ITEM_STORE_FAST
- **Style**: FASTVAR (the value's slot).
- **Stack**: `[..., obj, key] -> [...]`. Set obj[key] = value with the object on the stack, the key on the stack and the value read from a fast slot.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see section 15).
- **Compiler form:** The lowering of **ITEM_STORE_VAR**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/ITEM_STORE_FAST.c`

### KOS_OP_ITEM_CONST_STORE_FAST
- **Style**: EXTRA (CONST, FASTVAR, NOARG, NOARG).
- **Stack**: `[..., obj] -> [...]`. Set obj[key] = value with the object on the stack, the key a pool constant and the value read from a fast slot.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see section 15).
- **Compiler form:** The lowering of **ITEM_CONST_STORE_VAR**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/ITEM_CONST_STORE_FAST.c`

### KOS_OP_ITEM_FAST
- **Style**: FASTVAR (the key's slot).
- **Stack**: `[..., obj] -> [..., value]`. Get obj[key] with the object on the stack and the key read from a fast slot.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see section 15).
- **Compiler form:** The lowering of **ITEM_VAR**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/ITEM_FAST.c`

### KOS_OP_ITEM_FAST_CLEAR
- **Style**: FASTVAR (the key's slot).
- **Stack**: `[..., obj] -> [...]`. Delete obj[key] with the object on the stack and the key read from a fast slot.
- **Compiler form:** The lowering of **ITEM_VAR_CLEAR**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/ITEM_FAST_CLEAR.c`

### KOS_OP_ITEM_FAST_TAKE
- **Style**: FASTVAR (the key's slot).
- **Stack**: `[..., obj] -> [..., value]`. Get obj[key], delete it, and push the value with the object on the stack and the key read from a fast slot.
- **Compiler form:** The lowering of **ITEM_VAR_TAKE**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/ITEM_FAST_TAKE.c`

### KOS_OP_ITEM_FAST_REF
- **Style**: FASTVAR (the key's slot).
- **Stack**: `[..., obj] -> [..., value]`. Push an ItemRef over obj[key] with the object on the stack and the key read from a fast slot.
- **Compiler form:** The lowering of **ITEM_VAR_REF**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/ITEM_FAST_REF.c`

### KOS_OP_ITEM_FAST_STORE
- **Style**: FASTVAR (the key's slot).
- **Stack**: `[..., obj, value] -> [...]`. Set obj[key] = value with the object on the stack, the key read from a fast slot and the value on the stack.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see section 15).
- **Compiler form:** The lowering of **ITEM_VAR_STORE**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/ITEM_FAST_STORE.c`

### KOS_OP_ITEM_FAST_STORE_CONST
- **Style**: EXTRA (FASTVAR, CONST, NOARG, NOARG).
- **Stack**: `[..., obj] -> [...]`. Set obj[key] = value with the object on the stack, the key read from a fast slot and the value a pool constant.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see section 15).
- **Compiler form:** The lowering of **ITEM_VAR_STORE_CONST**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/ITEM_FAST_STORE_CONST.c`

### KOS_OP_ITEM_FAST_STORE_FAST
- **Style**: EXTRA (FASTVAR, FASTVAR, NOARG, NOARG).
- **Stack**: `[..., obj] -> [...]`. Set obj[key] = value with the object on the stack, the key read from a fast slot and the value read from a fast slot.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see section 15).
- **Compiler form:** The lowering of **ITEM_VAR_STORE_VAR**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/ITEM_FAST_STORE_FAST.c`

### KOS_OP_ITEM_FAST_INPLACE
- **Style**: EXTRA (OPCOUNT, FASTVAR, NOARG, NOARG).
- **Stack**: `[..., obj, v0 .. v_{n-1}] -> [...]`. Reduce obj[key] in place with the count values left to right and store it back with the object on the stack and the key read from a fast slot.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see section 15).
- **Compiler form:** The lowering of **ITEM_VAR_INPLACE**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/ITEM_FAST_INPLACE.c`

### KOS_OP_ITEM_FAST_INPLACE_CONST
- **Style**: EXTRA (OPERAND, FASTVAR, CONST, NOARG).
- **Stack**: `[..., obj] -> [...]`. Reduce obj[key] in place with the pool constant and store it back with the object on the stack and the key read from a fast slot.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see section 15).
- **Compiler form:** The lowering of **ITEM_VAR_INPLACE_CONST**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/ITEM_FAST_INPLACE_CONST.c`

### KOS_OP_FAST_ITEM
- **Style**: FASTVAR (the object's slot).
- **Stack**: `[..., key] -> [..., value]`. Get obj[key] with the object read from a fast slot and the key on the stack.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see section 15).
- **Compiler form:** The lowering of **VAR_ITEM**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM.c`

### KOS_OP_FAST_ITEM_CLEAR
- **Style**: FASTVAR (the object's slot).
- **Stack**: `[..., key] -> [...]`. Delete obj[key] with the object read from a fast slot and the key on the stack.
- **Compiler form:** The lowering of **VAR_ITEM_CLEAR**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_CLEAR.c`

### KOS_OP_FAST_ITEM_TAKE
- **Style**: FASTVAR (the object's slot).
- **Stack**: `[..., key] -> [..., value]`. Get obj[key], delete it, and push the value with the object read from a fast slot and the key on the stack.
- **Compiler form:** The lowering of **VAR_ITEM_TAKE**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_TAKE.c`

### KOS_OP_FAST_ITEM_REF
- **Style**: FASTVAR (the object's slot).
- **Stack**: `[..., key] -> [..., value]`. Push an ItemRef over obj[key] with the object read from a fast slot and the key on the stack.
- **Compiler form:** The lowering of **VAR_ITEM_REF**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_REF.c`

### KOS_OP_FAST_ITEM_STORE
- **Style**: FASTVAR (the object's slot).
- **Stack**: `[..., key, value] -> [...]`. Set obj[key] = value with the object read from a fast slot, the key on the stack and the value on the stack.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see section 15).
- **Compiler form:** The lowering of **VAR_ITEM_STORE**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_STORE.c`

### KOS_OP_FAST_ITEM_STORE_CONST
- **Style**: EXTRA (FASTVAR, CONST, NOARG, NOARG).
- **Stack**: `[..., key] -> [...]`. Set obj[key] = value with the object read from a fast slot, the key on the stack and the value a pool constant.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see section 15).
- **Compiler form:** The lowering of **VAR_ITEM_STORE_CONST**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_STORE_CONST.c`

### KOS_OP_FAST_ITEM_STORE_FAST
- **Style**: EXTRA (FASTVAR, FASTVAR, NOARG, NOARG).
- **Stack**: `[..., key] -> [...]`. Set obj[key] = value with the object read from a fast slot, the key on the stack and the value read from a fast slot.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see section 15).
- **Compiler form:** The lowering of **VAR_ITEM_STORE_VAR**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_STORE_FAST.c`

### KOS_OP_FAST_ITEM_INPLACE
- **Style**: EXTRA (OPCOUNT, FASTVAR, NOARG, NOARG).
- **Stack**: `[..., key, v0 .. v_{n-1}] -> [...]`. Reduce obj[key] in place with the count values left to right and store it back with the object read from a fast slot and the key on the stack.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see section 15).
- **Compiler form:** The lowering of **VAR_ITEM_INPLACE**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_INPLACE.c`

### KOS_OP_FAST_ITEM_INPLACE_CONST
- **Style**: EXTRA (OPERAND, FASTVAR, CONST, NOARG).
- **Stack**: `[..., key] -> [...]`. Reduce obj[key] in place with the pool constant and store it back with the object read from a fast slot and the key on the stack.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see section 15).
- **Compiler form:** The lowering of **VAR_ITEM_INPLACE_CONST**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_INPLACE_CONST.c`

### KOS_OP_FAST_ITEM_CONST
- **Style**: EXTRA (FASTVAR, CONST, NOARG, NOARG).
- **Stack**: `[...] -> [..., value]`. Get obj[key] with the object read from a fast slot and the key a pool constant.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see section 15).
- **Compiler form:** The lowering of **VAR_ITEM_CONST**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_CONST.c`

### KOS_OP_FAST_ITEM_CONST_CLEAR
- **Style**: EXTRA (FASTVAR, CONST, NOARG, NOARG).
- **Stack**: No stack effect. Delete obj[key] with the object read from a fast slot and the key a pool constant.
- **Compiler form:** The lowering of **VAR_ITEM_CONST_CLEAR**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_CONST_CLEAR.c`

### KOS_OP_FAST_ITEM_CONST_TAKE
- **Style**: EXTRA (FASTVAR, CONST, NOARG, NOARG).
- **Stack**: `[...] -> [..., value]`. Get obj[key], delete it, and push the value with the object read from a fast slot and the key a pool constant.
- **Compiler form:** The lowering of **VAR_ITEM_CONST_TAKE**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_CONST_TAKE.c`

### KOS_OP_FAST_ITEM_CONST_REF
- **Style**: EXTRA (FASTVAR, CONST, NOARG, NOARG).
- **Stack**: `[...] -> [..., value]`. Push an ItemRef over obj[key] with the object read from a fast slot and the key a pool constant.
- **Compiler form:** The lowering of **VAR_ITEM_CONST_REF**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_CONST_REF.c`

### KOS_OP_FAST_ITEM_CONST_STORE
- **Style**: EXTRA (FASTVAR, CONST, NOARG, NOARG).
- **Stack**: `[..., value] -> [...]`. Set obj[key] = value with the object read from a fast slot, the key a pool constant and the value on the stack.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see section 15).
- **Compiler form:** The lowering of **VAR_ITEM_CONST_STORE**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_CONST_STORE.c`

### KOS_OP_FAST_ITEM_CONST_STORE_CONST
- **Style**: EXTRA (FASTVAR, CONST, CONST, NOARG).
- **Stack**: No stack effect. Set obj[key] = value with the object read from a fast slot, the key a pool constant and the value a pool constant.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see section 15).
- **Compiler form:** The lowering of **VAR_ITEM_CONST_STORE_CONST**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_CONST_STORE_CONST.c`

### KOS_OP_FAST_ITEM_CONST_STORE_FAST
- **Style**: EXTRA (FASTVAR, CONST, FASTVAR, NOARG).
- **Stack**: No stack effect. Set obj[key] = value with the object read from a fast slot, the key a pool constant and the value read from a fast slot.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see section 15).
- **Compiler form:** The lowering of **VAR_ITEM_CONST_STORE_VAR**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_CONST_STORE_FAST.c`

### KOS_OP_FAST_ITEM_CONST_INPLACE
- **Style**: EXTRA (OPCOUNT, FASTVAR, CONST, NOARG).
- **Stack**: `[..., v0 .. v_{n-1}] -> [...]`. Reduce obj[key] in place with the count values left to right and store it back with the object read from a fast slot and the key a pool constant.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see section 15).
- **Compiler form:** The lowering of **VAR_ITEM_CONST_INPLACE**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_CONST_INPLACE.c`

### KOS_OP_FAST_ITEM_CONST_INPLACE_CONST
- **Style**: EXTRA (OPERAND, FASTVAR, CONST, CONST).
- **Stack**: No stack effect. Reduce obj[key] in place with the pool constant and store it back with the object read from a fast slot and the key a pool constant.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see section 15).
- **Compiler form:** The lowering of **VAR_ITEM_CONST_INPLACE_CONST**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_CONST_INPLACE_CONST.c`

### KOS_OP_FAST_ITEM_FAST
- **Style**: EXTRA (FASTVAR, FASTVAR, NOARG, NOARG).
- **Stack**: `[...] -> [..., value]`. Get obj[key] with the object read from a fast slot and the key read from a fast slot.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see section 15).
- **Compiler form:** The lowering of **VAR_ITEM_VAR**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_FAST.c`

### KOS_OP_FAST_ITEM_FAST_CLEAR
- **Style**: EXTRA (FASTVAR, FASTVAR, NOARG, NOARG).
- **Stack**: No stack effect. Delete obj[key] with the object read from a fast slot and the key read from a fast slot.
- **Compiler form:** The lowering of **VAR_ITEM_VAR_CLEAR**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_FAST_CLEAR.c`

### KOS_OP_FAST_ITEM_FAST_TAKE
- **Style**: EXTRA (FASTVAR, FASTVAR, NOARG, NOARG).
- **Stack**: `[...] -> [..., value]`. Get obj[key], delete it, and push the value with the object read from a fast slot and the key read from a fast slot.
- **Compiler form:** The lowering of **VAR_ITEM_VAR_TAKE**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_FAST_TAKE.c`

### KOS_OP_FAST_ITEM_FAST_REF
- **Style**: EXTRA (FASTVAR, FASTVAR, NOARG, NOARG).
- **Stack**: `[...] -> [..., value]`. Push an ItemRef over obj[key] with the object read from a fast slot and the key read from a fast slot.
- **Compiler form:** The lowering of **VAR_ITEM_VAR_REF**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_FAST_REF.c`

### KOS_OP_FAST_ITEM_FAST_STORE
- **Style**: EXTRA (FASTVAR, FASTVAR, NOARG, NOARG).
- **Stack**: `[..., value] -> [...]`. Set obj[key] = value with the object read from a fast slot, the key read from a fast slot and the value on the stack.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see section 15).
- **Compiler form:** The lowering of **VAR_ITEM_VAR_STORE**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_FAST_STORE.c`

### KOS_OP_FAST_ITEM_FAST_STORE_CONST
- **Style**: EXTRA (FASTVAR, FASTVAR, CONST, NOARG).
- **Stack**: No stack effect. Set obj[key] = value with the object read from a fast slot, the key read from a fast slot and the value a pool constant.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see section 15).
- **Compiler form:** The lowering of **VAR_ITEM_VAR_STORE_CONST**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_FAST_STORE_CONST.c`

### KOS_OP_FAST_ITEM_FAST_STORE_FAST
- **Style**: EXTRA (FASTVAR, FASTVAR, FASTVAR, NOARG).
- **Stack**: No stack effect. Set obj[key] = value with the object read from a fast slot, the key read from a fast slot and the value read from a fast slot.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see section 15).
- **Compiler form:** The lowering of **VAR_ITEM_VAR_STORE_VAR**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_FAST_STORE_FAST.c`

### KOS_OP_FAST_ITEM_FAST_INPLACE
- **Style**: EXTRA (OPCOUNT, FASTVAR, FASTVAR, NOARG).
- **Stack**: `[..., v0 .. v_{n-1}] -> [...]`. Reduce obj[key] in place with the count values left to right and store it back with the object read from a fast slot and the key read from a fast slot.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see section 15).
- **Compiler form:** The lowering of **VAR_ITEM_VAR_INPLACE**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_FAST_INPLACE.c`

### KOS_OP_FAST_ITEM_FAST_INPLACE_CONST
- **Style**: EXTRA (OPERAND, FASTVAR, FASTVAR, CONST).
- **Stack**: No stack effect. Reduce obj[key] in place with the pool constant and store it back with the object read from a fast slot and the key read from a fast slot.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see section 15).
- **Compiler form:** The lowering of **VAR_ITEM_VAR_INPLACE_CONST**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_FAST_INPLACE_CONST.c`

## 14. Attributes

### KOS_OP_ATTR

- **Style**: CONST. Arg = constant pool index of attribute name (post-compile).
- **Stack**: `[..., obj] -> [..., value]`. Get attribute; the value replaces the object.
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
- **Stack**: `[..., obj, value] -> [...]`. Set attribute on obj; pop both.
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
- **Stack**: `[..., obj] -> [...]`. Delete attribute.
- **Invariants**: TOS is the object.

**Example:** Delete obj.attr. Pool: 0 = "attr".

| index | opcode     | arg | arg view  | comment |
|------:|------------|-----|-----------|---------|
| 0     | VAR        | 0   | 0 ('obj') | stack: [..., obj] |
| 1     | ATTR_CLEAR | 1   | 1 ('attr')| del obj.attr |

---

### KOS_OP_ATTR_TAKE
- **Style**: CONST. Arg = constant pool index of attribute name.
- **Stack**: `[..., obj] -> [..., value]`. Get attribute, delete it, push value.
  Errors if attribute missing.
- **Invariants**: TOS is the object.
- **Compiler form:** Emitted by [compile/take-attr](../specials/compile-take-attr.md)
  when OBJ is not a symbol.

**Example:** Take obj.attr. Pool: 0 = "attr".

| index | opcode    | arg | arg view  | comment |
|------:|-----------|-----|-----------|---------|
| 0     | VAR       | 0   | 0 ('obj') | stack: [..., obj] |
| 1     | ATTR_TAKE | 1   | 1 ('attr')| take obj.attr; stack: [..., value] |

---

### KOS_OP_ATTR_REF

- **Style**: CONST. Arg = constant pool index of attribute name.
- **Stack**: `[..., obj] -> [..., obj, attr_ref]`. Push attribute reference.
- **Invariants**: TOS is the object.

**Example:** Push reference for obj.prop. Pool: 0 = "prop".

| index | opcode   | arg | arg view  | comment |
|------:|----------|-----|-----------|---------|
| 0     | VAR      | 0   | 0 ('obj') | stack: [..., obj] |
| 1     | ATTR_REF | 1   | 1 ('prop')| stack: [..., obj, attr_ref] |

### KOS_OP_ATTR_INPLACE

- **Style**: EXTRA (OPCOUNT, CONST, NOARG, NOARG). Row a0 = operator selector and count, a1 = the attribute name.
- **Stack**: `[..., obj, v0 .. v_{n-1}] -> [...]`. Reduce obj.attr in place with the count values left to right, store it back, drop the object and the values; pushes nothing (the special appends CONST None). Cold: the operator switch lives in the shared `kos_inplace_reduce`.
- **Compiler form:** Emitted by **`compile/inplace-attr`** when OBJ is an expression.
- **Implementation**: `koskari/lib/opcode/ATTR_INPLACE.c`

## 15. Items

The item rows are named by a three-position grammar: the leading
marker names the object's origin, ITEM is followed by the key's, and a
STORE or INPLACE verb by its value's. An unmarked operand rides the
stack; CONST is a pool constant, VAR a name resolved in the frame
namespace (section 12), FAST a fast slot (section 13). FAST_ITEM_CONST_STORE_FAST is
a fast object, a constant key, and a fast value; ITEM_VAR_STORE is a
stack object, a named key, and a stack value. The row's EXTRA fields
follow name order, skipping stack operands; a row with one named
operand takes that operand's plain style. The stack order of the
operands a row reads is object, key, value.

Every combination of stack and slot object, stack, constant, and slot
key, and stack, constant, and slot value exists for ITEM, CLEAR, TAKE,
REF, and STORE; the in-place rows take their operand from the stack or
the pool, as every other INPLACE row does. A VAR row is the twin of a
FAST row for a name with no fast slot; the lowering is all or nothing
per row.

The ITEM, STORE, and INPLACE rows of the stack tier and the FAST tier
carry one KOS_CACHE_ITEM slot each. A handler fetches its operands by
origin and calls `kos_item_load`, `kos_item_store`, or
`kos_item_inplace` (`eval/eval_item.c`), which on the site's first
execution elects a body for the container kind it sees: for a load an
exact list or tuple under an exact int key or an exact dict under any
key, for a store or an in-place reduce an exact list under an exact
int key or an exact dict, since a tuple takes no store. The body
guards the kind on every execution and reads or writes the container
directly; an index outside the sequence or a key the dict lacks runs
the object protocol for that execution, so IndexError and KeyError
keep their messages, and eight guard misses demote the site to the
protocol until the protocol's own count wraps and the site re-elects.
A dict body that hashed the key and cannot finish keeps its hold on
the object and key and hands it to the protocol path, since the hash
may have rebound the slot a handler peeked. The in-place bodies are
one per operator for int and float cells plus the str body for `+`,
find the cell inside the body, and offer the cell's value as the
result's home under the in-place result rule. CLEAR, TAKE, and REF
run the object protocol through `kos_item_clear`, `kos_item_take`, and
`kos_item_ref`, which hold the operands across the call.

The compiler emits the plain stack row after the object, the key, and
the value, and `ByteCodeProcessor` folds the run of feeders directly
ahead of the row, which in stack order is the value, then the key,
then the object; a CONST in the object position never folds. A special
shapes its emission only where a non-feeder operand would sit between
a named operand and the row: a symbol object under a computed key
names the object, and a computed value behind a symbol object or a
feeder key names what it can.

**Example:** `(set-item d "k" 42)` in a function body, `d` a local.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | FAST_ITEM_CONST_STORE_CONST | 0 | (fast 0 (d), c#0 ('k'), c#1 (42), 0) | d['k'] = 42; no stack effect |

### KOS_OP_ITEM
- **Style**: NOARG.
- **Stack**: `[..., obj, key] -> [..., value]`. Get obj[key] with the object on the stack and the key on the stack.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see section 15).
- **Compiler form:** Emitted by **`compile/item`**; `ByteCodeProcessor` folds the run of feeders (bare VAR and CONST loads) ahead of it into the rows that name them.
- **Implementation**: `koskari/lib/opcode/ITEM.c`

### KOS_OP_ITEM_CLEAR
- **Style**: NOARG.
- **Stack**: `[..., obj, key] -> [...]`. Delete obj[key] with the object on the stack and the key on the stack.
- **Compiler form:** Emitted by **`compile/del-item`**; `ByteCodeProcessor` folds the run of feeders (bare VAR and CONST loads) ahead of it into the rows that name them.
- **Implementation**: `koskari/lib/opcode/ITEM_CLEAR.c`

### KOS_OP_ITEM_TAKE
- **Style**: NOARG.
- **Stack**: `[..., obj, key] -> [..., value]`. Get obj[key], delete it, and push the value with the object on the stack and the key on the stack.
- **Compiler form:** Emitted by **`compile/take-item`**; `ByteCodeProcessor` folds the run of feeders (bare VAR and CONST loads) ahead of it into the rows that name them.
- **Implementation**: `koskari/lib/opcode/ITEM_TAKE.c`

### KOS_OP_ITEM_REF
- **Style**: NOARG.
- **Stack**: `[..., obj, key] -> [..., value]`. Push an ItemRef over obj[key] with the object on the stack and the key on the stack.
- **Compiler form:** Emitted by **`compile/ref-item`**; `ByteCodeProcessor` folds the run of feeders (bare VAR and CONST loads) ahead of it into the rows that name them.
- **Implementation**: `koskari/lib/opcode/ITEM_REF.c`

### KOS_OP_ITEM_STORE
- **Style**: NOARG.
- **Stack**: `[..., obj, key, value] -> [...]`. Set obj[key] = value with the object on the stack, the key on the stack and the value on the stack.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see section 15).
- **Compiler form:** Emitted by **`compile/set-item`**; `ByteCodeProcessor` folds the run of feeders (bare VAR and CONST loads) ahead of it into the rows that name them.
- **Implementation**: `koskari/lib/opcode/ITEM_STORE.c`

### KOS_OP_ITEM_STORE_CONST
- **Style**: CONST (the value).
- **Stack**: `[..., obj, key] -> [...]`. Set obj[key] = value with the object on the stack, the key on the stack and the value a pool constant.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see section 15).
- **Compiler form:** Folded by `ByteCodeProcessor` from `CONST value, ITEM_STORE`.
- **Implementation**: `koskari/lib/opcode/ITEM_STORE_CONST.c`

### KOS_OP_ITEM_INPLACE
- **Style**: OPCOUNT (operator selector, value count).
- **Stack**: `[..., obj, key, v0 .. v_{n-1}] -> [...]`. Reduce obj[key] in place with the count values left to right and store it back with the object on the stack and the key on the stack.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see section 15).
- **Compiler form:** Emitted by **`compile/inplace-item`**; `ByteCodeProcessor` folds the run of feeders (bare VAR and CONST loads) ahead of it into the rows that name them.
- **Implementation**: `koskari/lib/opcode/ITEM_INPLACE.c`

### KOS_OP_ITEM_INPLACE_CONST
- **Style**: EXTRA (OPERAND, CONST, NOARG, NOARG).
- **Stack**: `[..., obj, key] -> [...]`. Reduce obj[key] in place with the pool constant and store it back with the object on the stack and the key on the stack.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see section 15).
- **Compiler form:** Folded by `ByteCodeProcessor` from `CONST rhs, ITEM_INPLACE` at a count of one, or from `CONST rhs` ahead of the shaped **ITEM_INPLACE**.
- **Implementation**: `koskari/lib/opcode/ITEM_INPLACE_CONST.c`

### KOS_OP_ITEM_CONST
- **Style**: CONST (the key).
- **Stack**: `[..., obj] -> [..., value]`. Get obj[key] with the object on the stack and the key a pool constant.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see section 15).
- **Compiler form:** Folded by `ByteCodeProcessor` from `CONST key, ITEM`.
- **Implementation**: `koskari/lib/opcode/ITEM_CONST.c`

### KOS_OP_ITEM_CONST_CLEAR
- **Style**: CONST (the key).
- **Stack**: `[..., obj] -> [...]`. Delete obj[key] with the object on the stack and the key a pool constant.
- **Compiler form:** Folded by `ByteCodeProcessor` from `CONST key, ITEM_CLEAR`.
- **Implementation**: `koskari/lib/opcode/ITEM_CONST_CLEAR.c`

### KOS_OP_ITEM_CONST_TAKE
- **Style**: CONST (the key).
- **Stack**: `[..., obj] -> [..., value]`. Get obj[key], delete it, and push the value with the object on the stack and the key a pool constant.
- **Compiler form:** Folded by `ByteCodeProcessor` from `CONST key, ITEM_TAKE`.
- **Implementation**: `koskari/lib/opcode/ITEM_CONST_TAKE.c`

### KOS_OP_ITEM_CONST_REF
- **Style**: CONST (the key).
- **Stack**: `[..., obj] -> [..., value]`. Push an ItemRef over obj[key] with the object on the stack and the key a pool constant.
- **Compiler form:** Folded by `ByteCodeProcessor` from `CONST key, ITEM_REF`.
- **Implementation**: `koskari/lib/opcode/ITEM_CONST_REF.c`

### KOS_OP_ITEM_CONST_STORE
- **Style**: CONST (the key).
- **Stack**: `[..., obj, value] -> [...]`. Set obj[key] = value with the object on the stack, the key a pool constant and the value on the stack.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see section 15).
- **Compiler form:** Shaped by **`compile/set-item`** when OBJ is a symbol or KEY is a feeder and a value is not, so the fold could not reach the named operands.
- **Implementation**: `koskari/lib/opcode/ITEM_CONST_STORE.c`

### KOS_OP_ITEM_CONST_STORE_CONST
- **Style**: EXTRA (CONST, CONST, NOARG, NOARG).
- **Stack**: `[..., obj] -> [...]`. Set obj[key] = value with the object on the stack, the key a pool constant and the value a pool constant.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see section 15).
- **Compiler form:** Folded by `ByteCodeProcessor` from `CONST key, CONST value, ITEM_STORE`, or from `CONST value` ahead of the shaped **ITEM_CONST_STORE**.
- **Implementation**: `koskari/lib/opcode/ITEM_CONST_STORE_CONST.c`

### KOS_OP_ITEM_CONST_INPLACE
- **Style**: EXTRA (OPCOUNT, CONST, NOARG, NOARG).
- **Stack**: `[..., obj, v0 .. v_{n-1}] -> [...]`. Reduce obj[key] in place with the count values left to right and store it back with the object on the stack and the key a pool constant.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see section 15).
- **Compiler form:** Shaped by **`compile/inplace-item`** when OBJ is a symbol or KEY is a feeder and a value is not, so the fold could not reach the named operands.
- **Implementation**: `koskari/lib/opcode/ITEM_CONST_INPLACE.c`

### KOS_OP_ITEM_CONST_INPLACE_CONST
- **Style**: EXTRA (OPERAND, CONST, CONST, NOARG).
- **Stack**: `[..., obj] -> [...]`. Reduce obj[key] in place with the pool constant and store it back with the object on the stack and the key a pool constant.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see section 15).
- **Compiler form:** Folded by `ByteCodeProcessor` from `CONST key, CONST rhs, ITEM_INPLACE` at a count of one, or from `CONST rhs` ahead of the shaped **ITEM_CONST_INPLACE**.
- **Implementation**: `koskari/lib/opcode/ITEM_CONST_INPLACE_CONST.c`

## 15.1 Reference dereference

### KOS_OP_DEREF

- **Style**: NOARG.
- **Stack**: `[..., ref] -> [..., value]`. Pop one value (a Ref with a deref method). Call `ref.deref()`, push result. Raises TypeError if TOS has no deref method.
- **Use-case**: Fast path for dereferencing a mutable reference. Used by the `(deref EXPR)` special form.

**Example:** Dereference a ref. For `(deref r)` where r holds a Ref: VAR (or FAST) loads r, DEREF calls ref.deref().

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | VAR    | 0   | 0 ('r')  | stack: [..., ref] |
| 1     | DEREF  | 0   | 0        | stack: [..., value] |

## 16. Argument production

### KOS_OP_BUILD_ARGS

- **Style**: COUNT. Arg = number of positional arguments.
- **Stack**: `[..., v_0, ..., v_{n-1}] -> [..., tuple]`. Pop n elements and push a tuple of them.
- **Example:** Build empty args. Covered by `tests/lib/opcode/test_arg_opcodes.py`.

| index | opcode    | arg | arg view | comment |
|------:|-----------|-----|----------|---------|
| 0     | BUILD_ARGS| 0   | 0        | stack: [..., ()] |
| 1     | RETURN    | 1   | 1        | |

---

### KOS_OP_BUILD_FUNCTION

- **Style**: EXTRA (`CONST`, `CONST`, `NOARG`, `NOARG`). Instruction arg = index into the code object's extra table; that row holds `a0` = constant pool index of the **keyword-defaults names tuple** and `a1` = constant pool index of the **annotation names tuple**. Either or both may be the empty tuple, meaning no values of that kind on the stack.
- **Stack**: `[..., ann_1, ..., ann_M, def_1, ..., def_N, fspec] -> [..., function]`, where N = len(defaults names tuple) and M = len(annotation names tuple); either may be 0. The VM pops fspec, then (when N > 0) pops N values into a keyword-defaults dict keyed by the defaults names tuple, then (when M > 0) pops M more values into an annotations dict keyed by the annotation names tuple, and builds the function from the spec against the current frame namespace. The annotations dict becomes the function's creation-time annotations: values are real objects, except str values, which remain forward references resolved lazily on first `__annotations__` / `__signature__` access.
- **Invariants**: Both extra-row fields must be constant-pool tuples; the stack must hold M + N values below fspec, with annotation values below default values.
- **Compiler form:** Pre-compile extra operand is a 4-tuple `(defnames_tuple, annotation_names_tuple, 0, 0)`; `NOARG` slots are ignored by the assembler. The annotation names tuple is the annotated parameter names in order, followed by `"return"` when a return annotation is present.

**Example:** Build annotated function with one keyword default, as for `(lambda [(a :: int value: 9)] a)`. Pool: 0 = int, 1 = 9, 2 = function_spec; extra row: `(("a",), ("a",), 0, 0)`. Covered by `tests/lib/opcode/test_build_function.py`.

| index | opcode         | arg | arg view              | comment |
|------:|----------------|-----|-----------------------|---------|
| 0     | CONST          | 0   | 0 (int)               | annotation value; stack: [..., int] |
| 1     | CONST          | 1   | 1 (9)                 | default value; stack: [..., int, 9] |
| 2     | CONST          | 2   | 2 (fspec)             | stack: [..., int, 9, fspec] |
| 3     | BUILD_FUNCTION | 0   | 0 (("a",), ("a",), ...) | stack: [..., function] |
| 4     | RETURN         | 1   | 1                     | |

Un-annotated, no-defaults form: extra row `((), (), 0, 0)`; only fspec is popped.

---

### KOS_OP_BUILD_KWDS

- **Style**: PAIR_COUNT. Arg = number of keyword pairs.
- **Stack**: `[..., k_0, v_0, ..., k_{n-1}, v_{n-1}] -> [..., dict]`. Pop 2*opa elements as key,value pairs and push a dict.

### KOS_OP_BUILD_KWDS_NAMES

- **Style**: CONST. Arg = constant pool index of names tuple.
- **Stack**: `[..., v_0, ..., v_{n-1}] -> [..., dict]`. Pop n values, map names[i] -> value[i], push dict. Names tuple from constant pool; values from stack (val_0 deepest, val_{n-1} at TOS).

### KOS_OP_BUILD_LIST

- **Style**: COUNT. Arg = number of elements.
- **Stack**: `[..., v_0, ..., v_{n-1}] -> [..., list]`. Pop n elements into a new list and push it. Empty (arg 0) pushes `[]`.
- **Compiler form:** `compile/#list` for a leading/all dynamic run (`collections.py`).

### KOS_OP_BUILD_LIST_CONST

- **Style**: CONST. Arg = constant pool index of a sequence (typically a pooled constant tuple).
- **Stack**: `[...] -> [..., list]`. Build a **fresh** list from the pooled sequence (`PySequence_List`) and push it; each eval mints a new list, never aliasing the immutable pool value.
- **Compiler form:** `compile/#list` all-constant body, or the seed of a list whose first run is constant.

### KOS_OP_VAR_LIST_APPEND

- **Style**: VAR. Arg = variable name (may lower to FAST_LIST_APPEND).
- **Stack**: `[..., value] -> [...]`. Append TOS to the list bound to the named variable, in place. Used by `compile/#list-append` on a symbol binding.

### KOS_OP_FAST_LIST_APPEND

- **Style**: FASTVAR. Arg = frame slot index.
- **Stack**: `[..., value] -> [...]`. Fast-slot form of VAR_LIST_APPEND.

### KOS_OP_BUILD_LIST_APPEND

- **Style**: COUNT. Arg = number of values to append.
- **Stack**: `[..., list, v_0, ..., v_{n-1}] -> [..., list]`. Append the top n values to the list at depth n, in place; pop the values; leave the list. TypeError when the list slot is not a list. (Widened from the former NOARG append-one; `compile/#list-append` on a list expression emits count 1.)
- **Compiler form:** `compile/#list` dynamic tail run of k; `compile/#list-append` list-expression path (count 1).

### KOS_OP_BUILD_LIST_APPEND_CONST

- **Style**: CONST. Arg = constant pool index of a single value.
- **Stack**: `[..., list] -> [..., list]`. Append the pooled value to the TOS list in place. TypeError when TOS is not a list.
- **Compiler form:** `compile/#list` tail constant run of length 1.

### KOS_OP_BUILD_LIST_EXTEND

- **Style**: NOARG.
- **Stack**: `[..., list, iterable] -> [..., list]`. Extend the list in place from the TOS iterable (`PySequence_InPlaceConcat`, so any iterable the list API accepts); pop the iterable; leave the list. TypeError when the list slot is not a list.
- **Compiler form:** `compile/#list` `*:` splat segment.

### KOS_OP_BUILD_LIST_EXTEND_CONST

- **Style**: CONST. Arg = constant pool index of a sequence (typically a pooled constant tuple).
- **Stack**: `[..., list] -> [..., list]`. Extend the TOS list in place from the pooled sequence (tuple fast path, general-iterable fallback). TypeError when TOS is not a list.
- **Compiler form:** `compile/#list` tail constant run of length >= 2.

### KOS_OP_COMBINE_ARGS

- **Style**: COUNT. Arg = count of sequences to combine.
- **Stack**: `[..., t_0, ..., t_{m-1}] -> [..., tuple]`. Concatenate the m sequences (each via iteration) into a single tuple. Non-tuple values are converted with PySequence_Tuple when m == 1.

### KOS_OP_COMBINE_KWDS

- **Style**: COUNT. Arg = count of dicts to merge.
- **Stack**: `[..., d_0, ..., d_{m-1}] -> [..., dict]`. Merge the m dicts (later overwrites earlier on key clash) and push result.

## 17. Calls

### KOS_OP_CALL

- **Style**: COUNT. Arg = number of positional arguments.
- **Stack**: `[..., callable, arg_1, ..., arg_n] -> [..., result]`. KosExpression/KosFunction: new frame pushed, in-loop execution. Python callable: may suspend on awaitable.
- **Execution model**: Only the **call result** is ever auto-awaited: when a Python call returns a coroutine/awaitable, the VM awaits it (flattened) and pushes the final value. All other opcodes (CONST, loads, getattr, subscript, binary ops, yield-from results, etc.) push their value with no automatic await; use the AWAIT opcode to await explicitly.
- **Invariants**: Stack has at least 1 + arg elements. See execution-invariants for in-loop flattening.

**Example:** Call with 1 argument. Pool: 0 = fn, 1 = 42.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (fn)   | stack: [..., callable] |
| 1     | CONST  | 1   | 1 (42)   | stack: [..., callable, 42] |
| 2     | CALL   | 1   | 1        | pop callable + 1 arg; push result. stack: [..., result] |
| 3     | RETURN | 1   | 1        | |

---

### KOS_OP_CALL_NAMES
- **Style**: COUNT. Arg = N (number of positional arguments).
- **Stack**: `[..., callable, pos_0, ..., pos_{N-1}, val_0, ..., val_{M-1}, names_tuple] -> [..., result]`. TOS = names_tuple (tuple of M keyword names); below it the M keyword values in the same order as names; below that the N positional args; then callable. TailPtr for callable + args + kwnames; validate names tuple; kos_callable_vcall; DropNoErr. No tuple or dict allocation.
- **Invariants**: names_tuple must be a tuple; stack must have 1 + N + M + 1 elements (callable + N positionals + M keyword values + names).

**Example:** Call with 1 positional and 1 keyword (x=42). Pool: 0 = fn, 1 = 10, 2 = 42, 3 = ("x",).

| index | opcode    | arg | arg view | comment |
|------:|-----------|-----|----------|---------|
| 0     | CONST     | 0   | 0 (fn)   | stack: [..., callable] |
| 1     | CONST     | 1   | 1 (10)   | stack: [..., callable, 10] |
| 2     | CONST     | 2   | 2 (42)   | stack: [..., callable, 10, 42] |
| 3     | CONST     | 3   | 3 (names)| stack: [..., callable, 10, 42, names] |
| 4     | CALL_NAMES| 1   | 1        | stack: [..., result] |
| 5     | RETURN    | 1   | 1        | |

---

### KOS_OP_CALL_STAR
- **Style**: STAR. Arg = bitflags (1 = has *args, 2 = has **kwargs).
- **Stack**: `[..., callable, pos, kwd]` with kwd at TOS. pos = *args (tuple or iterable); kwd = **kwargs (dict). Pop per opa: both (opa & 3 == 3): pop 3; **kwargs only (opa & 2): pop 2, pos = (); *args only (opa & 1): pop 2, kwd = NULL; neither: pop 1, pos = (), kwd = NULL. Ensure pos is tuple (PySequence_Tuple if needed). Call via kos_callable_call(ctx, out_ev, callable, pos, kwd).
- **Invariants**: Stack height per opa; pos converted to tuple when *args present.

**Example:** Call with *args and **kwargs. Pool: 0 = fn, 1 = (1,2), 2 = {"x": 3}.

| index | opcode  | arg | arg view | comment |
|------:|---------|-----|----------|---------|
| 0     | CONST   | 0   | 0 (fn)   | stack: [..., callable] |
| 1     | CONST   | 1   | 1 (pos)  | stack: [..., callable, pos] |
| 2     | CONST   | 2   | 2 (kwd)  | stack: [..., callable, pos, kwd] |
| 3     | CALL_STAR | 3 | 3 (bits) | stack: [..., result] |
| 4     | RETURN  | 1   | 1        | |

---

### KOS_OP_CALL_VAR
- **Style**: EXTRA. Schema: VAR (name cpool idx), COUNT (npos), NOARG, NOARG.
- **Stack**: `[..., pos_0, ..., pos_{N-1}] -> [..., result]`. The callee is read from the frame namespace by name and is not on the stack. A Koskari callable runs in the evaluator with the arguments taken from the stack; any other callable is called with them and its result pushed, awaited when awaitable.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

---

### KOS_OP_CALL_FAST
- **Style**: EXTRA. Schema: FASTVAR (slot index), COUNT (npos), NOARG, NOARG.
- **Stack**: Same as CALL_VAR. Callable is loaded from fast local slot via `KosNamespace_FastDeref`; not on stack.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

---

### KOS_OP_CALL_VAR_NAMES
- **Style**: EXTRA. Schema: VAR (name cpool idx), CONST (kw names tuple cpool idx), COUNT (npos), NOARG.
- **Stack**: `[..., pos_0, ..., pos_{N-1}, kwd_val_0, ..., kwd_val_{M-1}] -> [..., result]`. Callable from namespace by name; kw names tuple from constant pool. Same call semantics as CALL_NAMES.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

---

### KOS_OP_CALL_FAST_NAMES
- **Style**: EXTRA. Schema: FASTVAR (slot index), CONST (kw names tuple cpool idx), COUNT (npos), NOARG.
- **Stack**: Same as CALL_VAR_NAMES. Callable from fast slot via `KosNamespace_FastDeref`.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

---

### KOS_OP_CALL_VAR_STAR
- **Style**: EXTRA. Schema: VAR (name cpool idx), STAR (bitflags: 1 = *args, 2 = **kwargs), NOARG, NOARG.
- **Stack**: Same as CALL_STAR minus callable. `[..., pos, kwd]` or subsets per flags. The callee is read from the frame namespace by name. The operands present are popped per the flags, `pos` is made a tuple, and the callee is called with them.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

---

### KOS_OP_CALL_FAST_STAR
- **Style**: EXTRA. Schema: FASTVAR (slot index), STAR (bitflags), NOARG, NOARG.
- **Stack**: Same as CALL_VAR_STAR. Callable from fast slot via `KosNamespace_FastDeref`.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

---

### KOS_OP_CALL_ATTR
- **Style**: EXTRA. Schema: CONST (attr name), COUNT (npos), NOARG, NOARG.
- **Stack**: `[..., obj, arg_0, ..., arg_{N-1}] -> [..., result]`. Attr name from constant pool (EXTRA row). `obj.name` is resolved as an attribute read would resolve it, then invoked. An unbound plain function found on the class is called as `fn(obj, *args)`; a bound method, an instance-dict callable or a descriptor's result is called as `callable(*args)`.
- **Invariants**: Stack has at least 1 + N elements (obj, N args). Resolved attribute must be callable or TypeError.

- **Receiver**: the resolution decides whether `obj` is passed: an unbound plain function from the class gets it as the first argument; anything else is called with the arguments alone. A Koskari callable runs in the evaluator; any other callable is called and its result pushed, awaited when awaitable.

**Example:** `"hello".upper()` (0 args). Pool: 0 = "upper", 1 = "hello". EXTRA row 0: a0 = cpool index of "upper", a1 = 0 (npos).

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 0     | CONST    | 1   | 1 ("hello") | stack: [..., obj] |
| 1     | CALL_ATTR| 0   | 0 (extra idx) | resolve obj.upper; call; push result. stack: [..., "HELLO"] |
| 2     | RETURN   | 1   | 1        | |

**Example:** `c.add_one(41)` (1 arg, unbound method). Pool: 0 = "add_one", 1 = c, 2 = 41. EXTRA row 0: a0 = cpool index of "add_one", a1 = 1 (npos).

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 0     | CONST    | 1   | 1 (c)    | stack: [..., obj] |
| 1     | CONST    | 2   | 2 (41)   | stack: [..., obj, 41] |
| 2     | CALL_ATTR| 0   | 0 (extra idx) | resolve; call add_one(c, 41); push 42. stack: [..., 42] |
| 3     | RETURN   | 1   | 1        | |

---

### KOS_OP_CALL_ATTR_NAMES
- **Style**: EXTRA. Schema: CONST (attr name), CONST (kwarg names tuple), COUNT (npos), NOARG.
- **Stack**: `[..., obj, pos_0, ..., pos_{N-1}, kwd_val_0, ..., kwd_val_{M-1}] -> [..., result]`. `obj.name` resolves as CALL_ATTR describes; the call then proceeds like CALL_NAMES. The kwarg names tuple and attr name are in the constant pool (EXTRA row), not on the stack.
- **Compiler form:** Emitted by `compile/call-attr` when the call has keyword arguments and no `*:` or `**:`.

---

### KOS_OP_CALL_FAST_ATTR
- **Style**: EXTRA. Schema: FASTVAR (slot index for `obj`), CONST (attr name), COUNT (npos), NOARG.
- **Stack**: `[..., arg_0, ..., arg_{N-1}] -> [..., result]`. Same resolution and invocation rules as [`KOS_OP_CALL_ATTR`](#kos_op_call_attr), but `obj` is **not** on the stack: it is read from the current frame's fast slot `a0`, so the frame must have fast locals. An unbound function found on the class receives `obj` as its first argument; any other callable receives the `N` arguments alone.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

---

### KOS_OP_CALL_VAR_ATTR
- **Style**: EXTRA. Schema: VAR (name cpool index for `obj`), CONST (attr name), COUNT (npos), NOARG.
- **Stack**: Same as `KOS_OP_CALL_FAST_ATTR`. `obj` is read from the frame namespace by name.
- **Behaviour**: otherwise as `KOS_OP_CALL_FAST_ATTR` and `KOS_OP_CALL_ATTR`: the same resolution, the same receiver rule, a Koskari callable run in the evaluator and any other called with its result pushed.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

---

### KOS_OP_CALL_FAST_ATTR_NAMES
- **Style**: EXTRA. Schema: FASTVAR (slot index for obj), CONST (attr name), CONST (kwarg names tuple), COUNT (npos).
- **Stack**: `[..., pos_0, ..., pos_{N-1}, kwd_val_0, ..., kwd_val_{M-1}] -> [..., result]`. `obj` is read from fast slot `a0` and is not on the stack, one fewer element than CALL_ATTR_NAMES. Resolution and the call proceed as CALL_ATTR_NAMES describes: an unbound function found on the class receives `obj` first, any other callable the arguments alone.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

---

### KOS_OP_CALL_VAR_ATTR_NAMES
- **Style**: EXTRA. Schema: VAR (name/cpool for obj), CONST (attr name), CONST (kwarg names tuple), COUNT (npos).
- **Stack**: Same as CALL_FAST_ATTR_NAMES. `obj` is read from the frame namespace by name instead of a fast slot.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

---

### KOS_OP_CALL_ATTR_STAR
- **Style**: EXTRA. Schema: CONST (attr name), COUNT (opa bitflags: 1 = has *args, 2 = has **kwargs), NOARG, NOARG.
- **Stack**: `obj.name(*args, **kwargs)`. Stack layout per opa flags (bottom ... TOS): opa=0 -> `[obj]`; opa=1 -> `[obj, pos]` (pos = *args); opa=2 -> `[obj, kwd]` (kwd = **kwargs); opa=3 -> `[obj, pos, kwd]`. Pop in reverse order (kwd then pos then obj when both star forms are present). `pos` is made a tuple when it is another sequence. `obj.name` resolves as CALL_ATTR describes; `obj` is prepended to the positional tuple when the attribute is an unbound function, and the callee is called with the tuple and the keywords.
- **Invariants**: attr name in constant pool. opa bits: 1 = has *args, 2 = has **kwargs.

---

### KOS_OP_CALL_VAR_ATTR_STAR
- **Style**: EXTRA. Schema: VAR (name cpool index for `obj`), CONST (attr name), COUNT (opa bitflags: 1 = has *args, 2 = has **kwargs), NOARG.
- **Stack**: Same semantics as [`KOS_OP_CALL_ATTR_STAR`](#kos_op_call_attr_star), but `obj` is **not** on the stack; it is read from the frame namespace by name. Stack layout per opa (bottom ... TOS): opa=0 -> `[]`; opa=1 -> `[pos]`; opa=2 -> `[kwd]`; opa=3 -> `[pos, kwd]` with `kwd` at TOS (same relative order as CALL_ATTR_STAR without the receiver). Pop order and `opa` handling match [`KOS_OP_CALL_VAR_STAR`](#kos_op_call_var_star) combined with CALL_ATTR_STAR; `obj.name` resolves as CALL_ATTR describes, `obj` is prepended to the positional tuple when the attribute is an unbound function, and the callee is called with the tuple and the keywords.
- **Invariants**: `var_name` and attr name in constant pool; same opa bit meanings as CALL_ATTR_STAR.
- **See also**: [`KOS_OP_CALL_VAR_ATTR`](#kos_op_call_var_attr) for the receiver read by name.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

---

### KOS_OP_CALL_FAST_ATTR_STAR
- **Style**: EXTRA. Schema: FASTVAR (slot index for `obj`), CONST (attr name), COUNT (opa bitflags), NOARG.
- **Stack**: Same stack layout and `opa` behavior as [`KOS_OP_CALL_VAR_ATTR_STAR`](#kos_op_call_var_attr_star). `obj` is read from fast slot `a0`, so the frame must have fast locals. Pop, resolution and the call match CALL_ATTR_STAR.
- **Invariants**: Same opa and tuple/`pos` rules as CALL_ATTR_STAR; fast slot must be in range and bound.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

---

### KOS_OP_TAIL_CALL

- **Style**: COUNT. Arg = number of positional arguments.
- **Stack**: Same as CALL: `[..., callable, arg_1, ..., arg_n]`. The arguments are taken off the stack before the callee is examined. A Koskari callable takes over the current frame; any other callable is called after the current frame is dropped, when it has a parent, and its result goes to the caller. Control then continues in the frame now on top, running any wind thunks on the way.
- **Execution model**: When the callable is a KosExpression or KosFunction, the VM replaces the current frame (tail install). For all other callable types, the opcode drops the current frame when parent exists, calls the callable, and pushes the result.
- **Invariants**: Stack has at least 1 + arg elements. KosExpression allows no arguments (arg must be 0); otherwise TypeError.
- **Compiler form:** Produced by the processor's `promote_tail_calls` pass relabeling a tail-position CALL in function-body code; never emitted by a special.

**Example:** Tail call with 0 args (replace frame with callee). Pool: 0 = fn.

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 0     | CONST    | 0   | 0 (fn)   | stack: [..., callable] |
| 1     | TAIL_CALL| 0   | 0        | replace frame; run callee. stack: [...,] (new frame) |

---

### KOS_OP_TAIL_CALL_NAMES
- **Style**: COUNT. Arg = N (number of positional arguments).
- **Stack**: Same layout as CALL_NAMES: `[..., callable, pos_0, ..., pos_{N-1}, val_0, ..., val_{M-1}, names_tuple]`. The arguments and the names tuple are taken off the stack before the callee is examined; the call then proceeds as TAIL_CALL describes, with the keywords bound by name.

---

### KOS_OP_TAIL_CALL_STAR
- **Style**: STAR. Arg = bitflags (1 = has *args, 2 = has **kwargs).
- **Stack**: Same layout as CALL_STAR: `[..., callable, pos, kwd]`. Pop order and the tuple coercion as CALL_STAR; the call then proceeds as TAIL_CALL describes, with `pos` and `kwd` spread.

---

### KOS_OP_TAIL_CALL_ATTR
- **Style**: EXTRA. Schema: CONST (attr name), COUNT (npos), NOARG, NOARG. One cache slot (attr resolution).
- **Stack**: Same layout as CALL_ATTR: `[..., obj, arg_0, ..., arg_{N-1}]`. `obj.name` resolves as CALL_ATTR describes, then the call is made in tail position: a Koskari function takes over the caller's frame rather than a new frame being pushed, and any other callable is called as TAIL_CALL describes.
- **Compiler form:** Produced by the processor's `promote_tail_calls` pass relabeling a tail-position CALL_ATTR in function-body code; never emitted by a special.

---

### KOS_OP_TAIL_CALL_ATTR_NAMES
- **Style**: EXTRA. Schema: CONST (attr name), CONST (kwarg names tuple), COUNT (npos), NOARG. One cache slot.
- **Stack**: Same layout as CALL_ATTR_NAMES. Tail semantics as KOS_OP_TAIL_CALL_ATTR: the caller's frame is reused rather than a new frame pushed.
- **Compiler form:** Promotion relabel of a tail-position CALL_ATTR_NAMES; never emitted by a special.

---

### KOS_OP_TAIL_CALL_ATTR_STAR
- **Style**: EXTRA. Schema: CONST (attr name), COUNT (opa bitflags: 1 = has *args, 2 = has **kwargs), NOARG, NOARG. One cache slot.
- **Stack**: Same layout and pop order as CALL_ATTR_STAR; the call is a tail call, so the caller's frame is reused rather than a new frame pushed.
- **Compiler form:** Promotion relabel of a tail-position CALL_ATTR_STAR; never emitted by a special.

---

### KOS_OP_TAIL_CALL_VAR
- **Style**: EXTRA. Schema: VAR (name cpool idx), COUNT (npos), NOARG, NOARG.
- **Stack**: Same layout as CALL_VAR: `[..., pos_0, ..., pos_{N-1}]`; the callable is loaded from the frame namespace by name, not on the stack. Tail semantics as KOS_OP_TAIL_CALL_ATTR: the caller's frame is reused rather than a new frame pushed.
- **Compiler form:** Promotion relabel of a tail-position CALL_VAR; convert_fast lowers it to TAIL_CALL_FAST when the name has a fast slot.

---

### KOS_OP_TAIL_CALL_VAR_NAMES
- **Style**: EXTRA. Schema: VAR (name), CONST (kwarg names tuple), COUNT (npos).
- **Stack**: Same layout as CALL_VAR_NAMES. Tail semantics as KOS_OP_TAIL_CALL_ATTR: the caller's frame is reused rather than a new frame pushed.
- **Compiler form:** Promotion relabel of a tail-position CALL_VAR_NAMES; lowers to TAIL_CALL_FAST_NAMES.

---

### KOS_OP_TAIL_CALL_VAR_STAR
- **Style**: EXTRA. Schema: VAR (name), STAR (bitflags), NOARG, NOARG.
- **Stack**: Same layout and pop order as CALL_VAR_STAR; the call is a tail call, so the caller's frame is reused rather than a new frame pushed.
- **Compiler form:** Promotion relabel of a tail-position CALL_VAR_STAR; lowers to TAIL_CALL_FAST_STAR.

---

### KOS_OP_TAIL_CALL_VAR_ATTR
- **Style**: EXTRA. Schema: VAR (name for obj), CONST (attr name), COUNT (npos), NOARG. One cache slot.
- **Stack**: Same layout as CALL_VAR_ATTR: args only; obj is loaded from the frame namespace by name (owned deref, released on every path). Tail semantics as KOS_OP_TAIL_CALL_ATTR: the caller's frame is reused rather than a new frame pushed.
- **Compiler form:** Promotion relabel of a tail-position CALL_VAR_ATTR; lowers to TAIL_CALL_FAST_ATTR.

---

### KOS_OP_TAIL_CALL_VAR_ATTR_NAMES
- **Style**: EXTRA. Schema: VAR (name for obj), CONST (attr name), CONST (kwarg names tuple), COUNT (npos). One cache slot.
- **Stack**: Same layout as CALL_VAR_ATTR_NAMES. Tail semantics as KOS_OP_TAIL_CALL_ATTR: the caller's frame is reused rather than a new frame pushed.
- **Compiler form:** Promotion relabel of a tail-position CALL_VAR_ATTR_NAMES; lowers to TAIL_CALL_FAST_ATTR_NAMES.

---

### KOS_OP_TAIL_CALL_VAR_ATTR_STAR
- **Style**: EXTRA. Schema: VAR (name for obj), CONST (attr name), COUNT (opa bitflags), NOARG. One cache slot.
- **Stack**: Same layout and pop order as CALL_VAR_ATTR_STAR; the call is a tail call, so the caller's frame is reused rather than a new frame pushed.
- **Compiler form:** Promotion relabel of a tail-position CALL_VAR_ATTR_STAR; lowers to TAIL_CALL_FAST_ATTR_STAR.

---

### KOS_OP_TAIL_CALL_FAST
- **Style**: EXTRA. Schema: FASTVAR (slot index), COUNT (npos), NOARG, NOARG.
- **Stack**: Same layout as CALL_FAST; the callable is loaded from the fast local slot (owned deref). Tail semantics as KOS_OP_TAIL_CALL_ATTR: the caller's frame is reused rather than a new frame pushed.
- **Compiler form:** Reached through promotion of CALL_VAR plus the fast lowering, or promotion of an already-lowered CALL_FAST; never emitted by a special.

---

### KOS_OP_TAIL_CALL_FAST_NAMES
- **Style**: EXTRA. Schema: FASTVAR (slot), CONST (kwarg names tuple), COUNT (npos).
- **Stack**: Same layout as CALL_FAST_NAMES. Tail semantics as KOS_OP_TAIL_CALL_ATTR: the caller's frame is reused rather than a new frame pushed.
- **Compiler form:** Fast tier of TAIL_CALL_VAR_NAMES; see [`KOS_OP_TAIL_CALL_FAST`](#kos_op_tail_call_fast).

---

### KOS_OP_TAIL_CALL_FAST_STAR
- **Style**: EXTRA. Schema: FASTVAR (slot), STAR (bitflags), NOARG, NOARG.
- **Stack**: Same layout and pop order as CALL_FAST_STAR; the call is a tail call, so the caller's frame is reused rather than a new frame pushed.
- **Compiler form:** Fast tier of TAIL_CALL_VAR_STAR; see [`KOS_OP_TAIL_CALL_FAST`](#kos_op_tail_call_fast).

---

### KOS_OP_TAIL_CALL_FAST_ATTR
- **Style**: EXTRA. Schema: FASTVAR (slot for obj), CONST (attr name), COUNT (npos), NOARG. One cache slot.
- **Stack**: Same layout as CALL_FAST_ATTR: args only; obj is loaded from the fast local slot (owned deref, released on every path). Tail semantics as KOS_OP_TAIL_CALL_ATTR: the caller's frame is reused rather than a new frame pushed.
- **Compiler form:** Fast tier of TAIL_CALL_VAR_ATTR; see [`KOS_OP_TAIL_CALL_FAST`](#kos_op_tail_call_fast).

---

### KOS_OP_TAIL_CALL_FAST_ATTR_NAMES
- **Style**: EXTRA. Schema: FASTVAR (slot for obj), CONST (attr name), CONST (kwarg names tuple), COUNT (npos). One cache slot.
- **Stack**: Same layout as CALL_FAST_ATTR_NAMES. Tail semantics as KOS_OP_TAIL_CALL_ATTR: the caller's frame is reused rather than a new frame pushed.
- **Compiler form:** Fast tier of TAIL_CALL_VAR_ATTR_NAMES; see [`KOS_OP_TAIL_CALL_FAST`](#kos_op_tail_call_fast).

---

### KOS_OP_TAIL_CALL_FAST_ATTR_STAR
- **Style**: EXTRA. Schema: FASTVAR (slot for obj), CONST (attr name), COUNT (opa bitflags), NOARG. One cache slot.
- **Stack**: Same layout and pop order as CALL_FAST_ATTR_STAR; the call is a tail call, so the caller's frame is reused rather than a new frame pushed.
- **Compiler form:** Fast tier of TAIL_CALL_VAR_ATTR_STAR; see [`KOS_OP_TAIL_CALL_FAST`](#kos_op_tail_call_fast).

---

## 18. Block control

### KOS_OP_PUSH_FOREACH

- **Style**: NOARG. Argument unused; break target is derived at runtime from the extent table (`pop_pc + 1`).
- **Stack**: Pops iterable (TOS); creates iterator; pushes the FOREACH node, which owns its segment. Region loop result starts unset. ITER_NEXT uses the node's iterator. Break target = `extent.pop_pc + 1` (instruction after the matching POP_FOREACH).
- **Pushes:** KOS_DYN_FOREACH (static extent KOS_EXT_FOREACH; node holds iterator and loop state).
- **Invariants**: A KOS_EXT_FOREACH extent must exist at the PUSH_FOREACH PC; otherwise RuntimeError. Extent kind KOS_EXT_FOREACH.
- **Compiler form:** Pre-compile arg = 0 (unused). Break target is not stored in the instruction; it is derived at runtime via `kos_extent_find(KOS_EXT_FOREACH)` -> `ext->pop_pc + 1`.

**Example:** for x in [1,2,3]: body. Stack at entry: [..., iterable]. Pool: 0 = [1,2,3].

| index | opcode       | arg | arg view | comment |
|------:|--------------|-----|----------|---------|
| 0     | CONST        | 0   | 0 ([1,2,3]) | stack: [..., iterable] |
| 1     | PUSH_FOREACH | 0   | 0        | pop iterable; push FOREACH region; break_pc = pop_pc+1 from extent table |
| 2     | ITER_NEXT    | 6   | 6 (-> 6) | next from region; else -> 6. stack: [..., x] |
| 3     | POP          | 1   | 1        | body: consume x |
| 4     | JUMP         | 2   | 2 (-> 2) | loop back |
| 5     | POP_FOREACH  | 0   | 0        | push loop result (None if unset) |
| 6     | LABEL        | --   | --        | break / exhausted (= pop_pc+1) |

---

### 18.1 Context managers (with / dyn-with)

Context managers (`with` / `dyn-with`) at the language level: [with](../specials/with.md), [dyn-with](../specials/dyn-with.md). Summary below.

### KOS_OP_PUSH_WITH
- **Style**: CONST. Arg = constant pool index of binding name (symbol/string) or sentinel for no binding.
- **Stack before:** `[..., mgr]`. **Stack after:** body runs in a fresh operand segment.
- **Behaviour:** Pop `mgr`; resolve and cache enter/exit; call enter (auto-await if needed); on success bind enter result if name given; push new segment; look up WITH extent at current PC, compute `resume_pc = ext->pop_pc + 1`; push `KOS_DYN_WITH` with `resume_pc` stored on the node. On enter failure, no node or block is installed; exception propagates.
- **Pushes:** KOS_DYN_WITH (single-shot: exits once on unwind or transfer, never re-entered). Node stores `resume_pc` (instruction after POP_WITH) for use when exit swallows an exception.
- **Invariants:** Node owns one operand segment; stack restoration is structural (segment pop), not height-mark. A WITH extent must exist at the PUSH_WITH PC; otherwise RuntimeError.

### KOS_OP_POP_WITH
- **Style**: NOARG (argument unused in current `opcodes.incl`).
- **Behaviour:** Store the body value on the node; call exit (no-exception form); auto-await if needed; pop the owned segment; pop dyn node; push the stored body value. When exit returns an awaitable: step via `kos_with_exit_start_await`; on sync completion (r==0) call `kos_complete_pending_with_exit` to consume the exit result and push the body value; on suspend returns **`EVAL_AWAIT(awaitable)`**; on resume, revive calls `kos_complete_pending_with_exit` so the with-expression value is the body's (exit return is never left on stack). On exception unwind (via no-region path or dyn_pop_top_with): call exit(exc_info); if swallow then clear exception, pop block, pop node, push `None`, set frame PC to `KosDynWith_RESUME_PC(node)` (the instruction after POP_WITH), resume; else pop block, pop node, re-raise.
- **Invariants:** Top of chain must be KOS_DYN_WITH. With-expression value is the body's value, or `None` when exit swallowed an exception; exit return is used only for swallow logic and is never the value seen by RETURN.
### KOS_OP_PUSH_DYN_WITH
- **Style**: CONST. Arg = constant pool index of binding name or sentinel for no binding (same as PUSH_WITH).
- **Stack / behaviour:** Same as PUSH_WITH except that the node is marked multi-shot (`KOS_DYN_WITH_SHOT_MULTI`) and the DYN_WITH extent supplies the pcs. The node participates in continuation transfer plans (wind/unwind on non-local transfer).
- **Pushes:** KOS_DYN_WITH, multi-shot.

### KOS_OP_POP_DYN_WITH
- **Style**: NOARG.
- **Behaviour:** Same as POP_WITH for normal and exception unwind. Pops the multi-shot KOS_DYN_WITH.

---
### KOS_OP_PUSH_COLLECT

- **Style**: CONST. Arg = constant pool index of NamespaceSpec.
- **Stack**: Does not consume the stack. Loads NamespaceSpec from constant pool at
  arg, creates CollectorNamespace, installs as frame ns, pushes KOS_DYN_COLLECT,
  and owns an operand segment; the body runs in it; expression results (e.g.
  None from compile/define) stay in the block and are discarded on POP_COLLECT.
- **Pushes:** KOS_DYN_COLLECT (static extent KOS_EXT_COLLECT; stores saved ns for
  restore on POP_COLLECT), with its owned segment for the body.
- **Invariants**: Extent kind KOS_EXT_COLLECT. Frame namespace must be KosNamespace.

---

### KOS_OP_POP_COLLECT

- **Style**: NOARG. Argument unused.
- **Stack**: Verifies dyn_top is KOS_DYN_COLLECT. Restores frame ns, drops the stack
  block (discarding body expression results), pops the dyn node, **pushes the
  collected namespace** onto the value stack. `[..., pos] -> [..., pos, ns]`.
- **Invariants**: Top of chain must be KOS_DYN_COLLECT.

---

### KOS_OP_BUILD_CLASS

- **Style**: NOARG. Argument unused.
- **Stack**: Expects `[..., pos, ns]` with TOS = ns, TOS-1 = pos. Pops ns, pos.
  pos = (name, bases) or (name, bases, metaclass). Builds the class via
  `kos_build_class` (namespace materialization, demand-wrap of slot-backed
  dunders, metaclass resolution -- plain `type` by default -- with
  `__prepare__`, merge, metaclass call); pushes resulting class.
- **Compiler form:** Build pos with BUILD_ARGS; use with_collector_namespace for
  PUSH_COLLECT/body/POP_COLLECT; then BUILD_CLASS. See [compile/build-class](../specials/compile-build-class.md).

**Example:** Empty class C. PUSH_COLLECT arg = constant pool index of nspec.

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 0     | CONST      | 0   | 0 ("C")  | name |
| 1     | CONST      | 1   | 1 (())   | bases |
| 2     | BUILD_ARGS | 2   | 2        | stack: [..., pos] |
| 3     | PUSH_COLLECT | 2 | 2 (nspec)| push collect scope; nspec from cpool |
| 4     | POP_COLLECT | 0 | 0        | drop block; pop; push ns |
| 5     | BUILD_CLASS| 0   | 0        | metaclass(name,bases,dict); push cls |
| 6     | RETURN     | 1   | 1        | |

---

### KOS_OP_PUSH_LOOP

- **Style**: NOARG. Argument unused; break target is derived at runtime from the extent table (`pop_pc + 1`). Continue target = instruction immediately following PUSH_LOOP (i.e. `CURRENT_PC` after the evaluator has advanced past the opcode).
- **Stack**: Pushes the LOOP node, which owns its segment. Break/continue use `break_pc` (derived at runtime) and `continue_pc` (next instruction after PUSH_LOOP). Initializes node result to `None`.
- **Pushes:** KOS_DYN_LOOP (static extent KOS_EXT_LOOP; with its own segment for the loop body).
- **Invariants**: A KOS_EXT_LOOP extent must exist at the PUSH_LOOP PC; otherwise RuntimeError. Extent kind KOS_EXT_LOOP.
- **Compiler form:** Pre-compile arg = 0 (unused). Break target is not stored in the instruction; it is derived at runtime via `kos_extent_find(KOS_EXT_LOOP)` -> `ext->pop_pc + 1`.

**Example:** while loop. Break target derived from extent table (pop_pc+1 = 6).

| index | opcode    | arg | arg view | comment |
|------:|-----------|-----|----------|---------|
| 0     | PUSH_LOOP | 0   | 0        | push LOOP region; break_pc = pop_pc+1 from extent table; continue_pc = 1 |
| 1     | CONST     | 0   | 0 (False)| condition |
| 2     | JUMP_FALSE| 5   | 5 (-> 5) | exit loop if false; jumps to POP_LOOP |
| 3     | ...         | ...   | ...        | body |
| 4     | JUMP      | 1   | 1 (-> 1) | continue (loop head) |
| 5     | POP_LOOP  | 0   | 0        | |
| 6     | LABEL     | --   | --        | break target (= pop_pc+1) |

---

### KOS_OP_PUSH_NS

- **Style**: EXTRA. Schema: CONST (cpool index of NamespaceSpec), CONST (cpool index of names tuple or None), NOARG, NOARG. Instruction `arg` is the extra-row index. Binding count is `len(names)` when names is a tuple, or `0` when names is `None`.
- **Constant pool**: NSPEC and NAMES are loaded from the pool; they are not placed on the stack by preceding `CONST` opcodes.
- **Stack**: If `opa == 0`, no stack operands for bindings. If `opa > 0`, stack is `[..., val_0, ..., val_{opa-1}]` with TOS = last value; names come from the extra row. The VM binds `names[i]` to `val[i]`, then drops `opa` stack slots.
- **Pushes:** KOS_DYN_NS (static extent KOS_EXT_NS; owns its operand segment; the prior namespace returns on POP_NS when the enclosing node is revealed).
- **Invariants**: Extent kind KOS_EXT_NS.

**Example:** Push namespace with no bound values (names = None). Operand row holds NSPEC and None; stack carries only the chain side effect until body runs.

| index | opcode  | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | PUSH_NS| 0   | extra row 0 (e.g. c# nspec, c# None) | push NS region; chain: [..., KosDynNs] |
| 1     | ...      | ...   | ...        | body |
| 2     | POP_NS | 0   | 0        | pop NS; restore previous namespace |

---

### KOS_OP_PUSH_TRY

- **Style**: LABEL. Handler targets in extent table.
- **Stack**: Pushes TRY node and a KosSegment (like PUSH_LOOP). Exception dispatch clears the segment before jumping to the handler. POP_TRY promotes the protected result (exactly one value) to the revealed segment and drops the region's segment. Node unwind (e.g. BREAK through TRY) also drops the segment.
- **Pushes:** KOS_DYN_TRY (static extent KOS_EXT_TRY; handler targets in extent descriptor) and KosSegment.
- **Invariants**: Extent kind KOS_EXT_TRY. Handler targets (except_pc, finally_pc, exit_pc) from extent descriptor.

**Example:** try/except. Extent table holds except_pc, finally_pc, exit_pc.

| index | opcode  | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | PUSH_TRY | 0  | 0        | push TRY region; extent table: except_pc, etc. |
| 1     | ...      | ...   | ...        | try body |
| 2     | POP_TRY | 0   | 0        | normal exit: pop TRY |
| 3     | JUMP   | 5   | 5 (-> 5)  | skip handler |
| 4     | LABEL  | --   | --        | except handler (except_pc) |
| 5     | LABEL  | --   | --        | exit |

---

### KOS_OP_PUSH_FINALLY

- **Style**: LABEL. Arg = handler/finally target.
- **Stack**: Pushes FINALLY node and a KosSegment (like PUSH_TRY). Exception dispatch clears the segment before jumping to the finally handler. POP_FINALLY promotes exactly one value to the revealed segment and drops the region's segment. Node unwind drops the segment when control passes through.
- **Pushes:** KOS_DYN_FINALLY (static extent KOS_EXT_FINALLY) and KosSegment.
- **Invariants**: Extent kind KOS_EXT_FINALLY.

**Example:** Push finally block. Arg = finally block pc (or extent holds it).

| index | opcode      | arg | arg view | comment |
|------:|-------------|-----|----------|---------|
| 0     | PUSH_FINALLY| 3   | 3 (-> 3) | push FINALLY; finally code at 3 |
| 1     | ...           | ...   | ...        | protected body |
| 2     | POP_FINALLY | 0   | 0        | normal exit |
| 3     | LABEL       | --   | --        | finally block |
| 4     | END_FINALLY | 0   | 0        | (conceptual) |

---

### KOS_OP_CLEAR_FINALLY
- **Style**: NOARG.
- **Stack**: No stack effect. Clears FINALLY region state on non-local exit; does not pop the region (POP_FINALLY does at target). Does not touch the segment (only disables the region); the segment is dropped by POP_FINALLY or unwind.
- **Example:** Exception or unwind path before transferring to a finally handler.

---

### KOS_OP_CLEAR_LOOP
- **Style**: NOARG.
- **Stack**: Clears the loop segment. Disables the top LOOP region: a BREAK or CONTINUE issued while it is disabled passes through it to the enclosing loop, and POP_LOOP takes the loop result from the region's stack. Emitted on normal exit from a while-loop body ahead of an else clause.
- **Invariants**: Top of chain must be KOS_DYN_LOOP. Use CLEAR_FOREACH for FOREACH extents.
- **Implementation**: `koskari/lib/opcode/CLEAR_LOOP.c`

---

### KOS_OP_CLEAR_FOREACH
- **Style**: NOARG.
- **Stack**: Clears the loop segment. Disables the top FOREACH region: a BREAK or CONTINUE issued while it is disabled passes through it to the enclosing loop, and POP_FOREACH takes the loop result from the region's stack. Emitted when a for-each iterator is exhausted naturally, ahead of an else clause.
- **Invariants**: Top of chain must be KOS_DYN_FOREACH. Use CLEAR_LOOP for LOOP extents.
- **Implementation**: `koskari/lib/opcode/CLEAR_FOREACH.c`

---

### KOS_OP_CLEAR_TRY
- **Style**: NOARG.
- **Stack**: No stack effect. Clears TRY region state on non-local exit; does not touch the segment; POP_TRY drops the segment at the target.
- **Invariants**: Used with structured exception and non-local control flow.

---

### KOS_OP_POP_TRY
- **Style**: NOARG. Argument unused (or 0).
- **Stack**: Promote one value: pop TOS from the try segment, pop the region (dropping its segment), and push the value onto the revealed segment. The segment must hold exactly one value. Clear exception state before pop.
- **Invariants**: Top of chain must be KOS_DYN_TRY.

---

### KOS_OP_POP_FINALLY
- **Style**: NOARG.
- **Stack**: Same structural behaviour as POP_TRY for the finally segment: promote one value, drop the segment, pop the region.
- **Invariants**: Top of chain must be KOS_DYN_FINALLY.

---

### KOS_OP_POP_LOOP
- **Style**: NOARG.
- **Stack**: Pop LOOP region and drop its segment; push the loop result. An enabled region delivers the result stored on the region; a region CLEAR_LOOP disabled ran its else clause, and the clause's value, the top of the region's stack, is the result.
- **Invariants**: Top of chain must be KOS_DYN_LOOP (paired with PUSH_LOOP).
- **Implementation**: `koskari/lib/opcode/POP_LOOP.c`

---

### KOS_OP_POP_FOREACH
- **Style**: NOARG.
- **Stack**: Pop FOREACH region and drop its segment; push the loop result. An enabled region delivers the result stored on the region, or `None` when no CONTINUE set one; a region CLEAR_FOREACH disabled ran its else clause, and the clause's value, the top of the region's stack, is the result.
- **Invariants**: Top of chain must be KOS_DYN_FOREACH (paired with PUSH_FOREACH).
- **Implementation**: `koskari/lib/opcode/POP_FOREACH.c`

---

### KOS_OP_POP_NS
- **Style**: NOARG.
- **Stack**: Pop the NS node; the revealed node's namespace becomes active, and any values remaining on the NS node's segment are delivered onto the revealed segment.

**Example:** Pool: N/A.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 2     | POP_NS | 0   | 0        | pop KosDynNs; restore saved namespace |

- **Invariants**: Top of chain must be KOS_DYN_NS.

---

## 19. Object protocol

Object protocol opcodes: single Python operation on TOS (and optionally below); pop operands, push result. All **Implemented** unless noted.

- **Comparisons and predicates**: OBJECT_COMPARE (CMPCOUNT) -- stack `[..., a1, a2, ... aN] -> [..., result]`, `N >= 2`, selector-keyed (KosCompare packs with the count); the fused OBJECT_COMPARE_VAR / OBJECT_COMPARE_FAST carry the ternary bounds idiom with no stack inputs.
- **Numeric**: OBJECT_ADD, OBJECT_SUBTRACT, OBJECT_MULTIPLY, OBJECT_POWER, OBJECT_FLOOR_DIVIDE, OBJECT_TRUE_DIVIDE, OBJECT_MODULO, OBJECT_* (bit ops, matrix multiply).
- **Numeric, in-place**: OBJECT_INPLACE_* (COUNT) -- stack `[..., a1, a2, ... aN] -> [..., result]`, `N >= 2`, reducing left-to-right via the `PyNumber_InPlace*` API (POWER is strictly binary); used by the inplace-item and inplace-attr special forms.
- **General**: OBJECT_ITER, OBJECT_LEN, OBJECT_NOT, OBJECT_REPR, OBJECT_STR, OBJECT_FORMAT, OBJECT_FORMAT_CONST, OBJECT_TYPE, OBJECT_DIR, OBJECT_VARS, OBJECT_TRUTH, OBJECT_DIVMOD, OBJECT_BIT_INVERT.

Each `OBJECT_*` opcode in `opcodes.incl` has its own subsection below.

### KOS_OP_OBJECT_COMPARE
- **Style**: CMPCOUNT. Arg packs a KosCompare selector (high byte) with
  the reducing count (low byte).
- **Semantics**: Chain-compare `N` stack values under the selector and
  push the result (`N >= 2`; raises `TypeError` otherwise). The
  richcompare six (LT/LE/EQ/NE/GT/GE, ordinals 0-5 == Py_LT..Py_GE)
  chain left-to-right and push the last richcompare result. The extended
  selectors push a bool: IS / IS_NOT / IN / NOT_IN are strictly binary;
  IS_INSTANCE / IS_SUBCLASS test the first operand against each operand
  after it, a tuple operand opened one level, and hold on the first
  match.
- **Compiler form:** Emitted by the comparison, predicate, and type-check
  specials with a `(selector, count)` pair, merged to the packed uint16
  at normalize. When immediately followed by `JUMP_FALSE`, `ByteCodeProcessor`
  fuses the pair into [CHECK_COMPARE](#kos_op_check_compare).

### KOS_OP_OBJECT_COMPARE_VAR
- **Style**: EXTRA (CMP, CONST, VAR, CONST). Row = bare selector, low bound
  (pool), middle operand name, high bound (pool). Schema-static slots: no source
  bits, the shape is the opcode.
- **Stack**: `[...] -> [..., result]`. The ternary bounds idiom (`const < x <
  const`) with the middle operand resolved by name: chain-compare the three
  operands under the selector and push the chain result.
- **Invariants**: Selector is a valid KosCompare ordinal (richcompare
  chain semantics; extended selectors are never emitted at arity 3).
- **Compiler form:** Produced by `ByteCodeProcessor.fuse_feeders` when the exact CONST / VAR / CONST feeder shape meets an arity-3 comparison. In value position the OBJECT_COMPARE quartet collapses to this row alone; in branch position the arity-3 CHECK_COMPARE cannot also carry its label (five fields), so it splits into this row plus `JUMP_FALSE`. Lowers to OBJECT_COMPARE_FAST through the generic schema-driven `convert_fast` path.
- **Implementation**: `koskari/lib/opcode/OBJECT_COMPARE_VAR.c` (cold handler)
- **Covered by**: `tests/compile/test_fuse_feeders.py`,
  `tests/lib/opcode/test_compare.py`,
  `tests/reproducers/test_compare_fusion.py`

**Example:** `(< 0 x 100)` in value position -- one dispatch.

| index | opcode              | arg | arg view                     | comment |
|------:|---------------------|-----|------------------------------|---------|
| 0     | OBJECT_COMPARE_VAR | 0 | (LT, c#0 (0), v#1 ('x'), c#2 (100)) | stack: [..., bool] |

---

### KOS_OP_OBJECT_COMPARE_FAST
- **Style**: EXTRA (CMP, CONST, FASTVAR, CONST). Row = bare selector, low bound
  (pool), middle operand fast slot, high bound (pool). Schema-static slots: no
  source bits, the shape is the opcode.
- **Stack**: `[...] -> [..., result]`. The ternary bounds idiom
  (`const < x < const`): chain-compare the three operands under the
  selector and push the chain result (for JUMP_FALSE / JUMP_TRUE or
  value use).
- **Invariants**: Selector is a valid KosCompare ordinal (richcompare
  chain semantics; extended selectors are never emitted at arity 3).
- **Compiler form:** Post-compile only: the generic `convert_fast` lowering of OBJECT_COMPARE_VAR when the middle name has a fast slot.
- **Implementation**: `koskari/lib/opcode/OBJECT_COMPARE_FAST.c`
- **Covered by**: `tests/compile/test_fuse_feeders.py`,
  `tests/lib/opcode/test_compare.py`,
  `tests/reproducers/test_compare_fusion.py`

**Example:** `(< 0 x 100)` in value position -- one dispatch.

| index | opcode              | arg | arg view                     | comment |
|------:|---------------------|-----|------------------------------|---------|
| 0     | OBJECT_COMPARE_FAST | 0 | (LT, c#0 (0), fast 0 ("x"), c#1 (100)) | stack: [..., bool] |

---

### KOS_OP_OBJECT_OPERAND

- **Style**: OPCOUNT. Arg packs a KosOperand selector (high byte) with the
  reduce count (low byte).
- **Stack**: `[..., a1, ..., aN] -> [..., result]`. Reduce the N operands
  left-to-right under the operator (`values[0]` is the seed), via
  `kos_operand_reduce` (`operator.c`) with its two-operand PyLong fast paths.
- **Semantics**: The hottest opcode by dispatch count and by time. The
  handler is a prologue only: it tail-calls the body the site's slot elected
  (`eval/eval_electors.c`, int and float fast paths with their own guards);
  an empty slot or an ungated site runs the electing function once, which
  settles the slot.
- **Compiler form:** Emitted by the operator specials in
  `compile/specials/object.py`; the selector is a KosOperand name.
- **Implementation**: `koskari/lib/opcode/OBJECT_OPERAND.c`

### KOS_OP_OBJECT_INPLACE

- **Style**: OPCOUNT. Arg packs a KosOperand selector (high byte) with the
  reduce count (low byte).
- **Stack**: `[..., a1, ..., aN] -> [..., result]`. In-place reduce the N
  operands left-to-right (`values[0]` is the accumulator seed) via
  `kos_inplace_reduce` (`operator.c`), the `PyNumber_InPlace*` protocol family.
  The consolidated replacement for the old per-operator `OBJECT_INPLACE_*`
  family.
- **Semantics**: The stack in-place operation behind the inplace-item and
  inplace-attr special forms; elected like OBJECT_OPERAND.
- **Implementation**: `koskari/lib/opcode/OBJECT_INPLACE.c`

### KOS_OP_OBJECT_BIT_INVERT
- **Style**: NOARG.
- **Semantics**: See [section 19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_BIT_INVERT.c`

### KOS_OP_OBJECT_DIR
- **Style**: NOARG.
- **Semantics**: See [section 19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_DIR.c`

### KOS_OP_OBJECT_DIVMOD
- **Style**: COUNT. Arg = operand count; only 2 is accepted.
- **Semantics**: See [section 19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_DIVMOD.c`

### KOS_OP_OBJECT_ITER
- **Style**: NOARG.
- **Semantics**: See [section 19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_ITER.c`

### KOS_OP_VAR_ITER
- **Style**: VAR. Arg = constant index of a name.
- **Stack**: `[...] -> [..., iterator]`. Fused VAR + OBJECT_ITER: resolve the name in the current frame's namespace and push its iterator, acquired uniformly over both protocols (`kos_iter_acquire`).
- **Compiler form:** Produced by `ByteCodeProcessor.fuse_feeders` when OBJECT_ITER is fed by a bare VAR load. Lowers to FAST_ITER through the simple `convert_fast` path.
- **Implementation**: `koskari/lib/opcode/VAR_ITER.c`
- **Covered by**: `tests/compile/test_fuse_feeders.py`,
  `tests/lib/opcode/test_object_unary.py`

### KOS_OP_FAST_ITER
- **Style**: FASTVAR. Arg = slot index.
- **Stack**: As VAR_ITER, with the iterable loaded from its fast slot.
- **Compiler form:** Post-compile only: the `convert_fast` lowering of VAR_ITER when the name has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITER.c`
- **Covered by**: `tests/compile/test_fuse_feeders.py`,
  `tests/lib/opcode/test_object_unary.py`

### KOS_OP_OBJECT_LEN
- **Style**: NOARG.
- **Semantics**: See [section 19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_LEN.c`

### KOS_OP_OBJECT_NOT
- **Style**: NOARG.
- **Semantics**: See [section 19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_NOT.c`

### KOS_OP_OBJECT_REPR
- **Style**: NOARG.
- **Semantics**: See [section 19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_REPR.c`

### KOS_OP_OBJECT_STR
- **Style**: COUNT.
- **Semantics**: See [section 19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_STR.c`

### KOS_OP_OBJECT_FORMAT
- **Style**: COUNT.
- **Semantics**: format operation. Arg must be 1 or 2.
  - arg=1: pop object, call `PyObject_Format(obj, NULL)`.
  - arg=2: pop rules then object, call `PyObject_Format(obj, rules)`.
  Stack effect: `[..., obj] -> [..., str]` or `[..., obj, rules] -> [..., str]`.
- **Implementation**: `koskari/lib/opcode/OBJECT_FORMAT.c`

### KOS_OP_OBJECT_FORMAT_CONST
- **Style**: CONST.
- **Semantics**: format operation with constant rules from cpool.
  Pop object, fetch format rules string from constant pool arg, call
  `PyObject_Format(obj, rules_const)`, push resulting string.
  Stack effect: `[..., obj] -> [..., str]`.
- **Implementation**: `koskari/lib/opcode/OBJECT_FORMAT_CONST.c`

### KOS_OP_VAR_FORMAT
- **Style**: VAR. Arg = constant index of a name.
- **Stack**: `[...] -> [..., str]`. Fused VAR + OBJECT_FORMAT (arg=1): resolve the name in the current frame's namespace and push `PyObject_Format(obj, NULL)`.
- **Compiler form:** Produced by `ByteCodeProcessor.fuse_feeders` when OBJECT_FORMAT (arg=1) is fed by a bare VAR load. Lowers to FAST_FORMAT through the simple `convert_fast` path.
- **Implementation**: `koskari/lib/opcode/VAR_FORMAT.c`

### KOS_OP_VAR_FORMAT_CONST
- **Style**: EXTRA (VAR, CONST). Row = obj name (pool), rules string (pool).
- **Stack**: `[...] -> [..., str]`. Fused VAR + OBJECT_FORMAT_CONST: resolve the name in the current frame's namespace and push `PyObject_Format(obj, rules)`.
- **Compiler form:** Produced by `ByteCodeProcessor.fuse_feeders` when OBJECT_FORMAT_CONST is fed by a bare VAR load. Lowers to FAST_FORMAT_CONST through the generic schema-driven `convert_fast` path.
- **Implementation**: `koskari/lib/opcode/VAR_FORMAT_CONST.c`

### KOS_OP_RETURN_VAR
- **Style**: VAR. Arg = pool index of the name.
- **Stack**: `[...] -> [...]` (no stack effect). Resolves the name in the current frame's namespace, raising `NameError` when it is unbound as **`KOS_OP_VAR`** would, and returns the value exactly as **`KOS_OP_RETURN`** **1** returns a popped one.
- **Compiler form:** Produced by `ByteCodeProcessor.fuse_feeders` when **`KOS_OP_RETURN`** **1** is fed by a bare **`KOS_OP_VAR`**. Lowers to **`KOS_OP_RETURN_FAST`** through the generic schema-driven `convert_fast` path.
- **Implementation**: `koskari/lib/opcode/RETURN_VAR.c`

### KOS_OP_YIELD_VAR
- **Style**: VAR. Arg = pool index of the name.
- **Stack**: `[...] -> [...]` (no stack effect). Resolves the name in the current frame's namespace, raising `NameError` when it is unbound as **`KOS_OP_VAR`** would, and yields the value exactly as **`KOS_OP_YIELD`** **1** yields a popped one. Marks the code as a generator.
- **Compiler form:** Produced by `ByteCodeProcessor.fuse_feeders` when **`KOS_OP_YIELD`** **1** is fed by a bare **`KOS_OP_VAR`**. Lowers to **`KOS_OP_YIELD_FAST`** through the generic schema-driven `convert_fast` path.
- **Implementation**: `koskari/lib/opcode/YIELD_VAR.c`

### KOS_OP_FAST_FORMAT
- **Style**: FASTVAR. Arg = slot index.
- **Stack**: As VAR_FORMAT, with the object loaded from its fast slot.
- **Compiler form:** Post-compile only: the `convert_fast` lowering of VAR_FORMAT when the name has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_FORMAT.c`

### KOS_OP_FAST_FORMAT_CONST
- **Style**: EXTRA (FASTVAR, CONST). Row = obj slot, rules string (pool).
- **Stack**: As VAR_FORMAT_CONST, with the object loaded from its fast slot.
- **Compiler form:** Post-compile only: the `convert_fast` lowering of VAR_FORMAT_CONST when the name has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_FORMAT_CONST.c`

### KOS_OP_RETURN_FAST
- **Style**: FASTVAR. Arg = fast slot index.
- **Stack**: `[...] -> [...]` (no stack effect). As **`KOS_OP_RETURN_VAR`**, with the value read from the fast slot; an empty slot raises `NameError` as **`KOS_OP_FAST`** would.
- **Compiler form:** Post-compile only: the `convert_fast` lowering of RETURN_VAR when the name has a fast slot.
- **Implementation**: `koskari/lib/opcode/RETURN_FAST.c`

### KOS_OP_YIELD_FAST
- **Style**: FASTVAR. Arg = fast slot index.
- **Stack**: `[...] -> [...]` (no stack effect). As **`KOS_OP_YIELD_VAR`**, with the value read from the fast slot; an empty slot raises `NameError` as **`KOS_OP_FAST`** would. Marks the code as a generator.
- **Compiler form:** Post-compile only: the `convert_fast` lowering of YIELD_VAR when the name has a fast slot.
- **Implementation**: `koskari/lib/opcode/YIELD_FAST.c`

### KOS_OP_OBJECT_TRUTH
- **Style**: NOARG.
- **Semantics**: See [section 19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_TRUTH.c`

### KOS_OP_OBJECT_TYPE
- **Style**: NOARG.
- **Semantics**: See [section 19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_TYPE.c`

### KOS_OP_OBJECT_VARS
- **Style**: NOARG.
- **Semantics**: See [section 19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_VARS.c`

### 19.1 In-place variable update (VAR_INPLACE / FAST_INPLACE and _CONST forms)

Augmented assignment: fetch the current value from a name (VAR) or fast slot
(FAST), apply the in-place operator with the rhs, store the result back. No
value is pushed (the form evaluates to None). The operator is a KosOperand
selector dispatched through `kos_inplace_reduce` (`operator.c`), the same
selector set as OBJECT_OPERAND / OBJECT_INPLACE.

**VAR_INPLACE** -- EXTRA (OPERAND, VAR). Row a0 is the bare operator
selector (one rhs popped from the stack); a1 = constant pool index of the
name. Stack: `[..., rhs] -> [...]`.

**FAST_INPLACE** -- EXTRA (OPERAND, FASTVAR). As above with a fast
slot. Produced by `convert_fast` lowering of the VAR form.

**VAR_INPLACE_CONST** -- EXTRA (OPERAND, VAR, CONST). The compressed
single-operand form with a constant rhs: a0 is a *bare* KosOperand selector
(OPERAND kind), a1 the name, a2 the pooled operand. **Completely
stack-free**: no push, no pop. Produced by `ByteCodeProcessor` fusing a
prior CONST into a VAR_INPLACE --
the `(set! i (+ i 1))` / `incr!` loop-tail idiom. Cold handler.

**FAST_INPLACE_CONST** -- EXTRA (OPERAND, FASTVAR, CONST). The
`convert_fast` lowering of the above; the hot form. A counted loop's
back-edge increment is this single dispatch.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 6 | FAST_INPLACE_CONST | 4 | (ADD, fast 1 ("i"), c#1 (1), 0) | i += 1, stack-free |

## 20. Delimited continuations

Delimited continuations use a block-based layout: PUSH_PROMPT establishes the
reset and owns the operand segment for the delimited region; PUSH_SHIFT and
POP_SHIFT delimit the shift block (handler); the continuation is the code
between POP_SHIFT and POP_PROMPT.

### KOS_OP_PUSH_PROMPT

- **Style**: CONST. Arg = prompt_id (constant pool index, post-compile).
- **Stack**: The prompt owns a fresh operand segment (so the reset body has an isolated stack), resolves the PROMPT extent at the push site and records the region's closing pc on the node (POP_SHIFT resumes at that pc + 1), then pushes the prompt node onto the dynamic chain.
- **Pushes:** KOS_DYN_PROMPT (static extent KOS_EXT_PROMPT; PUSH_SHIFT captures up to this prompt). PROMPT owns the segment for the delimited region; SHIFT does not create one.
- **Invariants**: PUSH_SHIFT captures up to this prompt. An exception unwinding past a PROMPT drops the segment and unlinks the node in one step.
- **Compiler form:** Pre-compile arg may be prompt id value or symbol; post-compile arg = pool index.

**Example:** reset (push prompt). Pool: 0 = prompt_id.

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 0     | PUSH_PROMPT| 0   | 0 (pid)  | push prompt (owning its segment) onto chain |
| 1     | ...          | ...   | ...        | body before shift (continuation prefix) |
| 2     | PUSH_SHIFT | 0   | 0 (pid)  | capture k; enter shift block with k on stack |
| 3     | ...          | ...   | ...        | shift body (k available) |
| 4     | POP_SHIFT  | 0   | 0        | return from shift without invoking |
| 5     | ...          | ...   | ...        | continuation (body after shift) |
| 6     | POP_PROMPT | 0   | 0        | drop segment; pop prompt |

---

### KOS_OP_PUSH_SHIFT

- **Style**: CONST. Arg = prompt_id (constant pool index, post-compile).
- **Stack**: Captures the continuation: one walk from the top of the dynamic chain locates the nearest matching prompt while offering handle(SHIFT) to each node crossed (a generator boundary denies). The captured continuation is the delimited region with the shift expression as its hole -- a cloned chain segment, so a shift hosted in a callee captures through the calling frames up to the prompt. Pushes a SHIFT marker recording the captured prompt's instance, pushes the captured continuation on stack, and enters the shift block in place; no unwinding happens at capture. SHIFT owns the segment the shift body's operands go on; the PROMPT's segment keeps the region's pending operands for the continuation to resume over.
- **Pushes:** KOS_DYN_SHIFT (bookmark for the shift body: owns its operand segment, no handlers).
- **Invariants**: Extent KOS_EXT_SHIFT pairs with POP_SHIFT. Stack block is owned by PROMPT, not SHIFT.
- **Errors**: A walk that exhausts the chain without meeting a matching prompt raises `KoskariPromptNotFound`; a node that denies handle(SHIFT) (a generator boundary) raises `KoskariPromptUnreachable` with the denying boundary recorded. Both carry `prompt_id` and `operation`.
- **Compiler form:** Pre-compile arg may be prompt id value/symbol; post-compile arg = pool index.

**Example:** shift k: body. Capture k, run shift body with k on stack. Pool: 0 = prompt_id.

| index | opcode    | arg | arg view | comment |
|------:|-----------|-----|----------|---------|
| 2     | PUSH_SHIFT| 0   | 0 (pid)  | capture k in place; push SHIFT marker; enter shift block with k on stack |

---

### KOS_OP_POP_SHIFT

- **Style**: NOARG.
- **Stack**: Pops TOS as result and resolves the delimiter by the prompt instance the SHIFT recorded at capture. For a live reset: builds a transfer plan that unwinds through the SHIFT and PROMPT (dropping both and the PROMPT's segment), injects the result onto the outer frame's stack, and resumes at the closing pc the prompt recorded at push, + 1. For an invocation prompt (created by invoking a captured k; no POP_PROMPT pairs with it): the result is the value of that k-invocation, delivered at the invoke site through the paired DELIM bookmark.
- **Invariants**: Must be inside a PUSH_SHIFT extent (parent is PROMPT extent). The segment owned by the PROMPT is dropped as part of the TRUNCATE_AND_INJECT step; the injected value lands on the frame's stack below the former block boundary.
- **Compiler form:** No argument.

**Example:** Shift handler returns value without invoking k.

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 4     | POP_SHIFT| 0   | 0        | pop result; unwind through SHIFT+PROMPT; drop segment; inject result onto outer stack |

---

### KOS_OP_POP_PROMPT

- **Style**: NOARG.
- **Stack**: Normal case (no active continuation return): pops TOS as the reset result, drops the PROMPT's segment, unlinks the prompt from the chain, and pushes the result onto the parent stack. Invoked-continuation return case: the walk down from the current node finds a **`KOS_DYN_DELIM`** bookmark (mode **`POP_PROMPT`** for a grafted re-invoke, **`INPLACE`** for an in-place first invoke), passing invocation prompts and stopping at a live prompt; it pops TOS as result, unwinds to the delim, delivers the result at the invoke site per the delim's return metadata (an INPLACE delim restores the displaced frame state and re-links the suspended handler first), and resumes there.
- **Invariants**: Top of chain must be the matching prompt when POP_PROMPT executes. When returning from an invoked delimited continuation (return path recorded on **`KOS_DYN_DELIM`**), POP_PROMPT transfers control back to the call site with the result value instead of exiting normally. Exactly one value is consumed from the PROMPT's segment and one is injected to the outer stack.
- **Push-return model**: Delimited invoke uses PUSH (graft frames onto current chain). When the continuation runs and hits POP_PROMPT, it consults the **`KOS_DYN_DELIM`** marker installed for that invoke and resumes at the call site with the value on stack.
- **Exception propagation**: If an exception escapes past the PROMPT during unwinding, the unwind drops the segment and unlinks the PROMPT, then continues. No POP_PROMPT opcode executes in this path.

**Example:** End of reset body.

| index | opcode    | arg | arg view | comment |
|------:|-----------|-----|----------|---------|
| 6     | POP_PROMPT| 0   | 0        | pop result; drop segment; pop prompt; push result to outer stack |

---

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
- **Example:** (call/cc (lambda (k) (k 42))) -> 42. Body captures k, pushes 42,
  CALL 1 invokes k(42); that jumps to POP_CC (index 4) with stack [..., 42];
  POP_CC runs; execution continues at 5 (resume), RETURN 1 yields 42.

| index | opcode     | arg | arg view   | comment |
|------:|------------|-----|------------|---------|
| 0     | PUSH_CC    | 0   | 0          | push CC block; resume at 5 (= pop_pc+1 from block table) |
| 1     | CAPTURE_CC | 0   | 0          | stack before: [...]; after: [..., continuation] |
| 2     | CONST      | 0   | 0 (42)     | stack: [..., continuation, 42] |
| 3     | CALL       | 1   | 1          | invoke continuation with one arg -> jump to POP_CC (4) with [..., 42] |
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

- **Style**: COUNT, 0 or 1.
- **Stack**: Captures the continuation of the CC dynamic extent (from current pc
  until the extent is exited at POP_CC); pushes it (KosPyContinuation) onto
  stack. With arg 0 (`let/cc`) the stack effect is `[...] -> [..., continuation]`.
  With arg 1 (`call/cc`) the callee on TOS is set aside for the capture and put
  back beneath the continuation, `[..., fn] -> [..., fn, continuation]`, so the
  captured stack holds the form's enclosing operands and not the callee; the
  CALL 1 that follows applies fn to the continuation. Invoking the continuation
  is a non-local jump to the POP_CC opcode with a value pushed onto the stack.
- **Example:** See the PUSH_CC disassembly table: at index 1, CAPTURE_CC pushes
  the continuation. Invoking it (e.g. CALL 1 with 42) jumps to POP_CC (index 4)
  with stack [..., 42]; POP_CC runs; execution continues at index 5 (resume).

## 22. Dynamic wind

Unified extent wind/unwind uses `kos_extent_run_wind` and `kos_extent_run_unwind`,
which dispatch by dynamic-chain node kind. See [compile/capture-cc](../specials/compile-capture-cc.md)
and [compile/dyn-wind](../specials/compile-dyn-wind.md).

### KOS_OP_PUSH_WIND

- **Style**: COUNT. Arg = 1 (before only) or 2 (both before and after).
- **Stack**: For arg=2: `[..., before, after]` (before below, after above). Pops thunks; each must be callable or nil. For arg=1: `[..., before]`. Pushes KOS_DYN_WIND extent; runs before thunk on entry (via normal call path; may return KOS_DYN_UNWIND_ASYNC_STARTED).
- **Pushes:** KOS_DYN_WIND (static extent KOS_EXT_WIND).
- **Example:** See extent-wind-unwind.md; thunks are invoked via kos_callable_call.

---

### KOS_OP_POP_WIND

- **Style**: NOARG.
- **Stack**: Pops TOS (the body result) into the wind node, pops wind from the dynamic chain, and runs the after thunk (if entered and non-null) via kos_dyn_wind_unwind; the body result is delivered to the revealed segment when the thunk completes (or immediately when there is none). May return KOS_DYN_UNWIND_ASYNC_STARTED when the thunk pushes a frame.
- **Invariants**: Dyn top must be KOS_DYN_WIND; otherwise RuntimeError.

## 23. Parameterize extent and parameter reads

### KOS_OP_PUSH_PARAM

- **Style**: CONST. Arg = constant pool index of names tuple `(name_0, name_1, ...)`.
- **Stack**: `[..., val_0, val_1, ..., val_{n-1}]` -> `[]`. Values consumed; names come from constant.
- **Pushes:** KOS_DYN_PARAM (extent kind KOS_EXT_PARAM; owns its operand segment). Builds dict from names and values, enters param extent.
- **Invariants**: Constant at arg must be a tuple of names; stack must have at least `len(names)` values.
- **Example:** `(parameterize ((x 1) (y 2)) body)` compiles to push values 1, 2, then PUSH_PARAM with names `("x", "y")`.

| index | opcode     | arg | arg view     | comment                    |
|------:|------------|-----|--------------|----------------------------|
| 0     | CONST      | 1   | 1 (1)        | push 1                     |
| 1     | CONST      | 2   | 2 (2)        | push 2                     |
| 2     | PUSH_PARAM | 0   | 0 (names)    | pop 2 values, push param   |
| 3     | ...          | ...   | ...            | body                       |
| N     | POP_PARAM  | 0   | 0            | pop param extent           |

---

### KOS_OP_POP_PARAM

- **Style**: NOARG.
- **Stack**: No change.
- **Pops:** KOS_DYN_PARAM from dynamic chain. Restores the cells the extent displaced in the context's shadow table. The PARAM node owns its operand segment; any values remaining on it are delivered onto the revealed segment, so the observable stack effect is unchanged.
- **Invariants**: Dyn top must be KOS_DYN_PARAM; otherwise RuntimeError.
- **Example:** See PUSH_PARAM table.

---

### KOS_OP_PARAM

- **Style**: EXTRA. Schema CONST (parameter name), COUNT (0 or 1 stacked defaults).
- **Stack**: `[]` -> `[value]` at count 0; `[default]` -> `[value]` at count 1.
- **Resolves:** the enclosing parameterize extent (the context's shadow table), then the root scope of the context's domain. Writes to neither, and creates no entry on a miss.
- **Invariants**: On a miss at count 0, raises NameError. At count 1 the stacked default answers a miss, and is dropped on a hit.
- **Example:** `(parameterq x)` compiles to PARAM `("x", 0)`; `(parameterq x (compute))` compiles the expression, then PARAM `("x", 1)`.

| index | opcode | arg | arg view      | comment                       |
|------:|--------|-----|---------------|-------------------------------|
| 0     | CONST  | 0   | "fallback"    | push the default              |
| 1     | PARAM  | 0   | ("x", 1)      | resolve x, else keep default  |

---

### KOS_OP_PARAM_CONST

- **Style**: EXTRA. Schema CONST (parameter name), CONST (default value).
- **Stack**: `[]` -> `[value]`.
- **Resolves:** as PARAM, with the default coming from the constant pool rather than the stack. Never raises for an unbound name.
- **Invariants**: Both constants must be present in the pool.
- **Example:** `(parameterq x 42)` compiles to PARAM_CONST `("x", 42)`.

## 24. Sentinel

### KOS_OP_THEEND

- **Style**: NOARG.
- **Stack**: Unreachable for normal execution.
- **Invariants**: If execution reaches THEEND, the VM raises RuntimeError. THEEND must always be last in opcodes.incl.

**Example:** Sentinel at end of code; no legitimate execution path reaches it.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | RETURN | 1   | 1        | normal exit |
| 1     | THEEND | 0   | 0        | unreachable; VM raises if pc lands here |

## Per-opcode pages

```{toctree}
:maxdepth: 1

opcodes/index
```
