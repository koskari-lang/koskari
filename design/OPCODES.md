# Koskari opcodes — reference

Authoritative technical reference for Koskari bytecode opcodes. The **opcode set**
and their order are defined in `koskari/lib/opcodes.incl`; any new opcode must be
added there first, then documented here. This document defines semantics and
invariants; the evaluator and compiler are expected to follow it.

**Implementation status:** The opcode set is defined in `koskari/lib/opcodes.incl`.
This document is the **authoritative reference** for which of those opcodes exist
in shipped bytecode and whether each is implemented, a stub, or has no runtime.
See §1.6. Other design docs should refer here instead of duplicating status.

**See also:** [interpreter/bytecode.md](interpreter/bytecode.md) (format, layout,
metadata tables), [interpreter/execution-invariants.md](interpreter/execution-invariants.md)
(execution model), [archive/bytecode-metadata-tables.md](archive/bytecode-metadata-tables.md)
(extents, jump locality), [EVALUATOR.md](EVALUATOR.md)
(VM structures, C API), [EVALUATOR.md](EVALUATOR.md) (eval loop, post-opcode
delivery, **`KosOpOutcome`**), [interpreter/with-dyn-with.md](interpreter/with-dyn-with.md)
(context managers).


## 1. Overview and conventions

### 1.1 How to use this document

- **VM / evaluator implementers:** Per-opcode stack effect, invariants, and
  post-compile disassembly tables. Section 1.3 (global invariants), §1.9 (BIND vs
  STORE—do not confuse them), and each opcode's Invariants bullets are the
  **contract**: the evaluator must preserve them; violations are undefined
  behaviour (or asserted in debug builds).
- **Compiler writers:** Section 1.7 (pre-compile vs post-compile), §1.8
  (`Compiler.compress` peephole rules), **Compiler form** lines, and pre-compile
  list-of-pairs examples. Ensure emitted code satisfies the invariants documented
  per opcode.
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
  and [archive/bytecode-metadata-tables.md](archive/bytecode-metadata-tables.md).

### 1.3 Glossary

- **BIND** (verb): Establish or replace the name→ref mapping. "I will now use this
  reference to resolve the given name." Opposed to STORE. See §1.9.
- **STORE** (verb): Write a value through the reference that already resolves a
  name. "I will now store this value in the given name." Opposed to BIND. See §1.9.
- **pc**: Instruction index (program counter) into the code array.
- **TOS**: Top of stack; rightmost element in stack notation.
- **cpool**: Constant pool; the read-only array of constants in a KosCode (`cpool[i]`).
- **ATTR** (field kind): A constant pool index whose constant is an attribute
  name. Declared on the name field of every `*_ATTR` row in `opcodes.incl`;
  finalize requires the constant to be a str, since the method cache and the
  type lookup read it as one. Otherwise handled as CONST.
- **RAW** (field kind): An EXTRA row field holding a number the
  instruction interprets itself, such as `CHECK_COMPARE`'s negate
  flag or a matcher instruction's stack depth, width, count, flag or
  core type ordinal. Finalize leaves it unchanged.
- **extent**: A statically described block (stored in the code object's extent
  table) with kind `KosExtentKind` (KOS_EXT_TRY, KOS_EXT_LOOP, KOS_EXT_FOREACH,
  KOS_EXT_PROMPT, KOS_EXT_NS, KOS_EXT_COLLECT, etc.), enter/exit pcs, and
  kind-specific targets; see [archive/bytecode-metadata-tables.md](archive/bytecode-metadata-tables.md).
- **dynamic chain / chain node**: At runtime, the evaluator maintains a chain of
  nodes (`KosDynKind`: KOS_DYN_FRAME, KOS_DYN_PROMPT, KOS_DYN_DELIM,
  KOS_DYN_WIND, KOS_DYN_TRY, KOS_DYN_FINALLY, KOS_DYN_LOOP, KOS_DYN_FOREACH,
  KOS_DYN_NS, KOS_DYN_COLLECT, KOS_DYN_CC, KOS_DYN_PARAM,
  KOS_DYN_WITH, KOS_DYN_SHIFT, KOS_DYN_GEN). PUSH_* opcodes push a node onto this chain;
  POP_* opcodes pop it.
  Each opcode section that pushes onto the chain states **Pushes:** the chain node
  type and, when relevant, the static extent kind.
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
| **opcode** | Opcode name (e.g. `KOS_OP_CHECK_COMPARE`). |
| **arg** | Raw argument value (uint16). |
| **arg view** | Decoded argument for readability (e.g. constant as `index (repr)`, branch as `N (-> N)`). |
| **comment** | Stack state before the instruction and/or control-flow or stack effect. |

Long comments (stack, branch conditions) may span the cell; keep index and opcode compact so the table stays scannable.

### 1.5 Global invariants

Unless specified otherwise for an opcode:

- Execution obeys the invariants in [interpreter/execution-invariants.md](interpreter/execution-invariants.md).
- Extent and location metadata are as in [archive/bytecode-metadata-tables.md](archive/bytecode-metadata-tables.md).
- **Jump locality**: Control-flow jump opcodes (JUMP, JUMP_FALSE, JUMP_TRUE,
  JUMP_AND, JUMP_OR, ITER_NEXT, CHECK_*) may only target a pc whose innermost
  extent is the source's own (a nested PUSH pc counts as the enclosing
  extent's) or the source extent's `pop_pc` (leaving the block). A target
  inside a block the source is not in, or another block's POP, is refused at
  Code construction. See [archive/bytecode-metadata-tables.md](archive/bytecode-metadata-tables.md)
  § "Jump Legality and Locality".
- **Fast slots and the frame namespace**: finalize validates FAST indices
  against the Code's own NamespaceSpec and records the root scope's slot need
  (`KosCode_FAST_NEED`); the frame install (`kos_code_fits_namespace`) refuses
  a namespace that does not support fast slots or holds fewer than that, so
  an Expression over a smaller namespace or a Function whose formals dedup
  raise RuntimeError instead of indexing past the namespace.

The **Invariants** bullets per opcode (and this section) form the **contract**:
the evaluator must preserve them, and the compiler must produce code that
satisfies them. Violations are undefined behaviour (or may be asserted in debug
builds).

### 1.6 Implementation status

**This document is the single authoritative reference for opcode implementation
status.** The opcodes without a runtime are listed here; every other opcode
is implemented.

**No runtime:** THEEND (end-of-code sentinel; always raises if executed).

**Compile-time only (no-op at runtime):** LABEL, POSITION.

All other opcodes are implemented. When adding or changing opcodes, update both
the implementation and this document.

### 1.7 Compilation: pre-compile vs post-compile

The compiler emits a **pre-compile** stream of instructions. A **finalize** pass
then produces the **post-compile** bytecode stored in a KosCode (labels → pcs,
constant pool, extent and position tables, optional VAR→FAST lowering—see the
pipeline below). That is **not** the same thing as the Python method
`Compiler.compress`, which is an earlier **peephole** pass on the instruction
list; see §1.8. Older text may call finalize “compress” in the abstract; this
document uses **finalize** for that VM-oriented stage to avoid confusion with
`compress`. Many opcodes have different argument meaning in each form.

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
| String label name      | pc (uint16)             | JUMP, JUMP_FALSE, JUMP_TRUE, JUMP_AND, JUMP_OR, ITER_NEXT, CHECK_*, PUSH_TRY, PUSH_FINALLY, PUSH_WIND |
| Literal value          | Constant pool index     | CONST |
| Name (string/symbol)   | Constant pool index     | VAR, VAR_BIND, VAR_CLEAR, VAR_TAKE, VAR_REF, VAR_STORE; ATTR*, ITEM_CONST* (key) |
| `(name, value, …)`     | Extra-table index       | VAR_STORE_CONST (§12); may lower to FAST_STORE_CONST (§13) |
| Name → slot (optional) | Fast local index       | VAR may be lowered to FAST*; then arg is slot index |
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

### 1.8 Peephole instruction compression (`Compiler.compress`)

The Koskari Python compiler runs **`Compiler.compress`** on the fragment’s
instruction list inside **`Compiler.complete`** (before `_cpool_scan_merge`,
**`convert_fast`**, **`demote_tail_calls_if_yields`**, and construction of the
**`Code`** object). Implementation:
[`koskari/compile/compiler.py`](../koskari/compile/compiler.py) (`compress`,
`_CHECK_SQUASHES`, `_SWITCH_SQUASHES`). Regression tests:
[`tests/compile/test_compress.py`](../tests/compile/test_compress.py).

**Purpose.** Shorten or simplify the pre-finalize `(opcode, arg)` stream in a
single forward pass. The pass keeps a short output deque; most rules compare the
current instruction with the **last** emitted one (`squished[-1]`). The
implementation seeds the deque with a leading **`KOS_OP_NOOP`** and drops it at
the end if any real instructions were emitted.

**Non-goals.** `compress` does **not** resolve labels to PCs, build the constant
pool, derive extent or position tables, lower VAR to FAST (**`convert_fast`**),
or demote tail calls when yields are present. Those happen later; see §1.7.

**Transformations** (order matches the `for (op, arg) in instr` branch order in
`compress`):

| Category | Rule |
|----------|------|
| Dead stack | **`KOS_OP_CONST`** or **`KOS_OP_DUP`** followed by **`KOS_OP_POP`** with **arg 1** → drop both (omit the POP and remove the prior instruction). |
| Constant fold | **`KOS_OP_OBJECT_NOT`** with prior **`KOS_OP_CONST`** → replace the pair with a single **`KOS_OP_CONST`** whose value is **`not`** the prior constant (Python `bool` semantics for the folded value). |
| Compare fold | **`KOS_OP_OBJECT_COMPARE`** whose CMPCOUNT count is fed entirely by prior bare **`KOS_OP_CONST`** rows → evaluate the chain at compile time and replace all rows with a single **`KOS_OP_CONST`** bool. A fallback rule (count read from the operand). Folds the six richcompare selectors (chained-pair semantics) plus IN / NOT_IN at arity 2; IS / IS_NOT never fold (object identity does not survive const serialization), nor do IS_INSTANCE / IS_SUBCLASS. Declines on any comparison error, leaving the runtime op in place. |
| Constant + `JUMP_TRUE` | Prior **`KOS_OP_CONST`**: if truthy, replace with **`KOS_OP_JUMP`** to the same label; if falsy, drop both (no jump). |
| Constant + `JUMP_FALSE` | Prior **`KOS_OP_CONST`**: if the constant is falsy, unconditional **`KOS_OP_JUMP`**; if truthy, drop both. |
| Compare + `JUMP_FALSE` | Prior opcode is in **`_CHECK_SQUASHES`** (see table below): delete the prior instruction and replace **`JUMP_FALSE`** with the mapped single opcode (same jump target **arg**). The **`KOS_OP_OBJECT_NOT`** result is dispatched again as a **`KOS_OP_JUMP_TRUE`**, so a compare ahead of the `not` fuses into a negated **`KOS_OP_CHECK_COMPARE`**. |
| `SWITCH_EQ` fusion | Prior opcode in **`_SWITCH_SQUASHES`**: replace **`KOS_OP_SWITCH_EQ`** with the mapped opcode; **arg** becomes **`(switch_label, *pargs[:3])`** if the prior **arg** was a tuple, else **`(switch_label, pargs, None, None)`** ( **`switch_label`** is this instruction’s original **arg**). |
| `LABEL` vs prior jump | If the **`KOS_OP_LABEL`** name equals the prior instruction’s target: for prior **`KOS_OP_JUMP`**, drop the jump; for prior **`KOS_OP_JUMP_TRUE`** / **`KOS_OP_JUMP_FALSE`**, if the instruction before the jump is **`KOS_OP_CONST`**, drop const and jump; else replace the conditional jump with **`KOS_OP_POP`** **1**. |
| Implicit `None` | **`KOS_OP_CONST`** **`None`** immediately before **`KOS_OP_RETURN`** / **`KOS_OP_BREAK`** / **`KOS_OP_CONTINUE`** / **`KOS_OP_YIELD`** with **arg 1** → remove the const and set the control opcode’s **arg** to **0**. |
| Built tuple | **`KOS_OP_BUILD_ARGS`** **n** (n ≥ 2) immediately before **`KOS_OP_RETURN`** / **`KOS_OP_BREAK`** / **`KOS_OP_CONTINUE`** / **`KOS_OP_YIELD`** with **arg 1** → drop the build and set the control opcode’s **arg** to **n**; its N form builds the same tuple. n of 0 and 1 stay, since `()` is not `None` and a one-tuple is not its element. |
| `CAR` / `CDR` merge | Two consecutive **same** opcodes **`KOS_OP_CAR`** or **`KOS_OP_CDR`** → one instruction with **args summed**. |
| `VAR_STORE` fusion | Prior **`KOS_OP_CONST`** `value` immediately followed by **`KOS_OP_VAR_STORE`** `name` → **`KOS_OP_VAR_STORE_CONST`** with arg **`(name, value, None, None)`** (drop the **`CONST`**). Only the **immediate** prior instruction counts. |
| Epilogue | If the stream does not end with **`KOS_OP_RETURN`**: if the last op is **`KOS_OP_CONST`** **`None`**, replace it with **`KOS_OP_RETURN`** **0**; otherwise append **`KOS_OP_RETURN`** **1**. |
| Control feeder (fusion pass) | In the later `fuse_feeders` pass, **`KOS_OP_RETURN`** **1** or **`KOS_OP_YIELD`** **1** fed by a bare **`KOS_OP_CONST`** becomes **`KOS_OP_RETURN_CONST`** / **`KOS_OP_YIELD_CONST`**, and fed by a bare **`KOS_OP_VAR`** becomes **`KOS_OP_RETURN_VAR`** / **`KOS_OP_YIELD_VAR`**, which lower to the FAST twins; the epilogue's appended **`KOS_OP_RETURN`** **1** fuses the same way. |
| Truth-test feeder (fusion pass) | In the later `fuse_feeders` pass, **`KOS_OP_JUMP_FALSE`** or **`KOS_OP_JUMP_TRUE`** fed by a bare **`KOS_OP_VAR`** becomes **`KOS_OP_JUMP_FALSE_VAR`** / **`KOS_OP_JUMP_TRUE_VAR`**, which lower to the FAST twins; a **`KOS_OP_CONST`** feeder was folded by the constant rules above. |
| Short-circuit feeder (fusion pass) | In the later `fuse_feeders` pass, **`KOS_OP_JUMP_AND`** or **`KOS_OP_JUMP_OR`** fed by a bare **`KOS_OP_VAR`** becomes **`KOS_OP_JUMP_AND_VAR`** / **`KOS_OP_JUMP_OR_VAR`**, which read the value from the row and push it only on the jumping path; they lower to the FAST twins. |
| Item feeders (fusion pass) | In the later `fuse_feeders` pass an item row folds the run of bare **`KOS_OP_VAR`** / **`KOS_OP_CONST`** loads directly ahead of it, which in stack order is the value, then the key, then the object, into the row that names them (§15): **`KOS_OP_ITEM`** fed by `VAR obj, VAR key` becomes **`KOS_OP_VAR_ITEM_VAR`**, **`KOS_OP_ITEM_STORE`** fed by `VAR obj, CONST key, CONST value` becomes **`KOS_OP_VAR_ITEM_CONST_STORE_CONST`**, and so on for CLEAR, TAKE, REF, and INPLACE, whose value feeders are re-emitted ahead of the fused row and whose single CONST operand folds into the `_CONST` twin. A CONST in the object position never folds. The rows a special shapes with the value on the stack fold a value feeder alone. The named rows lower to their FAST twins through a written-out map. |

**`_CHECK_SQUASHES`** (prior opcode → **LABEL** opcode replacing the **`JUMP_FALSE`** pair; the fused opcode inherits the jump's label):

| Prior | Replaced with |
|-------|----------------|
| `KOS_OP_OBJECT_NOT` | `KOS_OP_JUMP_TRUE` |
| `KOS_OP_EXCEPTION_MATCH` | `KOS_OP_CHECK_EXCEPTION` |
| `KOS_OP_NILP` | `KOS_OP_CHECK_NIL` |

**`OBJECT_COMPARE` fusion** (selector-keyed comparison prior → **EXTRA** `(CMP, COUNT, LABEL, RAW)` check). The prior `KOS_OP_OBJECT_COMPARE` carries its selector and operand count in a CMPCOUNT arg; the fusion preserves both, emitting **`KOS_OP_CHECK_COMPARE`** with arg `(selector, count, label, 0)` so the chained comparison survives (`a < b < c` fuses just like `a < b`); the `JUMP_TRUE` pair fuses the same way with the fourth field, the negate flag, set to 1, so the check tests the negated chain.

**`_SWITCH_SQUASHES`** (prior opcode → fused switch opcode):

| Prior | Replaced with |
|-------|----------------|
| `KOS_OP_CONST` | `KOS_OP_SWITCH_EQ_CONST` |
| `KOS_OP_VAR` | `KOS_OP_SWITCH_EQ_VAR` |
| `KOS_OP_VAR_ATTR` | `KOS_OP_SWITCH_EQ_VAR_ATTR` |

**Examples.**
- `KOS_OP_OBJECT_COMPARE` `(EQ, count)`, **`KOS_OP_JUMP_FALSE`** `L` → one **`KOS_OP_CHECK_COMPARE`** `(EQ, count, L, 0)` (selector and count carried from the comparison, label from the jump).
- `KOS_OP_OBJECT_COMPARE` `(EQ, count)`, **`KOS_OP_JUMP_TRUE`** `L` → one **`KOS_OP_CHECK_COMPARE`** `(EQ, count, L, 1)`, which jumps when the chain holds.

### 1.9 BIND vs STORE: critical distinction

**BIND and STORE are NOT interchangeable. They are fundamentally different operations.**

| Operation | Meaning | When to use |
|-----------|---------|-------------|
| **BIND** | *"I will now use this reference to resolve the given name."* | Establish or replace the name→ref mapping. The name will resolve to this reference from now on. |
| **STORE** | *"I will now store this value in the given name."* | Write a value through the reference that already resolves the name. The name must already be bound. |

- **BIND** takes a reference (or value that becomes a reference) and makes it the resolution for a name. It changes *what the name refers to*.
- **STORE** takes a value and writes it into the storage that the name already refers to. It changes *the content* at that reference; it does not change the binding itself.

Use **VAR_BIND** / **FAST_BIND** when introducing or re-binding a name. Use **VAR_STORE** / **FAST_STORE** when updating the value at an existing binding. When the stored value is a compile-time constant, **`Compiler.compress`** may fuse **`CONST`** + **`VAR_STORE`** into **`VAR_STORE_CONST`** (no stack); **`convert_fast`** may then lower that to **`FAST_STORE_CONST`** for locals.

### 1.10 Maintaining this document

When adding or changing an opcode:

1. **Add to `koskari/lib/opcodes.incl`** (name and style). The enum and order
   there are authoritative.
2. **Implement or stub** the handler in `koskari/lib/opcode/*.c` (included from
   `handlers.incl`; or mark as no-runtime for THEEND / compile-time for LABEL,
   POSITION).
3. **Add or update** the opcode's section in this document: a `### KOS_OP_*`
   heading with stable anchor, category, stack effect, invariants, compiler form
   (if pre ≠ post), and at least one disassembly table (post-compile) when
   helpful. For stubs, add **Intended** (design) and **Current** (stub/error) if helpful.
4. **Add or extend tests** that match the documented behaviour; consider adding a
   "Covered by …" line in the doc (e.g. `tests/lib/test_ceval.py`).
5. If you change **`Compiler.compress`** or its squash maps, update **§1.8** and
   **`tests/compile/test_compress.py`**.


## 2. Opcode categories (index)

- [3. No-op and compile-time](#3-no-op-and-compile-time) — NOOP, LABEL, POSITION
- [4. Stack](#4-stack) — DUP, POP, UNPACK, UNPACK_STAR, UNPACK_VAR, UNPACK_FAST
- [5. Constants](#5-constants) — CONST, CONST2, CONST3, CONST4
- [5.1 Pair construction and access](#51-pair-construction-and-access) — CAR, CDR, CONS, PROPER, PAIR_SPLICE, NILP
- [6. Mode control](#6-mode-control) — AWAIT, BREAK, CONTINUE, RAISE, RETURN, YIELD, RETURN_CONST, YIELD_CONST, YIELD_FROM
- [7. Iteration](#7-iteration) — ITER_NEXT, ITER_NEXT_STORE_VAR, ITER_NEXT_STORE_FAST, NEXT
- [8. Jumps](#8-jumps) — JUMP, JUMP_FALSE, JUMP_TRUE, JUMP_FALSE_VAR, JUMP_TRUE_VAR, JUMP_FALSE_FAST, JUMP_TRUE_FAST, JUMP_AND, JUMP_OR, JUMP_AND_VAR, JUMP_OR_VAR, JUMP_AND_FAST, JUMP_OR_FAST
- [9. Check jumps](#9-check-jumps) — CHECK_COMPARE, CHECK_COMPARE_VAR, CHECK_COMPARE_FAST, CHECK_EXCEPTION, CHECK_NIL
- [9.1 Switch dispatch](#91-switch-dispatch) — SWITCH_EQ, SWITCH_EQ_CONST, SWITCH_EQ_VAR, SWITCH_EQ_FAST, SWITCH_EQ_VAR_ATTR, SWITCH_EQ_FAST_ATTR
- [9.2 Pattern matching](#92-pattern-matching) — MATCH_DESCEND, MATCH_SINGLE, MATCH_FITS, MATCH_NEXT, MATCH_BOUND, MATCH_UNBOUND, MATCH_SYNC, MATCH_TAIL, MATCH_SCOPE, MATCH_SCOPE_NEXT, MATCH_RESET, MATCH_AT, MATCH_SPAN, MATCH_STORE, MATCH_CLEAR, MATCH_CAPTURES, MATCH_PASS, MATCH_LOAD, MATCH_APPEND, MATCH_MIN, MATCH_COLUMNS, MATCH_LITERAL, MATCH_CORE, MATCH_TYPE_VAR, MATCH_TYPE_FAST, MATCH_CLASS_VAR, MATCH_CLASS_FAST, MATCH_SAME, MATCH_FIELD_POS_VAR, MATCH_FIELD_POS_FAST, MATCH_FIELD_KW, MATCH_MAPPING, MATCH_KEY, MATCH_KEY_VAR, MATCH_KEY_FAST
- [10. Introspection](#10-introspection) — GLOBALS, LOCALS, IMPORT, IMPORT_FROM, IMPORT_STAR, IMPORT_USE
- [11. Exception handling](#11-exception-handling) — EXCEPTION, EXCEPTION_CLEAR, EXCEPTION_MATCH
- [12. Variables by name](#12-variables-by-name) — VAR, VAR2, VAR3, VAR4, VAR_BIND, VAR_CLEAR, VAR_CLEAR2/3/4, VAR_TAKE, VAR_REF, VAR_STORE, VAR_STORE_CONST, VAR_ATTR*, VAR_ITEM*, VAR_ITER, VAR_NEXT, VAR_FORMAT, VAR_FORMAT_CONST, RETURN_VAR, YIELD_VAR
- [13. Fast variables](#13-fast-variables) — FAST, FAST2, FAST3, FAST4, FAST_BIND, FAST_CLEAR, FAST_CLEAR2/3/4, FAST_TAKE, FAST_REF, FAST_STORE, FAST_STORE_CONST, FAST_ATTR*, FAST_ITEM*, FAST_ITER, FAST_NEXT, FAST_FORMAT, FAST_FORMAT_CONST, RETURN_FAST, YIELD_FAST
- [14. Attributes](#14-attributes) — ATTR, ATTR_CLEAR, ATTR_TAKE, ATTR_REF, ATTR_STORE
- [15. Items](#15-items) — ITEM, ITEM_STORE, ITEM_INPLACE, ITEM_CLEAR, ITEM_TAKE, ITEM_REF, and the rows naming their object, key, and value
- [15.1 Reference dereference](#151-reference-dereference) — DEREF
- [16. Argument production](#16-argument-production) — BUILD_ARGS, BUILD_FUNCTION, BUILD_KWDS, etc.
- [17. Calls](#17-calls) — CALL, CALL_NAMES, CALL_STAR, CALL_ATTR*, TAIL_*
- [18. Block control](#18-block-control) — PUSH_FOREACH, PUSH_LOOP, PUSH_NS, PUSH_COLLECT, PUSH_TRY, PUSH_FINALLY, CLEAR_*, POP_*
- [18.1 Context managers (with / dyn-with)](#181-context-managers-with--dyn-with) — PUSH_WITH, POP_WITH, PUSH_DYN_WITH, POP_DYN_WITH
- [19. Object protocol](#19-object-protocol) — OBJECT_COMPARE family, OBJECT_OPERAND and its named-operand fusions, OBJECT_INPLACE, and the general OBJECT_* opcodes
- [20. Delimited continuations](#20-delimited-continuations) — PUSH_PROMPT, PUSH_SHIFT, POP_SHIFT, POP_PROMPT
- [21. Undelimited continuations (call/cc)](#21-undelimited-continuations-callcc) — PUSH_CC, POP_CC, CAPTURE_CC
- [22. Dynamic wind](#22-dynamic-wind) — PUSH_WIND, POP_WIND
- [23. Parameterize extent](#23-parameterize-extent) — PUSH_PARAM, PARAM, POP_PARAM
- [24. Sentinel](#24-sentinel) — THEEND

### 2.1 Quick-reference index

| Opcode | Style | One-line effect | Section |
|--------|-------|------------------|---------|
| NOOP | NOARG | Advance PC only | [§3](#kos_op_noop) |
| LABEL | COMPILE | Jump target; stripped post-compile | [§3](#kos_op_label) |
| POSITION | COMPILE | Source position; stripped post-compile | [§3](#kos_op_position) |
| DUP | COUNT | Duplicate top N stack elements | [§4](#kos_op_dup) |
| POP | COUNT | Pop N elements | [§4](#kos_op_pop) |
| UNPACK | COUNT | Realize TOS as list; push N scalars (seq[0] is TOS) | [§4](#kos_op_unpack) |
| UNPACK_STAR | EXTRA | Prefix / middle / suffix unpack (two COUNT fields) | [§4](#kos_op_unpack_star) |
| UNPACK_VAR | EXTRA | STORE seq[0], seq[1] into two VAR names | [§4](#kos_op_unpack_var) |
| UNPACK_FAST | EXTRA | STORE seq[0], seq[1] into two fast slots | [§4](#kos_op_unpack_fast) |
| CONST | CONST | Push constant from pool | [§5](#kos_op_const) |
| CAR | COUNT | Apply car **n** times (**n** ≥ 1); **n** = 0 invalid | [§5.1](#kos_op_car) |
| CDR | COUNT | Apply cdr **n** times (**n** ≥ 1); **n** = 0 invalid | [§5.1](#kos_op_cdr) |
| CONS | COUNT | Pop N values; push (e_0 . ... . e_{N-1}) | [§5.1](#kos_op_cons) |
| PROPER | COUNT | Pop N values; push proper list (e_0 ... e_{N-1}) | [§5.1](#kos_op_proper) |
| BRACKET_CONS | COUNT | Pop N values; push the bracket pair chain [e_0 . ... . e_{N-1}] | [§5.1](#kos_op_bracket_cons) |
| BRACKET_PROPER | COUNT | Pop N values; push the proper bracket list [e_0 ... e_{N-1}] | [§5.1](#kos_op_bracket_proper) |
| PAIR_SPLICE | COUNT | Concatenate N pair/tuple segments into one proper list | [§5.1](#kos_op_pair_splice) |
| NILP | NOARG | Pop TOS; push True when it is an empty-list singleton | [§5.1](#kos_op_nilp) |
| AWAIT | COUNT | Suspend on awaitable; resume with value | [§6](#kos_op_await) |
| CONST2 | EXTRA | Push two constants in row order | [§5](#kos_op_const2) |
| CONST3 | EXTRA | Push three constants in row order | [§5](#kos_op_const3) |
| CONST4 | EXTRA | Push four constants in row order | [§5](#kos_op_const4) |
| BREAK | COUNT | Jump to loop break target | [§6](#kos_op_break) |
| CONTINUE | COUNT | Jump to loop continue target | [§6](#kos_op_continue) |
| RAISE | COUNT | Pop N, raise exception | [§6](#kos_op_raise) |
| RETURN | COUNT | Pop return value; threads the plain, extent, and resume-parent shapes itself, else `EVAL_RETURN(val)` → **`eval_event_return`** (unwind/pop/delivery); **`KOS_RES_RETURN`** at driver boundary | [§6](#kos_op_return) |
| YIELD | COUNT | Pop value; `EVAL_YIELD(val)` → **`apply_kos_op_result`** → **`eval_event_yield`** (walk/freeze, optional boundary deliver) | [§6](#kos_op_yield) |
| RETURN_CONST | CONST | Return the pooled constant; the `CONST` + `RETURN 1` fusion | [§6](#kos_op_return_const) |
| YIELD_CONST | CONST | Yield the pooled constant; the `CONST` + `YIELD 1` fusion | [§6](#kos_op_yield_const) |
| YIELD_FROM | NOARG | Delegate to sub-iterator/gen; PC-stable; send/throw/close | [§6](#kos_op_yield_from) |
| ITER_NEXT | LABEL | Next from region iterator; else jump | [§7](#kos_op_iter_next) |
| ITER_NEXT_STORE_VAR | EXTRA | Next from region iterator stored under the name; else jump; the `ITER_NEXT` + `VAR_STORE` fusion | [§7](#kos_op_iter_next_store_var) |
| ITER_NEXT_STORE_FAST | EXTRA | Next from region iterator stored into the fast slot; else jump | [§7](#kos_op_iter_next_store_fast) |
| OBJECT_NEXT | COUNT | Stack-based next (1 or 2 args) | [§7](#kos_op_object_next) |
| VAR_NEXT | EXTRA | Fused VAR + OBJECT_NEXT: step the named iterator; count 2 pops a default for exhaustion | [§12](#kos_op_var_next) |
| FAST_NEXT | EXTRA | As VAR_NEXT, with the iterator loaded from its fast slot | [§13](#kos_op_fast_next) |
| JUMP | LABEL | Unconditional jump to pc | [§8](#kos_op_jump) |
| JUMP_FALSE | LABEL | Jump if TOS false | [§8](#kos_op_jump_false) |
| JUMP_TRUE | LABEL | Jump if TOS true | [§8](#kos_op_jump_true) |
| JUMP_FALSE_VAR | EXTRA | Jump if the named value is false | [§8](#kos_op_jump_false_var) |
| JUMP_TRUE_VAR | EXTRA | Jump if the named value is true | [§8](#kos_op_jump_true_var) |
| JUMP_FALSE_FAST | EXTRA | Jump if the fast slot is false | [§8](#kos_op_jump_false_fast) |
| JUMP_TRUE_FAST | EXTRA | Jump if the fast slot is true | [§8](#kos_op_jump_true_fast) |
| JUMP_AND | LABEL | Short-circuit AND | [§8](#kos_op_jump_and) |
| JUMP_OR | LABEL | Short-circuit OR | [§8](#kos_op_jump_or) |
| JUMP_AND_VAR | EXTRA | Short-circuit AND over the named value | [§8](#kos_op_jump_and_var) |
| JUMP_OR_VAR | EXTRA | Short-circuit OR over the named value | [§8](#kos_op_jump_or_var) |
| JUMP_AND_FAST | EXTRA | Short-circuit AND over the fast slot | [§8](#kos_op_jump_and_fast) |
| JUMP_OR_FAST | EXTRA | Short-circuit OR over the fast slot | [§8](#kos_op_jump_or_fast) |
| CHECK_COMPARE | EXTRA | Chain-compare N operands under a KosCompare selector; jump if chain fails | [§9](#kos_op_check_compare) |
| CHECK_COMPARE_VAR | EXTRA | Arity-2 fused check; operands from VAR/CONST per CMPSRC bits | [§9](#kos_op_check_compare_var) |
| CHECK_COMPARE_FAST | EXTRA | Arity-2 fused check; operands from FAST/CONST per CMPSRC bits | [§9](#kos_op_check_compare_fast) |
| CHECK_EXCEPTION | LABEL | Jump if exception no match | [§9](#kos_op_check_exception) |
| CHECK_NIL | LABEL | Squashed NILP + JUMP_FALSE: pop TOS; jump when it is not an empty-list singleton | [§9](#kos_op_check_nil) |
| SWITCH_EQ | LABEL | Equality dispatch; on match pop both and jump; on mismatch pop key only, fall through | [§9.1](#kos_op_switch_eq) |
| SWITCH_EQ_CONST | EXTRA | Compare TOS to constant RHS; on match pop TOS and jump; on mismatch fall through | [§9.1](#kos_op_switch_eq_const) |
| SWITCH_EQ_VAR | EXTRA | Compare TOS to namespace var RHS; on match pop TOS and jump; on mismatch fall through | [§9.1](#kos_op_switch_eq_var) |
| SWITCH_EQ_FAST | EXTRA | Compare TOS to fast-slot RHS; on match pop TOS and jump; on mismatch fall through | [§9.1](#kos_op_switch_eq_fast) |
| SWITCH_EQ_VAR_ATTR | EXTRA | Compare TOS to getattr(var, attr); on match pop TOS and jump; on mismatch fall through | [§9.1](#kos_op_switch_eq_var_attr) |
| SWITCH_EQ_FAST_ATTR | EXTRA | Compare TOS to getattr(fast, attr); on match pop TOS and jump; on mismatch fall through | [§9.1](#kos_op_switch_eq_fast_attr) |
| MATCH_DESCEND | LABEL | Sequence gate on TOS; a cursor over it replaces it; a miss pops it | [§9.2](#kos_op_match_descend) |
| MATCH_SINGLE | NOARG | A one-element cursor over TOS replaces it | [§9.2](#kos_op_match_single) |
| MATCH_FITS | EXTRA | Miss unless the cursor at the depth has between the minimum and the maximum elements left | [§9.2](#kos_op_match_fits) |
| MATCH_NEXT | EXTRA | Pull the next element from the cursor at the depth, boxed from a pair spine; a miss at the end | [§9.2](#kos_op_match_next) |
| MATCH_BOUND | EXTRA | Push a cursor at the cursor's position ending the width before its end | [§9.2](#kos_op_match_bound) |
| MATCH_UNBOUND | EXTRA | Push a cursor at the cursor's position ending at its subject's end | [§9.2](#kos_op_match_unbound) |
| MATCH_SYNC | EXTRA | Move the cursor at the first depth to the position of the cursor at the second | [§9.2](#kos_op_match_sync) |
| MATCH_TAIL | EXTRA | Push the remaining elements but the suffix width as a span, advancing the cursor | [§9.2](#kos_op_match_tail) |
| MATCH_SCOPE | EXTRA | Push a mark at the cursor's position with the count of empty slots | [§9.2](#kos_op_match_scope) |
| MATCH_SCOPE_NEXT | EXTRA | Push a mark at the cursor's position, then the cursor's next element; a miss leaves the mark | [§9.2](#kos_op_match_scope_next) |
| MATCH_RESET | EXTRA | Move the cursor at the first depth back to the position of the mark at the second | [§9.2](#kos_op_match_reset) |
| MATCH_AT | EXTRA | Push the element at the mark's position | [§9.2](#kos_op_match_at) |
| MATCH_SPAN | EXTRA | Push the span from the mark to the cursor; jump when it is empty | [§9.2](#kos_op_match_span) |
| MATCH_STORE | EXTRA | Pop TOS into a slot of the mark at the depth | [§9.2](#kos_op_match_store) |
| MATCH_CLEAR | EXTRA | Empty the listed slots of the mark at the depth | [§9.2](#kos_op_match_clear) |
| MATCH_CAPTURES | EXTRA | Push the mark's filled slots: a DatumFrame over a pair spine, a dict otherwise | [§9.2](#kos_op_match_captures) |
| MATCH_PASS | EXTRA | Push a repeat pass's value: its captures, or its span when no slot is filled | [§9.2](#kos_op_match_pass) |
| MATCH_LOAD | EXTRA | Push a slot of the mark at the depth; jump when it is empty | [§9.2](#kos_op_match_load) |
| MATCH_APPEND | EXTRA | Pop TOS and append it to the list at the depth | [§9.2](#kos_op_match_append) |
| MATCH_MIN | EXTRA | Miss unless the list at the depth holds at least the count of items | [§9.2](#kos_op_match_min) |
| MATCH_COLUMNS | EXTRA | Store each name's per-pass values, as a list, into a slot of the mark | [§9.2](#kos_op_match_columns) |
| MATCH_LITERAL | EXTRA | TOS, which stays, must be the constant | [§9.2](#kos_op_match_literal) |
| MATCH_CORE | EXTRA | TOS, which stays, must be an instance of the core type the ordinal names | [§9.2](#kos_op_match_core) |
| MATCH_TYPE_VAR / MATCH_TYPE_FAST | EXTRA | TOS, which stays, must be an instance of the type in the variable | [§9.2](#kos_op_match_type_var) |
| MATCH_CLASS_VAR / MATCH_CLASS_FAST | EXTRA | TOS, which stays, must be an instance of the class in the variable, whose `__match_args__` must cover the positional fields | [§9.2](#kos_op_match_class_var) |
| MATCH_SAME | EXTRA | TOS, which stays, must equal a slot of the mark at the depth; an empty slot misses | [§9.2](#kos_op_match_same) |
| MATCH_FIELD_POS_VAR / MATCH_FIELD_POS_FAST | EXTRA | Push the attribute the class's `__match_args__` names at the position | [§9.2](#kos_op_match_field_pos_var) |
| MATCH_FIELD_KW | EXTRA | Push the attribute the constant names | [§9.2](#kos_op_match_field_kw) |
| MATCH_MAPPING | LABEL | The mapping gate on TOS | [§9.2](#kos_op_match_mapping) |
| MATCH_KEY | EXTRA | Push the value under the constant key; an absent key misses | [§9.2](#kos_op_match_key) |
| MATCH_KEY_VAR / MATCH_KEY_FAST | EXTRA | Push the value under the key in the variable; an absent key misses | [§9.2](#kos_op_match_key_var) |
| GLOBALS | NOARG | Push the module (outermost) namespace | [§10](#kos_op_globals) |
| LOCALS | NOARG | Push the frame's current namespace | [§10](#kos_op_locals) |
| IMPORT | EXTRA | Import module; bind root or leaf (self-contained) | [§10.1](#kos_op_import) |
| IMPORT_FROM | EXTRA | Import module; bind named attrs (self-contained) | [§10.1](#kos_op_import_from) |
| IMPORT_STAR | EXTRA | Import module; bind its public names | [§10.1](#kos_op_import_star) |
| IMPORT_USE | EXTRA | Import module; bind feature families (self-contained) | [§10.1](#kos_op_import_use) |
| EXCEPTION | NOARG | Push current exception | [§11](#kos_op_exception) |
| EXCEPTION_CLEAR | NOARG | Clear exception state | [§11](#kos_op_exception_clear) |
| EXCEPTION_MATCH | NOARG | Pop type(s), push match result | [§11](#kos_op_exception_match) |
| VAR | VAR | Load by name | [§12](#kos_op_var) |
| VAR2 | EXTRA | Load two names, push in row order | [§12](#kos_op_var2) |
| VAR3 | EXTRA | Load three names, push in row order | [§12](#kos_op_var3) |
| VAR4 | EXTRA | Load four names, push in row order | [§12](#kos_op_var4) |
| VAR_BIND | VAR | BIND: use TOS ref to resolve name | [§12](#kos_op_var_bind) |
| VAR_CLEAR | VAR | Delete the name from the namespace; no stack effect | [§12](#kos_op_var_clear) |
| VAR_CLEAR2 | EXTRA | Clear 2 names in row order; no stack effect | [§12](#kos_op_var_clear2) |
| VAR_CLEAR3 | EXTRA | Clear 3 names in row order; no stack effect | [§12](#kos_op_var_clear3) |
| VAR_CLEAR4 | EXTRA | Clear 4 names in row order; no stack effect | [§12](#kos_op_var_clear4) |
| VAR_TAKE | VAR | Deref name, clear binding, push value (errors if empty) | [§12](#kos_op_var_take) |
| VAR_REF | VAR | Push a reference to the named variable | [§12](#kos_op_var_ref) |
| VAR_STORE | VAR | STORE: write TOS value into name | [§12](#kos_op_var_store) |
| VAR_STORE_CONST | EXTRA | STORE: write constant into name (no stack) | [§12](#kos_op_var_store_const) |
| VAR_INPLACE | EXTRA | Augmented assignment through the namespace: deref the name, reduce with the popped rhs, store back | [§12](#kos_op_var_inplace) |
| VAR_INPLACE_CONST | EXTRA | Augmented assignment through the namespace with a constant rhs; no stack effect | [§12](#kos_op_var_inplace_const) |
| VAR_ATTR | EXTRA | Get attribute; object loaded from the namespace by name | [§12](#kos_op_var_attr) |
| VAR_ATTR_STORE | EXTRA | Set attribute; object from the namespace | [§12](#kos_op_var_attr_store) |
| VAR_ATTR_CLEAR | EXTRA | Delete attribute; object from the namespace | [§12](#kos_op_var_attr_clear) |
| VAR_ATTR_TAKE | EXTRA | Get attribute, delete it, push the value; object from the namespace | [§12](#kos_op_var_attr_take) |
| VAR_ATTR_REF | EXTRA | Push attribute reference; object from the namespace | [§12](#kos_op_var_attr_ref) |
| VAR_ATTR_INPLACE | EXTRA | Reduce obj.attr in place with the count values and store it back; object from the namespace | [§12](#kos_op_var_attr_inplace) |
| ITEM_STORE_VAR | VAR | Set item; object on the stack, key on the stack, value by name | [§12](#kos_op_item_store_var) |
| ITEM_CONST_STORE_VAR | EXTRA | Set item; object on the stack, constant key, value by name | [§12](#kos_op_item_const_store_var) |
| ITEM_VAR | VAR | Get item; object on the stack, key by name | [§12](#kos_op_item_var) |
| ITEM_VAR_CLEAR | VAR | Delete item; object on the stack, key by name | [§12](#kos_op_item_var_clear) |
| ITEM_VAR_TAKE | VAR | Take item; object on the stack, key by name | [§12](#kos_op_item_var_take) |
| ITEM_VAR_REF | VAR | Item reference; object on the stack, key by name | [§12](#kos_op_item_var_ref) |
| ITEM_VAR_STORE | VAR | Set item; object on the stack, key by name | [§12](#kos_op_item_var_store) |
| ITEM_VAR_STORE_CONST | EXTRA | Set item to a constant; object on the stack, key by name | [§12](#kos_op_item_var_store_const) |
| ITEM_VAR_STORE_VAR | EXTRA | Set item; object on the stack, key by name, value by name | [§12](#kos_op_item_var_store_var) |
| ITEM_VAR_INPLACE | EXTRA | Reduce item in place; object on the stack, key by name | [§12](#kos_op_item_var_inplace) |
| ITEM_VAR_INPLACE_CONST | EXTRA | Reduce item in place by a constant; object on the stack, key by name | [§12](#kos_op_item_var_inplace_const) |
| VAR_ITEM | VAR | Get item; object by name, key on the stack | [§12](#kos_op_var_item) |
| VAR_ITEM_CLEAR | VAR | Delete item; object by name, key on the stack | [§12](#kos_op_var_item_clear) |
| VAR_ITEM_TAKE | VAR | Take item; object by name, key on the stack | [§12](#kos_op_var_item_take) |
| VAR_ITEM_REF | VAR | Item reference; object by name, key on the stack | [§12](#kos_op_var_item_ref) |
| VAR_ITEM_STORE | VAR | Set item; object by name, key on the stack | [§12](#kos_op_var_item_store) |
| VAR_ITEM_STORE_CONST | EXTRA | Set item to a constant; object by name, key on the stack | [§12](#kos_op_var_item_store_const) |
| VAR_ITEM_STORE_VAR | EXTRA | Set item; object by name, key on the stack, value by name | [§12](#kos_op_var_item_store_var) |
| VAR_ITEM_INPLACE | EXTRA | Reduce item in place; object by name, key on the stack | [§12](#kos_op_var_item_inplace) |
| VAR_ITEM_INPLACE_CONST | EXTRA | Reduce item in place by a constant; object by name, key on the stack | [§12](#kos_op_var_item_inplace_const) |
| VAR_ITEM_CONST | EXTRA | Get item; object by name, constant key | [§12](#kos_op_var_item_const) |
| VAR_ITEM_CONST_CLEAR | EXTRA | Delete item; object by name, constant key | [§12](#kos_op_var_item_const_clear) |
| VAR_ITEM_CONST_TAKE | EXTRA | Take item; object by name, constant key | [§12](#kos_op_var_item_const_take) |
| VAR_ITEM_CONST_REF | EXTRA | Item reference; object by name, constant key | [§12](#kos_op_var_item_const_ref) |
| VAR_ITEM_CONST_STORE | EXTRA | Set item; object by name, constant key | [§12](#kos_op_var_item_const_store) |
| VAR_ITEM_CONST_STORE_CONST | EXTRA | Set item to a constant; object by name, constant key | [§12](#kos_op_var_item_const_store_const) |
| VAR_ITEM_CONST_STORE_VAR | EXTRA | Set item; object by name, constant key, value by name | [§12](#kos_op_var_item_const_store_var) |
| VAR_ITEM_CONST_INPLACE | EXTRA | Reduce item in place; object by name, constant key | [§12](#kos_op_var_item_const_inplace) |
| VAR_ITEM_CONST_INPLACE_CONST | EXTRA | Reduce item in place by a constant; object by name, constant key | [§12](#kos_op_var_item_const_inplace_const) |
| VAR_ITEM_VAR | EXTRA | Get item; object by name, key by name | [§12](#kos_op_var_item_var) |
| VAR_ITEM_VAR_CLEAR | EXTRA | Delete item; object by name, key by name | [§12](#kos_op_var_item_var_clear) |
| VAR_ITEM_VAR_TAKE | EXTRA | Take item; object by name, key by name | [§12](#kos_op_var_item_var_take) |
| VAR_ITEM_VAR_REF | EXTRA | Item reference; object by name, key by name | [§12](#kos_op_var_item_var_ref) |
| VAR_ITEM_VAR_STORE | EXTRA | Set item; object by name, key by name | [§12](#kos_op_var_item_var_store) |
| VAR_ITEM_VAR_STORE_CONST | EXTRA | Set item to a constant; object by name, key by name | [§12](#kos_op_var_item_var_store_const) |
| VAR_ITEM_VAR_STORE_VAR | EXTRA | Set item; object by name, key by name, value by name | [§12](#kos_op_var_item_var_store_var) |
| VAR_ITEM_VAR_INPLACE | EXTRA | Reduce item in place; object by name, key by name | [§12](#kos_op_var_item_var_inplace) |
| VAR_ITEM_VAR_INPLACE_CONST | EXTRA | Reduce item in place by a constant; object by name, key by name | [§12](#kos_op_var_item_var_inplace_const) |
| FAST | FASTVAR | Load from slot | [§13](#kos_op_fast) |
| FAST2 | EXTRA | Load two slots, push in row order | [§13](#kos_op_fast2) |
| FAST3 | EXTRA | Load three slots, push in row order | [§13](#kos_op_fast3) |
| FAST4 | EXTRA | Load four slots, push in row order | [§13](#kos_op_fast4) |
| FAST_BIND | FASTVAR | BIND: use TOS ref to resolve slot | [§13](#kos_op_fast_bind) |
| FAST_CLEAR | FASTVAR | Clear the slot; no stack effect | [§13](#kos_op_fast_clear) |
| FAST_CLEAR2 | EXTRA | Clear 2 slots in row order; no stack effect | [§13](#kos_op_fast_clear2) |
| FAST_CLEAR3 | EXTRA | Clear 3 slots in row order; no stack effect | [§13](#kos_op_fast_clear3) |
| FAST_CLEAR4 | EXTRA | Clear 4 slots in row order; no stack effect | [§13](#kos_op_fast_clear4) |
| FAST_TAKE | FASTVAR | Deref slot, clear, push value (errors if empty) | [§13](#kos_op_fast_take) |
| FAST_REF | FASTVAR | Push a reference to the slot | [§13](#kos_op_fast_ref) |
| FAST_STORE | FASTVAR | STORE: write TOS value into slot | [§13](#kos_op_fast_store) |
| FAST_STORE_CONST | EXTRA | STORE: write constant into slot (no stack) | [§13](#kos_op_fast_store_const) |
| FAST_INPLACE | EXTRA | Augmented assignment on a slot: deref, reduce with the popped rhs, store back | [§13](#kos_op_fast_inplace) |
| FAST_INPLACE_CONST | EXTRA | Augmented assignment on a slot with a constant rhs; no stack effect | [§13](#kos_op_fast_inplace_const) |
| FAST_ATTR | EXTRA | Get attribute; object from a fast slot | [§13](#kos_op_fast_attr) |
| FAST_ATTR_STORE | EXTRA | Set attribute; object from a fast slot | [§13](#kos_op_fast_attr_store) |
| FAST_ATTR_CLEAR | EXTRA | Delete attribute; object from a fast slot | [§13](#kos_op_fast_attr_clear) |
| FAST_ATTR_TAKE | EXTRA | Get attribute, delete it, push the value; object from a fast slot | [§13](#kos_op_fast_attr_take) |
| FAST_ATTR_REF | EXTRA | Push attribute reference; object from a fast slot | [§13](#kos_op_fast_attr_ref) |
| FAST_ATTR_INPLACE | EXTRA | Reduce obj.attr in place with the count values and store it back; object from a fast slot | [§13](#kos_op_fast_attr_inplace) |
| ITEM_STORE_FAST | FASTVAR | Set item; object on the stack, key on the stack, value from a fast slot | [§13](#kos_op_item_store_fast) |
| ITEM_CONST_STORE_FAST | EXTRA | Set item; object on the stack, constant key, value from a fast slot | [§13](#kos_op_item_const_store_fast) |
| ITEM_FAST | FASTVAR | Get item; object on the stack, key from a fast slot | [§13](#kos_op_item_fast) |
| ITEM_FAST_CLEAR | FASTVAR | Delete item; object on the stack, key from a fast slot | [§13](#kos_op_item_fast_clear) |
| ITEM_FAST_TAKE | FASTVAR | Take item; object on the stack, key from a fast slot | [§13](#kos_op_item_fast_take) |
| ITEM_FAST_REF | FASTVAR | Item reference; object on the stack, key from a fast slot | [§13](#kos_op_item_fast_ref) |
| ITEM_FAST_STORE | FASTVAR | Set item; object on the stack, key from a fast slot | [§13](#kos_op_item_fast_store) |
| ITEM_FAST_STORE_CONST | EXTRA | Set item to a constant; object on the stack, key from a fast slot | [§13](#kos_op_item_fast_store_const) |
| ITEM_FAST_STORE_FAST | EXTRA | Set item; object on the stack, key from a fast slot, value from a fast slot | [§13](#kos_op_item_fast_store_fast) |
| ITEM_FAST_INPLACE | EXTRA | Reduce item in place; object on the stack, key from a fast slot | [§13](#kos_op_item_fast_inplace) |
| ITEM_FAST_INPLACE_CONST | EXTRA | Reduce item in place by a constant; object on the stack, key from a fast slot | [§13](#kos_op_item_fast_inplace_const) |
| FAST_ITEM | FASTVAR | Get item; object from a fast slot, key on the stack | [§13](#kos_op_fast_item) |
| FAST_ITEM_CLEAR | FASTVAR | Delete item; object from a fast slot, key on the stack | [§13](#kos_op_fast_item_clear) |
| FAST_ITEM_TAKE | FASTVAR | Take item; object from a fast slot, key on the stack | [§13](#kos_op_fast_item_take) |
| FAST_ITEM_REF | FASTVAR | Item reference; object from a fast slot, key on the stack | [§13](#kos_op_fast_item_ref) |
| FAST_ITEM_STORE | FASTVAR | Set item; object from a fast slot, key on the stack | [§13](#kos_op_fast_item_store) |
| FAST_ITEM_STORE_CONST | EXTRA | Set item to a constant; object from a fast slot, key on the stack | [§13](#kos_op_fast_item_store_const) |
| FAST_ITEM_STORE_FAST | EXTRA | Set item; object from a fast slot, key on the stack, value from a fast slot | [§13](#kos_op_fast_item_store_fast) |
| FAST_ITEM_INPLACE | EXTRA | Reduce item in place; object from a fast slot, key on the stack | [§13](#kos_op_fast_item_inplace) |
| FAST_ITEM_INPLACE_CONST | EXTRA | Reduce item in place by a constant; object from a fast slot, key on the stack | [§13](#kos_op_fast_item_inplace_const) |
| FAST_ITEM_CONST | EXTRA | Get item; object from a fast slot, constant key | [§13](#kos_op_fast_item_const) |
| FAST_ITEM_CONST_CLEAR | EXTRA | Delete item; object from a fast slot, constant key | [§13](#kos_op_fast_item_const_clear) |
| FAST_ITEM_CONST_TAKE | EXTRA | Take item; object from a fast slot, constant key | [§13](#kos_op_fast_item_const_take) |
| FAST_ITEM_CONST_REF | EXTRA | Item reference; object from a fast slot, constant key | [§13](#kos_op_fast_item_const_ref) |
| FAST_ITEM_CONST_STORE | EXTRA | Set item; object from a fast slot, constant key | [§13](#kos_op_fast_item_const_store) |
| FAST_ITEM_CONST_STORE_CONST | EXTRA | Set item to a constant; object from a fast slot, constant key | [§13](#kos_op_fast_item_const_store_const) |
| FAST_ITEM_CONST_STORE_FAST | EXTRA | Set item; object from a fast slot, constant key, value from a fast slot | [§13](#kos_op_fast_item_const_store_fast) |
| FAST_ITEM_CONST_INPLACE | EXTRA | Reduce item in place; object from a fast slot, constant key | [§13](#kos_op_fast_item_const_inplace) |
| FAST_ITEM_CONST_INPLACE_CONST | EXTRA | Reduce item in place by a constant; object from a fast slot, constant key | [§13](#kos_op_fast_item_const_inplace_const) |
| FAST_ITEM_FAST | EXTRA | Get item; object from a fast slot, key from a fast slot | [§13](#kos_op_fast_item_fast) |
| FAST_ITEM_FAST_CLEAR | EXTRA | Delete item; object from a fast slot, key from a fast slot | [§13](#kos_op_fast_item_fast_clear) |
| FAST_ITEM_FAST_TAKE | EXTRA | Take item; object from a fast slot, key from a fast slot | [§13](#kos_op_fast_item_fast_take) |
| FAST_ITEM_FAST_REF | EXTRA | Item reference; object from a fast slot, key from a fast slot | [§13](#kos_op_fast_item_fast_ref) |
| FAST_ITEM_FAST_STORE | EXTRA | Set item; object from a fast slot, key from a fast slot | [§13](#kos_op_fast_item_fast_store) |
| FAST_ITEM_FAST_STORE_CONST | EXTRA | Set item to a constant; object from a fast slot, key from a fast slot | [§13](#kos_op_fast_item_fast_store_const) |
| FAST_ITEM_FAST_STORE_FAST | EXTRA | Set item; object from a fast slot, key from a fast slot, value from a fast slot | [§13](#kos_op_fast_item_fast_store_fast) |
| FAST_ITEM_FAST_INPLACE | EXTRA | Reduce item in place; object from a fast slot, key from a fast slot | [§13](#kos_op_fast_item_fast_inplace) |
| FAST_ITEM_FAST_INPLACE_CONST | EXTRA | Reduce item in place by a constant; object from a fast slot, key from a fast slot | [§13](#kos_op_fast_item_fast_inplace_const) |
| ATTR | CONST | Get attribute | [§14](#kos_op_attr) |
| ATTR_STORE | CONST | Set attribute | [§14](#kos_op_attr_store) |
| ATTR_CLEAR | CONST | Delete attribute | [§14](#kos_op_attr_clear) |
| ATTR_TAKE | CONST | Get attribute, delete it, push value | [§14](#kos_op_attr_take) |
| ATTR_REF | CONST | Push attribute reference | [§14](#kos_op_attr_ref) |
| ATTR_INPLACE | EXTRA | Reduce obj.attr in place with the count values and store it back; pushes nothing | [§14](#kos_op_attr_inplace) |
| ITEM | NOARG | Get item; object on the stack, key on the stack | [§15](#kos_op_item) |
| ITEM_CLEAR | NOARG | Delete item; object on the stack, key on the stack | [§15](#kos_op_item_clear) |
| ITEM_TAKE | NOARG | Take item; object on the stack, key on the stack | [§15](#kos_op_item_take) |
| ITEM_REF | NOARG | Item reference; object on the stack, key on the stack | [§15](#kos_op_item_ref) |
| ITEM_STORE | NOARG | Set item; object on the stack, key on the stack | [§15](#kos_op_item_store) |
| ITEM_STORE_CONST | CONST | Set item to a constant; object on the stack, key on the stack | [§15](#kos_op_item_store_const) |
| ITEM_INPLACE | OPCOUNT | Reduce item in place; object on the stack, key on the stack | [§15](#kos_op_item_inplace) |
| ITEM_INPLACE_CONST | EXTRA | Reduce item in place by a constant; object on the stack, key on the stack | [§15](#kos_op_item_inplace_const) |
| ITEM_CONST | CONST | Get item; object on the stack, constant key | [§15](#kos_op_item_const) |
| ITEM_CONST_CLEAR | CONST | Delete item; object on the stack, constant key | [§15](#kos_op_item_const_clear) |
| ITEM_CONST_TAKE | CONST | Take item; object on the stack, constant key | [§15](#kos_op_item_const_take) |
| ITEM_CONST_REF | CONST | Item reference; object on the stack, constant key | [§15](#kos_op_item_const_ref) |
| ITEM_CONST_STORE | CONST | Set item; object on the stack, constant key | [§15](#kos_op_item_const_store) |
| ITEM_CONST_STORE_CONST | EXTRA | Set item to a constant; object on the stack, constant key | [§15](#kos_op_item_const_store_const) |
| ITEM_CONST_INPLACE | EXTRA | Reduce item in place; object on the stack, constant key | [§15](#kos_op_item_const_inplace) |
| ITEM_CONST_INPLACE_CONST | EXTRA | Reduce item in place by a constant; object on the stack, constant key | [§15](#kos_op_item_const_inplace_const) |
| DEREF | NOARG | Pop Ref, call ref.deref(), push result | [§15.1](#kos_op_deref) |
| BUILD_ARGS | COUNT | Pop N elements and push a tuple of them | [§16](#kos_op_build_args) |
| BUILD_LIST | COUNT | Pop N elements into a new list and push it | [§16](#kos_op_build_list) |
| BUILD_LIST_CONST | CONST | Push a fresh list built from the pooled sequence | [§16](#kos_op_build_list_const) |
| VAR_LIST_APPEND | VAR | Append TOS to the list bound to the named variable, in place | [§16](#kos_op_var_list_append) |
| FAST_LIST_APPEND | FASTVAR | Fast-slot form of VAR_LIST_APPEND | [§16](#kos_op_fast_list_append) |
| BUILD_LIST_APPEND | COUNT | Append the top N values to the list beneath them, in place; leave the list | [§16](#kos_op_build_list_append) |
| BUILD_LIST_APPEND_CONST | CONST | Append the pooled value to the TOS list, in place | [§16](#kos_op_build_list_append_const) |
| BUILD_LIST_EXTEND | NOARG | Extend the list in place from the TOS iterable; leave the list | [§16](#kos_op_build_list_extend) |
| BUILD_LIST_EXTEND_CONST | CONST | Extend the TOS list in place from the pooled sequence | [§16](#kos_op_build_list_extend_const) |
| BUILD_FUNCTION | EXTRA | Build function from spec, with keyword defaults and annotations from names tuples | [§16](#kos_op_build_function) |
| BUILD_KWDS | PAIR_COUNT | Pop 2N elements as key/value pairs and push a dict | [§16](#kos_op_build_kwds) |
| BUILD_KWDS_NAMES | CONST | Pop N values, pair them with the pooled names tuple, push a dict | [§16](#kos_op_build_kwds_names) |
| COMBINE_ARGS | COUNT | Concatenate the M sequences into a single tuple | [§16](#kos_op_combine_args) |
| COMBINE_KWDS | COUNT | Merge the M dicts, later entries winning on a key clash | [§16](#kos_op_combine_kwds) |
| CALL | COUNT | Call with N positional args | [§17](#kos_op_call) |
| CALL_NAMES | COUNT | Call with keyword names on stack | [§17](#kos_op_call_names) |
| CALL_STAR | STAR | Call with *args and/or **kwargs | [§17](#kos_op_call_star) |
| CALL_VAR | EXTRA | Call callable from namespace by name | [§17](#kos_op_call_var) |
| CALL_FAST | EXTRA | Call callable from fast local slot | [§17](#kos_op_call_fast) |
| CALL_VAR_NAMES | EXTRA | Call by name with keyword args | [§17](#kos_op_call_var_names) |
| CALL_FAST_NAMES | EXTRA | Call from fast slot with keyword args | [§17](#kos_op_call_fast_names) |
| CALL_VAR_STAR | EXTRA | Call by name with *args/**kwargs | [§17](#kos_op_call_var_star) |
| CALL_FAST_STAR | EXTRA | Call from fast slot with *args/**kwargs | [§17](#kos_op_call_fast_star) |
| CALL_ATTR | EXTRA | Call obj.name with N positional args | [§17](#kos_op_call_attr) |
| CALL_ATTR_NAMES | EXTRA | Call obj.name with positional + keyword args | [§17](#kos_op_call_attr_names) |
| CALL_FAST_ATTR | EXTRA | Call obj.name with N positional args; obj from fast slot | [§17](#kos_op_call_fast_attr) |
| CALL_VAR_ATTR | EXTRA | Call obj.name with N positional args; obj from VAR | [§17](#kos_op_call_var_attr) |
| CALL_FAST_ATTR_NAMES | EXTRA | Call obj.name with positional + keyword args; obj from fast slot | [§17](#kos_op_call_fast_attr_names) |
| CALL_VAR_ATTR_NAMES | EXTRA | Call obj.name with positional + keyword args; obj from VAR | [§17](#kos_op_call_var_attr_names) |
| CALL_ATTR_STAR | EXTRA | Call obj.name with *args/**kwargs | [§17](#kos_op_call_attr_star) |
| CALL_VAR_ATTR_STAR | EXTRA | Call obj.name with *args/**kwargs; obj from VAR | [§17](#kos_op_call_var_attr_star) |
| CALL_FAST_ATTR_STAR | EXTRA | Call obj.name with *args/**kwargs; obj from fast slot | [§17](#kos_op_call_fast_attr_star) |
| TAIL_CALL | COUNT | Tail call (replace frame) | [§17](#kos_op_tail_call) |
| TAIL_CALL_NAMES | COUNT | Tail call with keyword names | [§17](#kos_op_tail_call_names) |
| TAIL_CALL_STAR | STAR | Tail call with *args and/or **kwargs | [§17](#kos_op_tail_call_star) |
| TAIL_CALL_ATTR | EXTRA | Tail call obj.name with N positional args | [§17](#kos_op_tail_call_attr) |
| TAIL_CALL_ATTR_NAMES | EXTRA | Tail call obj.name with positional + keyword args | [§17](#kos_op_tail_call_attr_names) |
| TAIL_CALL_ATTR_STAR | EXTRA | Tail call obj.name with *args/**kwargs | [§17](#kos_op_tail_call_attr_star) |
| TAIL_CALL_VAR | EXTRA | Tail call callable from namespace by name | [§17](#kos_op_tail_call_var) |
| TAIL_CALL_VAR_NAMES | EXTRA | Tail call by name with keyword args | [§17](#kos_op_tail_call_var_names) |
| TAIL_CALL_VAR_STAR | EXTRA | Tail call by name with *args/**kwargs | [§17](#kos_op_tail_call_var_star) |
| TAIL_CALL_VAR_ATTR | EXTRA | Tail call obj.name; obj from VAR | [§17](#kos_op_tail_call_var_attr) |
| TAIL_CALL_VAR_ATTR_NAMES | EXTRA | Tail call obj.name with keyword args; obj from VAR | [§17](#kos_op_tail_call_var_attr_names) |
| TAIL_CALL_VAR_ATTR_STAR | EXTRA | Tail call obj.name with *args/**kwargs; obj from VAR | [§17](#kos_op_tail_call_var_attr_star) |
| TAIL_CALL_FAST | EXTRA | Tail call callable from fast local slot | [§17](#kos_op_tail_call_fast) |
| TAIL_CALL_FAST_NAMES | EXTRA | Tail call from fast slot with keyword args | [§17](#kos_op_tail_call_fast_names) |
| TAIL_CALL_FAST_STAR | EXTRA | Tail call from fast slot with *args/**kwargs | [§17](#kos_op_tail_call_fast_star) |
| TAIL_CALL_FAST_ATTR | EXTRA | Tail call obj.name; obj from fast slot | [§17](#kos_op_tail_call_fast_attr) |
| TAIL_CALL_FAST_ATTR_NAMES | EXTRA | Tail call obj.name with keyword args; obj from fast slot | [§17](#kos_op_tail_call_fast_attr_names) |
| TAIL_CALL_FAST_ATTR_STAR | EXTRA | Tail call obj.name with *args/**kwargs; obj from fast slot | [§17](#kos_op_tail_call_fast_attr_star) |
| PUSH_FOREACH | NOARG | Push foreach region; break target from extent table | [§18](#kos_op_push_foreach) |
| PUSH_WITH | CONST | Pop the manager, call enter, bind the result, push a single-shot WITH node and a fresh segment | [§18](#kos_op_push_with) |
| POP_WITH | NOARG | Call exit, pop the owned segment and the node, push None | [§18](#kos_op_pop_with) |
| PUSH_DYN_WITH | CONST | As PUSH_WITH, with a multi-shot node that takes part in continuation transfer plans | [§18](#kos_op_push_dyn_with) |
| POP_DYN_WITH | NOARG | As POP_WITH, for the multi-shot node | [§18](#kos_op_pop_dyn_with) |
| PUSH_COLLECT | CONST | Push collector namespace scope | [§18](#kos_op_push_collect) |
| POP_COLLECT | NOARG | Pop collect scope, push ns | [§18](#kos_op_pop_collect) |
| BUILD_CLASS | NOARG | Consume pos+ns, call metaclass | [§18](#kos_op_build_class) |
| PUSH_LOOP | NOARG | Push loop region; break target from extent table | [§18](#kos_op_push_loop) |
| PUSH_NS | EXTRA | Push namespace scope (nspec + names in extra row) | [§18](#kos_op_push_ns) |
| PUSH_TRY | LABEL | Push try region | [§18](#kos_op_push_try) |
| PUSH_FINALLY | LABEL | Push finally region and its segment | [§18](#kos_op_push_finally) |
| CLEAR_FINALLY | NOARG | Disable the top FINALLY region on non-local exit; POP_FINALLY still runs at the target | [§18](#kos_op_clear_finally) |
| CLEAR_LOOP | NOARG | Disable top LOOP region (before POP_LOOP) | [§18](#kos_op_clear_loop) |
| CLEAR_FOREACH | NOARG | Disable top FOREACH region (before POP_FOREACH) | [§18](#kos_op_clear_foreach) |
| CLEAR_TRY | NOARG | Disable the top TRY region on non-local exit; POP_TRY still drops the segment | [§18](#kos_op_clear_try) |
| POP_TRY | NOARG | Promote the one value on the try segment, drop the segment, pop the region | [§18](#kos_op_pop_try) |
| POP_FINALLY | NOARG | As POP_TRY, for the finally segment | [§18](#kos_op_pop_finally) |
| POP_LOOP | NOARG | Pop loop region; push loop result | [§18](#kos_op_pop_loop) |
| POP_FOREACH | NOARG | Pop foreach region; push loop result | [§18](#kos_op_pop_foreach) |
| POP_NS | NOARG | Pop the NS scope; the revealed node's namespace becomes active and leftover values are delivered to its segment | [§18](#kos_op_pop_ns) |
| OBJECT_COMPARE | CMPCOUNT | Chain-compare N stack values under the packed selector and push the result | [§19](#kos_op_object_compare) |
| OBJECT_COMPARE_VAR | EXTRA | The ternary bounds idiom with the middle operand resolved by name | [§19](#kos_op_object_compare_var) |
| OBJECT_COMPARE_FAST | EXTRA | The ternary bounds idiom with the middle operand from a fast slot | [§19](#kos_op_object_compare_fast) |
| OBJECT_OPERAND | OPCOUNT | Reduce the N operands left to right under the operator | [§19](#kos_op_object_operand) |
| OBJECT_OPERAND_CONST | EXTRA | The stack top under the operator with a pooled right operand | [§19](#kos_op_object_operand_const) |
| VAR_OPERAND_CONST | EXTRA | A named left operand and a pooled right operand under the operator, the result pushed | [§19](#kos_op_var_operand_const) |
| FAST_OPERAND_CONST | EXTRA | A fast-slot left operand and a pooled right operand under the operator, the result pushed | [§19](#kos_op_fast_operand_const) |
| OBJECT_OPERAND_VAR | EXTRA | The stack top under the operator with a named right operand | [§19](#kos_op_object_operand_var) |
| OBJECT_OPERAND_FAST | EXTRA | The stack top under the operator with a fast-slot right operand | [§19](#kos_op_object_operand_fast) |
| VAR_OPERAND_VAR | EXTRA | Two named operands under the operator, the result pushed | [§19](#kos_op_var_operand_var) |
| FAST_OPERAND_FAST | EXTRA | Two fast-slot operands under the operator, the result pushed | [§19](#kos_op_fast_operand_fast) |
| OBJECT_INPLACE | OPCOUNT | In-place reduce the N operands left to right, through the `PyNumber_InPlace*` family | [§19](#kos_op_object_inplace) |
| OBJECT_BIT_INVERT | NOARG | Pop TOS, push its bitwise inverse | [§19](#kos_op_object_bit_invert) |
| OBJECT_DIR | NOARG | Pop TOS, push `dir(TOS)` | [§19](#kos_op_object_dir) |
| OBJECT_DIVMOD | COUNT | Pop right then left, push `divmod(left, right)` | [§19](#kos_op_object_divmod) |
| OBJECT_ITER | NOARG | Acquire an iterator over either protocol; a Koskari async generator passes through as itself | [§19](#kos_op_object_iter) |
| VAR_ITER | VAR | Fused VAR + OBJECT_ITER: push the named iterable's iterator | [§12](#kos_op_var_iter) |
| FAST_ITER | FASTVAR | As VAR_ITER, with the iterable loaded from its fast slot | [§13](#kos_op_fast_iter) |
| OBJECT_LEN | NOARG | Pop TOS, push `len(TOS)` | [§19](#kos_op_object_len) |
| OBJECT_NOT | NOARG | Pop TOS, push its logical negation | [§19](#kos_op_object_not) |
| OBJECT_REPR | NOARG | Pop TOS, push `repr(TOS)` | [§19](#kos_op_object_repr) |
| OBJECT_STR | COUNT | Build a string from the top N values (f-string assembly) | [§19](#kos_op_object_str) |
| OBJECT_FORMAT | COUNT | Format the object; arg 2 takes the rules from the stack, arg 1 uses none | [§19](#kos_op_object_format) |
| OBJECT_FORMAT_CONST | CONST | Format the object with format rules from the pool | [§19](#kos_op_object_format_const) |
| VAR_FORMAT | VAR | Fused VAR + OBJECT_FORMAT: push `format(obj)` for the named object | [§12](#kos_op_var_format) |
| VAR_FORMAT_CONST | EXTRA | Fused VAR + OBJECT_FORMAT_CONST: format the named object with pooled rules | [§12](#kos_op_var_format_const) |
| RETURN_VAR | VAR | Return the name's value; the `VAR` + `RETURN 1` fusion | [§12](#kos_op_return_var) |
| YIELD_VAR | VAR | Yield the name's value; the `VAR` + `YIELD 1` fusion | [§12](#kos_op_yield_var) |
| FAST_FORMAT | FASTVAR | As VAR_FORMAT, with the object loaded from its fast slot | [§13](#kos_op_fast_format) |
| FAST_FORMAT_CONST | EXTRA | As VAR_FORMAT_CONST, with the object loaded from its fast slot | [§13](#kos_op_fast_format_const) |
| RETURN_FAST | FASTVAR | Return the slot's value; lowering of RETURN_VAR | [§13](#kos_op_return_fast) |
| YIELD_FAST | FASTVAR | Yield the slot's value; lowering of YIELD_VAR | [§13](#kos_op_yield_fast) |
| OBJECT_TRUTH | NOARG | Pop TOS, push its truth value | [§19](#kos_op_object_truth) |
| OBJECT_TYPE | NOARG | Pop TOS, push `type(TOS)` | [§19](#kos_op_object_type) |
| OBJECT_VARS | NOARG | Pop TOS, push its `__dict__` | [§19](#kos_op_object_vars) |
| PUSH_PROMPT | CONST | Push prompt (delimited cont.) | [§20](#kos_op_push_prompt) |
| PUSH_SHIFT | CONST | Capture continuation, enter shift block | [§20](#kos_op_push_shift) |
| POP_SHIFT | NOARG | Return from shift block without invoking k | [§20](#kos_op_pop_shift) |
| POP_PROMPT | NOARG | Pop prompt | [§20](#kos_op_pop_prompt) |
| PUSH_CC | NOARG | Push an undelimited continuation block onto the dynamic chain | [§21](#kos_op_push_cc) |
| POP_CC | NOARG | Pop the CC block; also the landing site when a captured continuation is invoked | [§21](#kos_op_pop_cc) |
| CAPTURE_CC | COUNT | Capture the CC extent's continuation and push it; 1 sets the callee on TOS aside for the capture | [§21](#kos_op_capture_cc) |
| PUSH_WIND | COUNT | Pop the before (and after) thunks, push the wind extent, run before | [§22](#kos_op_push_wind) |
| POP_WIND | NOARG | Pop the body result and the wind node, run the after thunk, deliver the result | [§22](#kos_op_pop_wind) |
| PUSH_PARAM | CONST | Pop N values, bind them to the pooled names, enter the parameterize extent | [§23](#kos_op_push_param) |
| POP_PARAM | NOARG | Pop the param extent and restore the cells it displaced | [§23](#kos_op_pop_param) |
| PARAM | EXTRA | Read a parameter by name; count 1 supplies a stacked default, count 0 raises NameError on a miss | [§23](#kos_op_param) |
| PARAM_CONST | EXTRA | Read a parameter by name with a pooled default; never raises for an unbound name | [§23](#kos_op_param_const) |
| THEEND | NOARG | Unreachable; no runtime | [§24](#kos_op_theend) |

**Completeness:** Every member of the `KosOpCode` enum in `koskari/lib/opcodes.incl` has a dedicated `### KOS_OP_*` heading in this document (some sections group related opcodes under one narrative first, then list each name). Search for `### KOS_OP_` to enumerate. Design-only or future opcodes belong in design notes, not here, until they are added to `opcodes.incl`.


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
| 2     | LABEL  | 0   | —        | target "done"; no-op if executed |
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
| 0     | POSITION| 0   | —        | line 1, col 0 (in position table); no-op if executed |
| 1     | CONST   | 0   | 0 (1)    | stack: [..., 1] |
| 2     | RETURN  | 1   | 1        | |


## 4. Stack

### KOS_OP_DUP

- **Style**: COUNT. Arg = number of elements to duplicate.
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
- **Stack**: `[..., a_1, ..., a_n] → [...]` (pop `arg` elements).
- **Invariants**: Stack must have at least `arg` elements.

**Example:** POP 1: discard TOS.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (1)    | stack: [..., 1] |
| 1     | CONST  | 1   | 1 (2)    | stack: [..., 1, 2] |
| 2     | POP    | 1   | 1        | stack: [..., 1] |
| 3     | RETURN | 1   | 1        | |


### KOS_OP_UNPACK {#kos_op_unpack}

- **Style**: COUNT. Arg = `n` (number of values to push).
- **Stack**: `[..., raw] → [..., v_{n-1}, ..., v_1, v_0]` where `v_i` is the
  `i`-th element of the realized sequence and **TOS = v_0 = seq[0]** (so the
  first binding in source order can use `VAR_STORE` / `FAST_STORE` on TOS
  first). Implementation: pop TOS and materialize it through
  `KosContext_PopUnpackSequence` (an exact tuple or list is used as is,
  anything else goes through `PySequence_Fast`), require `len(seq) == n`, then `KosStack_PushRef` for indices `n-1` down to `0`,
  then `Py_DECREF(seq)`.
- **Invariants**: `n >= 0`. For `n == 0`, TOS must be an empty sequence after
  realization.
- **Errors**: `ValueError` if length ≠ `n`; `TypeError` when TOS is not
  iterable.
- **Compiler form:** Pre-compile / post-compile arg = `n` (uint16).

**Example:** Unpack `(1, 2, 3)` so TOS is `1`, then return three values (stack
bottom to top matches push order).

| index | opcode  | arg | arg view | comment |
|------:|---------|-----|----------|---------|
| 0     | CONST   | 0   | 0 ((1,2,3)) | stack: [..., (1,2,3)] |
| 1     | UNPACK  | 3   | 3        | stack: [..., 3, 2, 1]; TOS = 1 |
| 2     | RETURN  | 3   | 3        | |

---

### KOS_OP_UNPACK_STAR {#kos_op_unpack_star}

- **Style**: EXTRA (`COUNT`, `COUNT`, `NOARG`, `NOARG`). Instruction arg =
  index into the code object's **extra** table; that row has `a0 = left`,
  `a1 = right` (both non-negative counts).
- **Semantics**: Pop TOS and materialize it as UNPACK does. Require `left + right > 0`
  and `len(seq) >= left + right`. Let `L = len(seq)`. The **middle** slice is
  `seq[left : L - right]` (inclusive start, exclusive end), materialized as a
  **tuple** (new reference). Pushes are ordered so that after all pushes,
  **TOS is the first prefix element** `seq[0]` (when `left > 0`):
  1. **Suffix** scalars: indices `L-1, L-2, ..., L-right` (each via
     `KosStack_PushRef`).
  2. **Middle**: one value — the tuple — via `KosStack_PUSH` (**stolen**
     reference from `PySequence_Tuple`).
  3. **Prefix** scalars: indices `left-1, ..., 0` (each via `PushRef`).
- **Stack (TOS right)**: `[..., raw] → [..., seq[L-1], ..., seq[L-right],
  middle, seq[left-1], ..., seq[0]]` when all parts are non-empty; adjust when
  `left` or `right` is zero (no prefix or suffix pushes for that side).
- **Invariants**: Extra row matches schema; `left + right` fits in uint16;
  realized value must be iterable.
- **Errors**: `ValueError` if `left + right == 0` or `len(seq) < left + right`.
- **Compiler form:** Pre-compile extra operand is a 4-tuple
  `(left, right, 0, 0)`; `NOARG` slots are ignored by the assembler. Same
  pattern as other `EXTRA` opcodes (see §1.7).

**Example:** `left=2`, `right=1`, seq `(1,2,3,4)`: suffix `[4]`, middle
`(3,)`, prefix `2, 1` with TOS `1`.

| index | opcode       | arg | arg view   | comment |
|------:|--------------|-----|------------|---------|
| 0     | CONST        | 0   | 0 ((1,2,3,4)) | |
| 1     | UNPACK_STAR  | 0   | 0 (2, 1, …) | extra row: left=2, right=1 |
| 2     | RETURN       | 4   | 4          | stack bottom..top: 4, (3,), 2, 1 |

---

### KOS_OP_UNPACK_VAR {#kos_op_unpack_var}

- **Style**: EXTRA (`VAR`, `VAR`, `NOARG`, `NOARG`). Row: `a0`, `a1` = constant
  pool indices of the two names (strings).
- **Stack**: `[..., raw] → [...]`. No values pushed; **STORE** semantics only
  (same as `VAR_STORE` / `KosNamespace_Store` through `frame_ns_store`): `seq[0]`
  into the first name, `seq[1]` into the second. Pop TOS and materialize it
  as UNPACK does, require `len == 2`, own both items, release the sequence,
  then store; owning both before the first store keeps the second safe from
  a finalizer the first store runs.
- **Invariants**: Both names must already be bound in the frame namespace (see
  §1.9 STORE vs BIND).
- **Errors**: `ValueError` if length ≠ 2.
- **Compiler form:** Pre-compile extra tuple `(name0, name1, 0, 0)`; names
  intern to cpool like other `VAR` extra fields.

---

### KOS_OP_UNPACK_FAST {#kos_op_unpack_fast}

- **Style**: EXTRA (`FASTVAR`, `FASTVAR`, `NOARG`, `NOARG`). Row: `a0`, `a1` =
  fast local slot indices.
- **Stack**: `[..., raw] → [...]`. Pop TOS and materialize it as UNPACK does,
  require `len == 2`, own both items before either store, then `KosNamespace_FastStore` for slot `a0` with `seq[0]` and slot `a1` with
  `seq[1]` (same ownership rules as `FAST_STORE`: namespace increments ref as
  needed).
- **Invariants**: Frame namespace supports fast locals; both indices in range
  (same as `FAST_STORE`).
- **Errors**: `ValueError` if length ≠ 2; `RuntimeError` / `NameError` if fast
  namespace invalid or index out of bounds.
- **Compiler form:** Pre-compile extra tuple `(slot0, slot1, 0, 0)` with
  integer slot indices.

---

## 5. Constants

### KOS_OP_CONST

- **Style**: CONST. Arg = constant pool index (post-compile).
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


### 5.1 Pair construction and access

### KOS_OP_CAR {#kos_op_car}

- **Style**: COUNT. Arg = repeat count: apply **car** that many times to the
  stack top (each step pops one pair and pushes its car).
- **Invariants**: **`n` ≥ 1**. **`n` ≤ 0** (including **CAR 0**) is invalid:
  **RuntimeError** (`CAR invoked with invalid argument count: …`); the stack is
  not modified because the handler returns before popping TOS.
- **Stack**: With valid **`n` ≥ 1**, same effect as **`n`** chained **CAR 1**
  instructions (one stack pop of the initial value, local walk, one push). If
  any intermediate value is not a pair or is nil, **TypeError** (message
  includes step index and type / `nil`).


### KOS_OP_CDR {#kos_op_cdr}

- **Style**: COUNT. Arg = repeat count: apply **cdr** that many times to the
  stack top (each step pops one pair and pushes its cdr).
- **Invariants**: **`n` ≥ 1**. **`n` ≤ 0** (including **CDR 0**) is invalid:
  **RuntimeError** (`CDR invoked with invalid argument count: …`); the stack is
  not modified because the handler returns before popping TOS.
- **Stack**: With valid **`n` ≥ 1**, same effect as **`n`** chained **CDR 1**
  instructions (one stack pop, local walk, one push). If any intermediate value
  is not a pair or is nil, **TypeError** (message includes step index and type /
  `nil`).


### KOS_OP_CONS {#kos_op_cons}

- **Style**: COUNT. Arg = number of values on stack (bottom = first element,
  top = tail).
- **Stack**: `[..., e_0, ..., e_{count-1}] → [..., result]` where result =
  (e_0 . (e_1 . ... (e_{count-1}) ...)). Implemented via KosStack_TailPtr,
  KosPair_ConsFromPtr, KosStack_Drop, then push result. count 0 → push
  nil (INCREF).


### KOS_OP_PROPER {#kos_op_proper}

- **Style**: COUNT. Arg = number of element values on stack.
- **Stack**: `[..., e_0, ..., e_{count-1}] → [..., result]` where result is the
  proper list `(e_0 e_1 ... e_{count-1})`. nil is implicitly appended as the
  tail. count 0 → push nil directly.
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


### KOS_OP_BRACKET_CONS {#kos_op_bracket_cons}

- **Style**: COUNT. Arg = number of values on stack (bottom = first element,
  top = tail).
- **Stack**: `[..., e_0, ..., e_{count-1}] → [..., result]` where result is
  the bracket pair chain `[e_0 . [e_1 . ... [e_{count-1}] ...]]`: the twin of
  CONS over bracket pairs, via KosBracketPair_ConsFromPtr. count 0 → push
  bracket-nil.
- **Use-case**: `compile/bracket-cons`, and the quasiquote compiler for a
  `[ ]` form with an explicit tail. Before these two opcodes the quasiquote
  called the `bracket-cons` builtin by name, which a binding of that name
  shadowed.


### KOS_OP_BRACKET_PROPER {#kos_op_bracket_proper}

- **Style**: COUNT. Arg = number of element values on stack.
- **Stack**: `[..., e_0, ..., e_{count-1}] → [..., result]` where result is the
  proper bracket list `[e_0 e_1 ... e_{count-1}]`; bracket-nil is the
  implicit tail. count 0 → push bracket-nil directly.
- **Use-case**: `compile/bracket-proper`, and the quasiquote compiler for the
  segments of a `[ ]` form; the twin of PROPER.


### KOS_OP_PAIR_SPLICE {#kos_op_pair_splice}

- **Style**: COUNT. Arg = number of segments on stack (bottom = first segment,
  top = last segment).
- **Stack**: `[..., seg_0, ..., seg_{count-1}] → [..., result]` where result is
  the concatenation of all segments as a single proper list. Each segment may be:
  nil (contributes nothing), a pair (proper or improper; its elements are
  appended), a Python tuple (its elements are appended), or any iterable.
  count 0 → push nil.
- **Invariants**: All segments must be iterable; non-iterable values raise
  TypeError. The result is always a proper pair list (or nil if all segments are
  empty). Dotted-pair segments are walked until nil; the implicit nil tail of
  the last segment is not included.
- **Use-case**: Emitted by the quasiquote compiler when at least one
  `unquote-splicing` site is present in a list. Each constant-only segment is
  folded into a single `CONST tuple` (which PAIR_SPLICE accepts natively),
  reducing instruction count significantly for mostly-literal lists.

**Example:** Splice a variable list `b` between constants.
`` `(a ,@b c) `` compiles to roughly:

| index | opcode      | arg | arg view        | comment |
|------:|-------------|-----|-----------------|---------|
| 0     | CONST       | 0   | 0 ((sym a,))    | segment: tuple `(a,)` |
| 1     | VAR         | 1   | 1 (b)           | splice value |
| 2     | CONST       | 2   | 2 ((sym c,))    | segment: tuple `(c,)` |
| 3     | PAIR_SPLICE | 3   | 3               | stack: [..., (a . (b_0 ... b_n . (c)))] |


### KOS_OP_NILP {#kos_op_nilp}

- **Style**: NOARG.
- **Stack**: `[..., x] → [..., bool]`. Pop TOS; push `True` when it is an
  empty-list singleton (`nil` or bracket-nil), else `False`.
- **Compiler form:** `Compiler.compress` lowers NILP followed by JUMP_FALSE
  into one [CHECK_NIL](#kos_op_check_nil).
- **Implementation**: `koskari/lib/opcode/NILP.c`


## 6. Mode control

### KOS_OP_AWAIT

- **Style**: COUNT (stack element count).
- **Arg**: `1` — pop TOS (awaitable), dispatch; may return `EVAL_AWAIT(val)` or push the resolved value; on resume from a Python awaitable, result is pushed onto the operand stack. `0` — bare cooperative suspend: no stack pop or push; returns `EVAL_AWAIT(Py_None)` (no `py_await_iter`); `revive_await` does not push the resume value onto the operand stack (e.g. next instruction may be `CONST None` for `(compile/await)`).

**Example:** Await a coroutine; TOS is the awaitable. On resume, result is pushed.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (coro)  | stack: [..., awaitable] |
| 1     | AWAIT  | 1   | 1        | pop one; suspend; on resume stack: [..., value] |
| 2     | RETURN | 1   | 1        | |

---

### KOS_OP_CONST2 {#kos_op_const2}

- **Style**: EXTRA (CONST, CONST, NOARG, NOARG). Row = two constant pool indices (post-compile).
- **Stack**: `[...] → [..., a, b]`. Push the two constants in row order. A constant is
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

---

### KOS_OP_CONST3 {#kos_op_const3}

- **Style**: EXTRA (CONST, CONST, CONST, NOARG). Row = three constant pool indices (post-compile).
- **Stack**: `[...] → [..., a, b, c]`. Push the three constants in row order. A constant is
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

---

### KOS_OP_CONST4 {#kos_op_const4}

- **Style**: EXTRA (CONST, CONST, CONST, CONST). Row = four constant pool indices (post-compile).
- **Stack**: `[...] → [..., a, b, c, d]`. Push the four constants in row order. A constant is
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

---

### KOS_OP_BREAK

- **Style**: COUNT. Arg = number of stack elements to pop before jumping.
- **Stack**: Pops `arg` elements; control transfers to break target of innermost LOOP/FOREACH extent.
- **Invariants**: Must be inside a LOOP or FOREACH extent.

**Example:** Inside a loop; pop 0 and jump to extent break_pc (instruction after the matching POP_LOOP or POP_FOREACH).

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 2     | BREAK  | 0   | 0        | pop 0, jump to break target (e.g. pc 6) |
| …     | …      | …   | …        | |
| 6     | POP_LOOP | 0 | 0        | break target (LOOP extent; use POP_FOREACH for FOREACH) |

---

### KOS_OP_CONTINUE

- **Style**: COUNT. Arg = number of stack elements to pop before jumping.
- **Stack**: Pops `arg` elements; control transfers to continue target of innermost LOOP/FOREACH extent.
- **Invariants**: Must be inside a LOOP or FOREACH extent. The back-edge of every compiled loop, and so a cooperation point (a matcher REPEAT's back-edge polls at `MATCH_APPEND`): the handler ends in **`RESULT_NEXT_POLL`**, which hands the trampoline the next step instead of threading when the eval breaker is up (EVALUATOR.md §1.6).

**Example:** Inside a loop; pop 0 and jump to extent continue_pc (e.g. loop head).

| index | opcode   | arg | arg view | comment |
|------:|---------|-----|----------|---------|
| 4     | CONTINUE| 0   | 0        | pop 0, jump to continue target (e.g. pc 1) |
| …     | …       | …   | …        | |

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

- **Style**: COUNT. Arg = number of values to leave on stack as return value (typically 1).
- **Stack**: Pops the return value(s) per `arg` and tail-calls **`kos_return_deliver`**, which threads its own common shapes: a **plain bytecode parent** (**`kos_return_is_plain`** → **`eval_plain_return`**: retarget the live registers, free the callee, push `val`, tail-call the parent's next instruction); a **region** over such a parent (**`kos_return_is_region`** → **`eval_region_return`**: the same retarget; a WITH or WIND, which carry **`KOS_DYN_CAP_CONTROL_HOOK`**, then runs its control-returned hook); a region with no hook over a **frame waiting on a resume** (**`kos_return_is_region_resume`** → **`eval_region_resume_return`** → **`RESUME_HOP`**); and a **resume-carrying parent** (**`kos_return_is_resume`** → **`kos_return_take_resume`** → **`RESUME_HOP`**). Every other shape goes to **`eval_event_return`**: **`kos_dyn_unwind_return`** over the extents above the frame, a return across a delimiting reset, or the delivery to the parent; a RESUME answer goes through the relay into the live frame's resume. A root frame, or a **GEN** that an adapter runs: **`KOS_RES_RETURN`** from **`koskari_eval`**. A GEN with a parent and no resume waiting is **`EVAL_FATAL`**.
- **Invariants**: See execution-invariants: in-loop flattening for RETURN.
- **See also**: [EVALUATOR.md](EVALUATOR.md) §2.6–3; **`koskari/lib/opcode/RETURN.c`**.

**Example:** Return TOS (arg = 1). Pool: 0 = 42.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (42)   | stack: [..., 42] |
| 1     | RETURN | 1   | 1        | return 42 (`eval_event_return` or in-loop parent push) |

---

### KOS_OP_YIELD

- **Style**: COUNT. Arg = number of values to yield (typically 1).
- **Stack**: Pops the yielded value(s) and tail-calls **`kos_yield_deliver`**. Sets **`KOS_CODE_YIELDS`** on the code object. **`eval_event_yield`** freezes the running frame's GEN (**`kos_dyn_gen_freeze`**). When the live frame then has a resume -- a flattened consumer: a for-each step, a next request, a yield-from -- **`iter_deliver_to_resume`** hands it the value and the relay's RESUME arm threads into that resume without stopping **`koskari_eval`**. Otherwise the yield is terminal: **`KOS_RES_YIELD`** with **`KOS_SUSP_YIELD`** for the adapter that runs the generator.
- **Invariants**: Code is treated as async generator when YIELDS flag set.
- **See also**: [EVALUATOR.md](EVALUATOR.md) §2.6, §5.7; **`koskari/lib/opcode/YIELD.c`**.

**Example:** Yield TOS (arg = 1). Pool: 0 = 100.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (100)  | stack: [..., 100] |
| 1     | YIELD  | 1   | 1        | yield 100; on resume continue |

---

### KOS_OP_RETURN_CONST

- **Style**: CONST. Arg = pool index of the constant to return.
- **Stack**: `[...] -> [...]`. Returns the constant exactly as RETURN 1 returns a popped value: the handler takes a reference and tail-calls `kos_return_deliver`, the KosThreadFn-shaped delivery every RETURN row ends in (plain, region and resume shapes thread on; anything else signals RETURN).
- **Compiler form:** Produced by `ByteCodeProcessor.fuse_feeders` when RETURN 1 is fed by a bare CONST; a `None` constant takes RETURN 0 in the squish pass first. The epilogue's appended RETURN 1 after a constant body fuses the same way.
- **Implementation**: `koskari/lib/opcode/RETURN_CONST.c`
- **Covered by**: `tests/lib/opcode/test_control_operand.py`,
  `tests/compile/test_fuse_feeders.py`

---

### KOS_OP_YIELD_CONST

- **Style**: CONST. Arg = pool index of the constant to yield.
- **Stack**: `[...] -> [...]`. Yields the constant exactly as YIELD 1 yields a popped value: the handler takes a reference and tail-calls `kos_yield_deliver`, the delivery every YIELD row ends in. Sets **`KOS_CODE_YIELDS`** at finalize (`kos_op_yields`) and counts as a yield for tail-call demotion.
- **Compiler form:** Produced by `ByteCodeProcessor.fuse_feeders` when YIELD 1 is fed by a bare CONST; a `None` constant takes YIELD 0 in the squish pass first.
- **Implementation**: `koskari/lib/opcode/YIELD_CONST.c`
- **Covered by**: `tests/lib/opcode/test_control_operand.py`,
  `tests/compile/test_fuse_feeders.py`

---

### KOS_OP_YIELD_FROM

- **Style**: NOARG.
- **Stack**: Consume TOS (subexpr_result) and hop into the delegate kind's drive with a plain next; the handler runs once per delegation. The delegating frame's resume is the delegation: its state is the delegate, its kind is the delegate's kind and the phase (a drive waiting on the consumer's next or send, a throw kind waiting on a throw, the emit kind waiting on an installed provider's element, an await kind waiting on a python async step). Each delivery runs the phase, which sets the next; the opcode itself is not re-entered. At the end the delegate is released and the frame threads on with nothing pushed; the delegate's return value (the StopIteration or StopAsyncIteration value) is discarded. The pc sits past the instruction from first entry.
- **Execution model**: Single-opcode delegation primitive (conduit); not loop sugar. Full send/throw/close propagation; async flattening for Python and Koskari async gens. No chain node: the delegation lives in the frame's resume slot pair, and the kind's clear closes the delegate on a teardown with no delivery coming.
- **Delegate kinds** (chosen at first entry by normalizing TOS): **sync iterator** (PyIter_Next); **sync generator** (send/throw/close); **Python async iterator** (__anext__); **Python async generator** (__anext__/asend/athrow/aclose); **Koskari async generator** (driven in-process via koskari_eval). Callables are called once; the return value is classified as above. Fallback: `iter(obj)` for sync iteration.
- **Rejected**: Coroutines (Koskari or Python). Normalization raises `TypeError: "coroutine object is not async iterable"`. Yielded values are never re-awaited; they are emitted as yields only.
- **Invariants**: TOS on first entry must be an iterable or async-iterable (or callable returning one). The frame keeps a resume set, one phase succeeding the next, until the delegate ends; every yield of an element (boundary **`KOS_RES_YIELD`**) and every suspension on an await (boundary **`KOS_RES_AWAIT`**) leaves the next phase set. The frame is never suspended toward the consumer while the delegate's frames are installed, which is what lets one resume slot carry both directions.
- **Implementation**: Handler and every YIELD_FROM helper (normalize and classify; five drive kinds, one per delegate kind, each naming its throw kind; the emit kind and the two await kinds; the kind's clear and clone) live in `koskari/lib/opcode/YIELD_FROM.c` (included into `eval.c`). See [koskari/lib/opcode/README.md](../../koskari/lib/opcode/README.md) for the opcode-fragment layout.
- **Design**: [design/archive/yield-from-execution-model.md](archive/yield-from-execution-model.md) (archived narrative; see [EVALUATOR.md](EVALUATOR.md) §5)
- **Covered by**: `tests/lib/opcode/test_yield_from.py` (sync/async delegates, Koskari async gen, send/throw/close, final values, type errors).

**Example:** Delegate to a sync iterator; each yielded value is emitted; nothing is pushed when the iterator exhausts. Pool: 0 = [1, 2, 3].

| index | opcode   | arg | arg view | comment |
|------:|---------|-----|----------|---------|
| 0     | CONST   | 0   | 0 ([1,2,3]) | stack: [..., iterable] |
| 1     | YIELD_FROM | 0 | 0        | consume TOS as the delegate and hop into its drive; each delivery runs the phase set on the frame; at the end nothing is pushed |
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
| 2     | ITER_NEXT| 6   | 6 (-> 6)  | next from region; if exhausted → pc=6. stack: [..., val] or jump |
| 3     | POP      | 1   | 1        | consume value (body) |
| 4     | JUMP     | 2   | 2 (-> 2) | loop back |
| 5     | POP_FOREACH | 0 | 0        | push loop result (None if unset) |
| 6     | LABEL    | —   | —        | else (exhausted) |

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
| 5     | LABEL    | —   | —        | else (exhausted) |

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
| 5     | LABEL    | —   | —        | else (exhausted) |

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


### KOS_OP_VAR_NEXT {#kos_op_var_next}

- **Style**: EXTRA (VAR, COUNT). Row = iterator name (pool), n (1 or 2).
- **Stack**: n=1: `[...] -> [..., value]`; n=2: `[..., default] -> [..., value-or-default]`. Fused VAR + OBJECT_NEXT: resolve the iterator name and make the next request (**`KosContext_NextResume`**, the arity picking the resume's kind; the iterator is the resume's owned state, the default stays on the stack under the halted instruction). A Koskari async generator runs flattened; an awaited step suspends the frame and completes the instruction when it resumes. 1-arg raises StopIteration on exhaustion; 2-arg delivers the default.
- **Invariants**: The fused name resolution runs after the default row (the fusion requires a bare default feeder, so the reorder is unobservable beyond NameError timing).
- **Compiler form:** Produced by `ByteCodeProcessor.fuse_feeders` when OBJECT_NEXT's iterator feeder is a bare VAR load (the 2-arg form also requires a bare VAR/CONST default). Lowers to FAST_NEXT through the generic schema-driven `convert_fast` path.
- **Implementation**: `koskari/lib/opcode/VAR_NEXT.c`
- **Covered by**: `tests/compile/test_fuse_feeders.py`,
  `tests/lib/opcode/test_object_next.py`

### KOS_OP_FAST_NEXT {#kos_op_fast_next}

- **Style**: EXTRA (FASTVAR, COUNT). Row = iterator slot, n (1 or 2).
- **Stack**: As VAR_NEXT, with the iterator loaded from its fast slot.
- **Compiler form:** Post-compile only: the generic `convert_fast` lowering of VAR_NEXT when the iterator name has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_NEXT.c`
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
| 1     | JUMP_FALSE | 4   | 4 (-> 4) | pop TOS; if false → pc=4 |
| 2     | CONST      | 1   | 1 (42)   | stack: [..., 42] |
| 3     | JUMP       | 5   | 5 (-> 5) | |
| 4     | LABEL      | —   | —        | else |
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
| 1     | JUMP_TRUE  | 3   | 3 (-> 3) | pop TOS; if true → pc=3 |
| 2     | CONST      | 0   | 0 (0)    | else path |
| 3     | LABEL      | —   | —        | then path |
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

### KOS_OP_JUMP_AND

- **Style**: LABEL. Short-circuit AND: jump if TOS is false.
- **Stack**: Pops one value; if false, push it back and jump to `arg`; if true,
  discard it and advance. Either way TOS is consumed on the fall-through path.
- **Invariants**: Jump locality for target.
- **Compiler form:** Pre-compile arg = string label; post-compile arg = pc.

**Compiler use-case:** `compile/and` is the canonical consumer
(`koskari/compile/specials/conditionals.py`). For `(compile/and A B C)` the
compiler emits one `JUMP_AND done` after each non-last term; the last term is
compiled without a jump. Because `JUMP_AND` already discards the value on the
fall-through path, no `POP` is needed between terms.

```
eval A
JUMP_AND done   ; falsy → push A back, jump to done
eval B          ; A was already discarded on fall-through
JUMP_AND done
eval C          ; last term — value stays on stack
done:
```

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
- **Stack**: Pops one value; if true, push it back and jump to `arg`; if false,
  discard it and advance. Either way TOS is consumed on the fall-through path.
- **Invariants**: Jump locality for target.
- **Compiler form:** Pre-compile arg = string label; post-compile arg = pc.

**Compiler use-case:** `compile/or` is the canonical consumer
(`koskari/compile/specials/conditionals.py`). For `(compile/or A B C)` the
compiler emits one `JUMP_OR done` after each non-last term; the last term is
compiled without a jump. Because `JUMP_OR` already discards the value on the
fall-through path, no `POP` is needed between terms.

```
eval A
JUMP_OR done    ; truthy → push A back, jump to done
eval B          ; A was already discarded on fall-through
JUMP_OR done
eval C          ; last term — value stays on stack
done:
```

**Example:** Short-circuit or: if first operand true, jump to "ok".

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 0     | CONST    | 0   | 0 (False)| stack: [..., a] |
| 1     | JUMP_OR  | 4   | 4 (-> 4) | if a → pc=4 |
| 2     | CONST    | 1   | 1 (False)| stack: [..., b] |
| 3     | JUMP_OR  | 4   | 4 (-> 4) | if b → pc=4 |
| 4     | LABEL    | —   | —        | ok |
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

Check jumps: jump to the target label when the predicate does not hold. Jump locality applies to every target. The comparison check is the selector-keyed **CHECK_COMPARE**, **EXTRA** with a `(CMP, COUNT, LABEL, RAW)` row: `a0` is a KosCompare selector, `a1` the operand count (≥ 2), `a2` the target pc, `a3` the negate flag. It is variadic — the top `COUNT` operands are chain-compared left-to-right under the selector (e.g. `a < b < c`), negated when `a3` is set, and the jump is taken when the chain fails. IS, IS_NOT, IN and NOT_IN are strictly binary at this opcode; IS_INSTANCE and IS_SUBCLASS test the first operand against every operand after it. The remaining checks (exception, nil) take a single **LABEL** arg.

### KOS_OP_CHECK_COMPARE {#kos_op_check_compare}

- **Style**: EXTRA (CMP, COUNT, LABEL, RAW). Row = KosCompare selector,
  operand count, target pc, negate flag.
- **Stack**: `[..., a1, ..., aN] → [...]`. Chain-compare the top COUNT
  operands under the selector, negated when the negate flag is set;
  jump to LABEL when the chain fails. The operands are dropped
  either way. Raises `TypeError` when `COUNT < 2`.
- **Invariants**: Selector is a valid KosCompare ordinal. The richcompare
  six (LT/LE/EQ/NE/GT/GE, ordinals 0-5 == CPython's Py_LT..Py_GE) chain
  over any COUNT; IS, IS_NOT, IN and NOT_IN require COUNT == 2;
  IS_INSTANCE and IS_SUBCLASS take any COUNT >= 2.
- **Compiler form:** Produced by `Compiler.compress` fusing
  `OBJECT_COMPARE` + `JUMP_FALSE` into one instruction, and
  `OBJECT_COMPARE` + `JUMP_TRUE` into the same with the flag set. Never
  emitted directly by
  specials.
- **Implementation**: `koskari/lib/opcode/CHECK_COMPARE.c`; core dispatch
  `kos_compare_chain` in `operator.c`.
- **Covered by**: `tests/lib/opcode/test_compare.py`,
  `tests/lib/opcode/test_checks.py`

**Example:** Branch unless `1 < x < 100`. Selector LT = 0.

| index | opcode        | arg | arg view          | comment |
|------:|---------------|-----|-------------------|---------|
| 0     | CONST         | 0   | 0 (1)             | stack: [..., 1] |
| 1     | FAST          | 0   | 0 (x)             | stack: [..., 1, x] |
| 2     | CONST         | 1   | 1 (100)           | stack: [..., 1, x, 100] |
| 3     | CHECK_COMPARE | 0   | (LT, 3, -> 6, 0)  | jump to 6 unless 1 < x < 100 |

---

### KOS_OP_CHECK_COMPARE_VAR {#kos_op_check_compare_var}

- **Style**: EXTRA (CMPSRC, VAR, VAR, LABEL). Row = packed selector +
  source bits, two operand slots, target pc; the negate bit makes the check test the negated comparison. A set source bit overrides
  that operand slot from its declared VAR kind to CONST (pool index).
- **Stack**: No stack effect. Arity-2 fused comparison branch: load both
  operands per the source bits (frame namespace name lookup, or constant pool), compare
  under the selector, jump to LABEL when the comparison, negated under a set negate bit, fails.
- **Invariants**: Selector is a valid KosCompare ordinal; both-CONST rows
  are never emitted (that is a folding case).
- **Compiler form:** Produced by `ByteCodeProcessor.fuse_feeders` (late `compress` sweep) absorbing two bare VAR/CONST feeder rows into an arity-2 [CHECK_COMPARE](#kos_op_check_compare). Runs before `pack_var_runs` so this fusion gets first claim on the feeders. Lowers to CHECK_COMPARE_FAST via the CMPSRC-aware `convert_fast` case (all-or-nothing on the name slots).
- **Implementation**: `koskari/lib/opcode/CHECK_COMPARE_VAR.c`
- **Covered by**: `tests/compile/test_fuse_feeders.py`,
  `tests/lib/opcode/test_compare.py`

**Example:** Loop condition `i < n` on fast slots 1 and 0.

| index | opcode             | arg | arg view                          | comment |
|------:|--------------------|-----|-----------------------------------|---------|
| 3     | CHECK_COMPARE_VAR | 2 | (LT, v#1 ('i'), v#0 ('n'), -> 9) | jump to 9 unless i < n |

---

### KOS_OP_CHECK_COMPARE_FAST {#kos_op_check_compare_fast}

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
| 4     | CHECK_EXCEPTION | 7   | 7 (-> 7) | pop typ; if exc not match → pc=7 |
| 5     | POP             | 1   | 1        | consume exc |
| 6     | RETURN          | 1   | 1        | handle |
| 7     | LABEL           | —   | —        | re-raise or next handler |


### KOS_OP_CHECK_NIL {#kos_op_check_nil}

- **Style**: LABEL. Arg = target pc (post-compile).
- **Stack**: `[..., x] → [...]`. Pop TOS; branch to arg when it is not an
  empty-list singleton; fall through when it is.
- **Invariants**: Jump locality for target. The squashed form of
  [NILP](#kos_op_nilp) followed by JUMP_FALSE.
- **Compiler form:** Produced by `Compiler.compress`; pre-compile arg = string
  label, post-compile arg = pc.
- **Implementation**: `koskari/lib/opcode/CHECK_NIL.c`


## 9.1 Switch dispatch

Switch dispatch opcodes implement multi-way **equality** branching. A typical
**checks** region runs comparisons in order: each comparison **falls through**
when it does not match so the next check can run without re-pushing the
scrutinee. A **match** jumps into a shared **body** (or the same body for
several keys).

**`KOS_OP_SWITCH_EQ`** compares two stack operands, discriminant **`left`**
below key **`right`** (TOS). On **match**, both are popped and the PC is set
to **`arg`** (the body). On **mismatch**, only **`right`** is popped; **`left`**
remains so the following code can push another key and use another
**`SWITCH_EQ`**.

The **single-slot** variants (**`SWITCH_EQ_CONST`**, **`SWITCH_EQ_VAR`**, …)
compare the scrutinee **`s`** at TOS to an RHS from the extra row. On
**match**, **`s`** is popped and the PC jumps to **`a0`**. On **mismatch**,
the stack is **unchanged** and execution falls through.

See [archive/switch-eq-opcodes.md](archive/switch-eq-opcodes.md) (archived) for
family rationale and the surface **`compile/switch`** form.

### KOS_OP_SWITCH_EQ

- **Style**: LABEL. Arg = target pc (post-compile).
- **Stack**: `[..., left, right]` (right = TOS). Compares `left == right` with
  the same equality semantics as [CHECK_COMPARE](#kos_op_check_compare) EQ
  (`PyObject_RichCompareBool` with `Py_EQ`).
  - **Match**: pop both operands; `pc = arg`. Stack after: `[...]`.
  - **Mismatch**: pop `right` only; fall through. Stack after: `[..., left]`.
- **Invariants**: Jump target is the matching body. After a mismatch, bytecode
  must leave `left` correct for the next comparison (push a new `right`) or
  reach cleanup that drops `left`.
- **Compiler form:** Pre-compile arg = string label (body); post-compile arg =
  pc.

**Example:** Two-arm chain then else. Pool: 0 = 10, 1 = 1, 2 = 20, 3 = 2, 4 =
0. Discriminant 10: first key 1 (mismatch — pop key, fall through with `[10]`);
second key 20 (match — pop both, jump to body); body returns 2. Else drops
disc and returns 0. Covered by `tests/lib/opcode/test_switch.py`.

| index | opcode    | arg | arg view  | comment |
|------:|-----------|-----|-----------|---------|
| 0     | CONST     | 0   | 0 (10)    | stack: [..., 10] |
| 1     | CONST     | 1   | 1 (1)     | stack: [..., 10, 1] |
| 2     | SWITCH_EQ | 8   | 8 (-> 8)  | match → pop2, pc=8 (body); mismatch → [..., 10], next insn |
| 3     | CONST     | 2   | 2 (20)    | stack: [..., 10, 20] |
| 4     | SWITCH_EQ | 8   | 8 (-> 8)  | match → body; mismatch → [..., 10], next insn |
| 5     | POP       | 1   | 1         | else: drop discriminant |
| 6     | CONST     | 4   | 4 (0)     | else result |
| 7     | RETURN    | 1   | 1         | |
| 8     | LABEL     | —   | —         | body |
| 9     | CONST     | 3   | 3 (2)     | stack: [..., 2] |
| 10    | RETURN    | 1   | 1         | |


### KOS_OP_SWITCH_EQ_CONST

- **Style**: EXTRA. Schema `(LABEL, CONST, NOARG, NOARG)`.
- **Stack**: `[..., s]`. Compare `s == cpool[a1]` with `Py_EQ`.
  - **Match**: pop `s`; `pc = a0`.
  - **Mismatch**: fall through; stack unchanged (`s` still TOS).
- **Compiler form:** tuple arg `("label", const_value, 0, 0)` pre-compile;
  post-compile row stores pc in `a0` and constant index in `a1`.
- **Implementation**: `koskari/lib/opcode/SWITCH_EQ_CONST.c`

### KOS_OP_SWITCH_EQ_VAR

- **Style**: EXTRA. Schema `(LABEL, VAR, NOARG, NOARG)`.
- **Stack**: `[..., s]`. Resolve `rhs = frame_ns_deref(var_name)` from `a1`,
  then compare `s == rhs` with `Py_EQ`.
  - **Match**: pop `s`; `pc = a0`.
  - **Mismatch**: fall through; stack unchanged.
- **Compiler form:** tuple arg `("label", "name", 0, 0)` pre-compile.
- **Implementation**: `koskari/lib/opcode/SWITCH_EQ_VAR.c`

### KOS_OP_SWITCH_EQ_FAST

- **Style**: EXTRA. Schema `(LABEL, FASTVAR, NOARG, NOARG)`.
- **Stack**: `[..., s]`. Resolve `rhs = KosNamespace_FastDeref(ns, a1)`, then
  compare `s == rhs` with `Py_EQ`.
  - **Match**: pop `s`; `pc = a0`.
  - **Mismatch**: fall through; stack unchanged.
- **Compiler form:** tuple arg `("label", slot_index, 0, 0)` pre-compile.
- **Implementation**: `koskari/lib/opcode/SWITCH_EQ_FAST.c`

### KOS_OP_SWITCH_EQ_VAR_ATTR

- **Style**: EXTRA. Schema `(LABEL, VAR, ATTR, NOARG)`.
- **Stack**: `[..., s]`. Resolve `obj = frame_ns_deref(var_name)` from `a1`,
  `rhs = getattr(obj, attr_name)` from `a2`, then compare `s == rhs`.
  - **Match**: pop `s`; `pc = a0`.
  - **Mismatch**: fall through; stack unchanged.
- **Compiler form:** tuple arg `("label", "obj_name", "attr_name", 0)` pre-compile.
- **Implementation**: `koskari/lib/opcode/SWITCH_EQ_VAR_ATTR.c`

### KOS_OP_SWITCH_EQ_FAST_ATTR

- **Style**: EXTRA. Schema `(LABEL, FASTVAR, ATTR, NOARG)`.
- **Stack**: `[..., s]`. Resolve `obj = KosNamespace_FastDeref(ns, a1)`,
  `rhs = getattr(obj, attr_name)` from `a2`, then compare `s == rhs`.
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
- **Fusion.** The bytecode processor fuses a `MATCH_SCOPE` followed by a
  `MATCH_NEXT` on the cursor the mark was made at into
  `MATCH_SCOPE_NEXT`; both are also emitted apart.

Each `koskari/lib/opcode/MATCH_*.c` handler holds its instruction's
body, except where handlers share one: the VAR and FAST twins, and
MATCH_CAPTURES with MATCH_PASS, tail-call a body in
`koskari/lib/eval/eval_match.c`. A body calls the matcher subsystem's
step (`koskari/lib/matcher/cursor.c`, `mark.c`, `step.c`;
`matcher.h`). The
lowering is the `Emitter` in `koskari/matcher/emitter.py`, entered by
`lower_arm` inline for a `compile/match` arm and `lower_matcher` into a
function body, which `koskari/matcher/build.py` completes as a syntax
rule's lambda or, through `build_matcher`, as a standalone `Function`.
Design:
[archive/matcher-core.md](archive/matcher-core.md).

### KOS_OP_MATCH_DESCEND {#kos_op_match_descend}

- **Style**: LABEL
- **Stack**: `[..., v]` -> `[..., cur]`; on a miss `[..., v]` -> `[...]` and a
  jump to the label.
- **Semantics**: The sequence gate on `v` read through `Datum`. A pair spine
  (`nil` included), a tuple, a list or another sequence type descends, and a
  cursor over it replaces `v`; `str`, `bytes`, `bytearray`, a mapping, a set,
  an iterator and any other value miss. A pair spine is counted once, here: an
  improper spine is a miss and a spine that loops raises `ValueError`. Later
  instructions read a list at its live length and any other sequence at the
  length it had here.
- **Implementation**: `koskari/lib/opcode/MATCH_DESCEND.c`

### KOS_OP_MATCH_SINGLE {#kos_op_match_single}

- **Style**: NOARG
- **Stack**: `[..., v]` -> `[..., cur]`; cannot miss.
- **Semantics**: A cursor over a one-element subject whose element is `v`
  itself, not read through `Datum`, replaces `v`.
- **Implementation**: `koskari/lib/opcode/MATCH_SINGLE.c`

### KOS_OP_MATCH_FITS {#kos_op_match_fits}

- **Style**: EXTRA (LABEL, RAW, RAW, RAW): label, cursor depth, minimum,
  maximum.
- **Stack**: No stack effect; jump to `a0` on a miss.
- **Semantics**: Miss unless the cursor at depth `a1` has at least `a2` and at
  most `a3` elements left before its end; a maximum of `0xFFFF` is unbounded.
  With `a2` and `a3` both 0 it is the end test.
- **Implementation**: `koskari/lib/opcode/MATCH_FITS.c`

### KOS_OP_MATCH_NEXT {#kos_op_match_next}

- **Style**: EXTRA (LABEL, RAW): label, cursor depth.
- **Stack**: `[...]` -> `[..., e]`; jump to `a0` with nothing pushed on a
  miss.
- **Semantics**: Pull the next element from the cursor at depth `a1` and
  advance the cursor. From a pair spine `e` is a `Datum` over the cell's car,
  read through `Datum`, carrying the cell's position; from a tuple, list or
  other sequence it is the item; from a single-value cursor, the value. A miss
  at the cursor's end, or when the spine cell at the cursor is no longer a
  pair.
- **Implementation**: `koskari/lib/opcode/MATCH_NEXT.c`

### KOS_OP_MATCH_BOUND {#kos_op_match_bound}

- **Style**: EXTRA (LABEL, RAW, RAW): label, cursor depth, width.
- **Stack**: `[...]` -> `[..., bound]`; jump to `a0` with nothing pushed on a
  miss.
- **Semantics**: Push a cursor at the position of the cursor at depth `a1`
  whose end is `a2` elements before that cursor's end. A miss when fewer than
  `a2` elements remain.
- **Implementation**: `koskari/lib/opcode/MATCH_BOUND.c`

### KOS_OP_MATCH_UNBOUND {#kos_op_match_unbound}

- **Style**: EXTRA (RAW): cursor depth.
- **Stack**: `[...]` -> `[..., c]`; cannot miss.
- **Semantics**: Push a cursor at the position of the cursor at depth `a0`
  whose end is its subject's end: the elements a `MATCH_BOUND` left before
  that cursor's end, at any depth of nesting, are left on the new cursor. The
  cursor at `a0` does not move.
- **Implementation**: `koskari/lib/opcode/MATCH_UNBOUND.c`

### KOS_OP_MATCH_SYNC {#kos_op_match_sync}

- **Style**: EXTRA (RAW, RAW): cursor depth, cursor depth.
- **Stack**: No stack effect; cannot miss.
- **Semantics**: Move the cursor at depth `a0` to the position of the cursor
  at depth `a1`, a cursor over the same subject.
- **Implementation**: `koskari/lib/opcode/MATCH_SYNC.c`

### KOS_OP_MATCH_TAIL {#kos_op_match_tail}

- **Style**: EXTRA (LABEL, RAW, RAW, RAW): label, cursor depth, suffix width,
  minimum.
- **Stack**: `[...]` -> `[..., span]`; jump to `a0` with nothing pushed on a
  miss.
- **Semantics**: Take the elements left at the cursor at depth `a1` but the
  last `a2`, push them as a span, and advance the cursor past them. A miss
  when fewer than `a2` elements remain or the span holds fewer than `a3`. Over
  a tuple, list or other sequence the span is a tuple of the items; over a
  single value, a one-element tuple of the value or the empty tuple. Over a
  pair spine it is a `Datum` over the rest of the spine when the span runs to
  the spine's end, a `Datum` over the spine's terminator when the span is
  empty, and otherwise a tuple of a `Datum` per cell; a spine cut inside the
  span raises `ValueError`.
- **Implementation**: `koskari/lib/opcode/MATCH_TAIL.c`

### KOS_OP_MATCH_SCOPE {#kos_op_match_scope}

- **Style**: EXTRA (RAW, RAW): cursor depth, slot count.
- **Stack**: `[...]` -> `[..., m]`; cannot miss.
- **Semantics**: Push a mark at the position of the cursor at depth `a0` with
  `a1` empty slots. A mark's position never moves.
- **Implementation**: `koskari/lib/opcode/MATCH_SCOPE.c`

### KOS_OP_MATCH_SCOPE_NEXT {#kos_op_match_scope_next}

- **Style**: EXTRA (LABEL, RAW, RAW): label, cursor depth, slot count.
- **Stack**: `[...]` -> `[..., m, e]`; jump to `a0` with `m` pushed on a
  miss.
- **Semantics**: `MATCH_SCOPE a1 a2` then `MATCH_NEXT a0` on the same cursor,
  one entry deeper under the mark. Fused by the bytecode processor; never
  emitted directly.
- **Implementation**: `koskari/lib/opcode/MATCH_SCOPE_NEXT.c`

### KOS_OP_MATCH_RESET {#kos_op_match_reset}

- **Style**: EXTRA (RAW, RAW): cursor depth, mark depth.
- **Stack**: No stack effect; cannot miss.
- **Semantics**: Move the cursor at depth `a0` back to the position of the
  mark at depth `a1`.
- **Implementation**: `koskari/lib/opcode/MATCH_RESET.c`

### KOS_OP_MATCH_AT {#kos_op_match_at}

- **Style**: EXTRA (RAW): mark depth.
- **Stack**: `[...]` -> `[..., e]`; cannot miss.
- **Semantics**: Push the element at the position of the mark at depth `a0`,
  as `MATCH_NEXT` gave it. Raises `ValueError` when the subject no longer
  holds that element: a list shrunk below the position, or a spine cell that
  is no longer a pair.
- **Implementation**: `koskari/lib/opcode/MATCH_AT.c`

### KOS_OP_MATCH_SPAN {#kos_op_match_span}

- **Style**: EXTRA (LABEL, RAW, RAW): label, mark depth, cursor depth.
- **Stack**: `[...]` -> `[..., span]`; jump to `a0` with nothing pushed when
  the span is empty.
- **Semantics**: Push the elements from the position of the mark at depth `a1`
  to the cursor at depth `a2`: one element as `MATCH_AT` gives it, two or more
  as a tuple of them, a `Datum` per cell over a pair spine. Raises
  `ValueError` when the subject no longer holds the span.
- **Implementation**: `koskari/lib/opcode/MATCH_SPAN.c`

### KOS_OP_MATCH_STORE {#kos_op_match_store}

- **Style**: EXTRA (RAW, RAW, RAW, CONST): mark depth, slot index, cursor
  depth, names tuple.
- **Stack**: `[..., v]` -> `[...]`; cannot miss.
- **Semantics**: Pop `v` into slot `a1` of the mark at depth `a0`, replacing
  what the slot held. When the cursor at depth `a2` walks a pair spine, a
  `Datum` `v`, and each `Datum` in a tuple `v`, takes the name `a3[a1]` as its
  binding; a `Datum` from a tuple, list or single value is stored unchanged. A
  cursor depth `a2` of 0 names no cursor: `v` was tested where it lay, and is
  stored unchanged.
- **Invariants**: `a3` is a tuple with an entry at `a1`; Code construction
  raises `ValueError` otherwise. An index outside the mark's slots raises
  `SystemError`.
- **Implementation**: `koskari/lib/opcode/MATCH_STORE.c`

### KOS_OP_MATCH_CLEAR {#kos_op_match_clear}

- **Style**: EXTRA (RAW, CONST): mark depth, slot indices.
- **Stack**: No stack effect; cannot miss.
- **Semantics**: Empty the slots of the mark at depth `a0` that the tuple `a1`
  lists.
- **Invariants**: `a1` is a tuple; Code construction raises `ValueError`
  otherwise. An index outside the mark's slots raises `SystemError`.
- **Implementation**: `koskari/lib/opcode/MATCH_CLEAR.c`

### KOS_OP_MATCH_CAPTURES {#kos_op_match_captures}

- **Style**: EXTRA (RAW, RAW, CONST): mark depth, cursor depth, names tuple.
- **Stack**: `[...]` -> `[..., caps]`; cannot miss.
- **Semantics**: Push the captures of the mark at depth `a0`: each filled slot
  under its name in `a2`, an empty slot absent. Over a tuple, list, other
  sequence or single value `caps` is a dict. Over a pair spine it is a
  `DatumFrame` whose position is the first cell's and whose source is the span
  from the mark's position to the cursor at depth `a1`: the first cell itself
  when the span is empty or runs to the spine's end, the car read through
  `Datum` for one cell, otherwise a tuple of the cars. Raises `ValueError`
  when the spine no longer reaches the cursor's cell from the mark's.
- **Invariants**: `a2` is a tuple; Code construction raises `ValueError`
  otherwise. A names tuple without one name per slot of the mark raises
  `SystemError`.
- **Implementation**: `koskari/lib/opcode/MATCH_CAPTURES.c`

### KOS_OP_MATCH_PASS {#kos_op_match_pass}

- **Style**: EXTRA (RAW, RAW, CONST, RAW): mark depth, cursor depth, names
  tuple, group flag.
- **Stack**: `[...]` -> `[..., value]`; cannot miss.
- **Semantics**: Push one repeat pass's value. When any slot of the mark at
  depth `a0` is filled, the filled slots keyed by their names in `a2`: a dict
  when `a3` is 0, the captures as `MATCH_CAPTURES` gives them when `a3` is
  set. When no slot is filled, the span from the mark's position to the cursor
  at depth `a1`, as `MATCH_SPAN` gives it.
- **Invariants**: `a2` is a tuple; Code construction raises `ValueError`
  otherwise.
- **Implementation**: `koskari/lib/opcode/MATCH_PASS.c`

### KOS_OP_MATCH_LOAD {#kos_op_match_load}

- **Style**: EXTRA (LABEL, RAW, RAW): label, mark depth, slot index.
- **Stack**: `[...]` -> `[..., v]`; jump to `a0` with nothing pushed when the
  slot is empty.
- **Semantics**: Push the value in slot `a2` of the mark at depth `a1`. An
  index outside the mark's slots raises `SystemError`.
- **Implementation**: `koskari/lib/opcode/MATCH_LOAD.c`

### KOS_OP_MATCH_APPEND {#kos_op_match_append}

- **Style**: EXTRA (RAW): list depth.
- **Stack**: `[..., v]` -> `[...]`; cannot miss.
- **Semantics**: Pop `v` and append it to the list at depth `a0`. Raises
  `TypeError` when that entry is not a list. Every REPEAT pass that goes on
  ends here, ahead of its back-edge, so the handler ends in
  **`RESULT_NEXT_POLL`**: the REPEAT loop's cooperation point.
- **Implementation**: `koskari/lib/opcode/MATCH_APPEND.c`

### KOS_OP_MATCH_MIN {#kos_op_match_min}

- **Style**: EXTRA (LABEL, RAW, RAW): label, list depth, count.
- **Stack**: No stack effect; jump to `a0` on a miss.
- **Semantics**: Miss unless the list at depth `a1` holds at least `a2` items.
  Raises `TypeError` when that entry is not a list.
- **Implementation**: `koskari/lib/opcode/MATCH_MIN.c`

### KOS_OP_MATCH_COLUMNS {#kos_op_match_columns}

- **Style**: EXTRA (RAW, RAW, CONST, CONST): list depth, mark depth, names
  tuple, slot indices.
- **Stack**: No stack effect; cannot miss.
- **Semantics**: For each position `j`, store into slot `a3[j]` of the mark at
  depth `a1` the list of the values the name `a2[j]` takes in the passes of
  the list at depth `a0`, in pass order. A pass that did not bind the name
  adds nothing; when no pass binds it the list is empty. The list at `a0` is
  read, not changed.
- **Invariants**: `a2` and `a3` are tuples; Code construction raises
  `ValueError` otherwise. Tuples of different lengths, or an index outside the
  mark's slots, raise `SystemError`.
- **Implementation**: `koskari/lib/opcode/MATCH_COLUMNS.c`

### KOS_OP_MATCH_LITERAL {#kos_op_match_literal}

- **Style**: EXTRA (LABEL, CONST): label, constant.
- **Stack**: `[..., e]` -> `[..., e]`; jump to `a0` on a miss, `e` left either
  way.
- **Semantics**: `e` read through `Datum` must be the constant `a1`, a gensym
  whose base is `a1`, or equal to `a1`.
- **Implementation**: `koskari/lib/opcode/MATCH_LITERAL.c`

### KOS_OP_MATCH_CORE {#kos_op_match_core}

- **Style**: EXTRA (LABEL, RAW): label, core type ordinal.
- **Stack**: `[..., e]` -> `[..., e]`; jump to `a0` on a miss, `e` left either
  way.
- **Semantics**: `e` read through `Datum` must be an instance of the core type
  whose ordinal is `a1`: 0 `pair`, 1 `symbol`, 2 `keyword`, 3 `int`, 4 `str`,
  5 `tuple`, 6 `list`. An ordinal past those raises `SystemError`.
- **Implementation**: `koskari/lib/opcode/MATCH_CORE.c`

### KOS_OP_MATCH_TYPE_VAR / KOS_OP_MATCH_TYPE_FAST {#kos_op_match_type_var}

- **Style**: EXTRA (LABEL, VAR) on `MATCH_TYPE_VAR`, (LABEL, FASTVAR) on
  `MATCH_TYPE_FAST`: label, type.
- **Stack**: `[..., e]` -> `[..., e]`; jump to `a0` on a miss, `e` left either
  way.
- **Semantics**: `e` read through `Datum` must be an instance of the type in
  the variable `a1`, read by name on the `_VAR` form and by fast index on the
  `_FAST` form.
- **Implementation**: `koskari/lib/opcode/MATCH_TYPE_VAR.c`,
  `koskari/lib/opcode/MATCH_TYPE_FAST.c`

### KOS_OP_MATCH_CLASS_VAR / KOS_OP_MATCH_CLASS_FAST {#kos_op_match_class_var}

- **Style**: EXTRA (LABEL, VAR, RAW) on `MATCH_CLASS_VAR`, (LABEL, FASTVAR,
  RAW) on `MATCH_CLASS_FAST`: label, class, positional field count.
- **Stack**: `[..., e]` -> `[..., e]`; jump to `a0` on a miss, `e` left either
  way.
- **Semantics**: `e` read through `Datum` must be an instance of the class in
  the variable `a1`, read by name on the `_VAR` form and by fast index on the
  `_FAST` form; a non-instance is a miss. The class's `__match_args__` must
  then hold at least `a2` names: a class with no `__match_args__`, a
  `__match_args__` that is not a tuple, or one shorter than `a2` raises
  `TypeError` before any attribute of `e` is read. On Python 3.9 a dataclass
  or named tuple without `__match_args__` reads the equivalent tuple Python
  3.10 would generate.
- **Implementation**: `koskari/lib/opcode/MATCH_CLASS_VAR.c`,
  `koskari/lib/opcode/MATCH_CLASS_FAST.c`

### KOS_OP_MATCH_SAME {#kos_op_match_same}

- **Style**: EXTRA (LABEL, RAW, RAW): label, mark depth, slot index.
- **Stack**: `[..., e]` -> `[..., e]`; jump to `a0` on a miss, `e` left either
  way.
- **Semantics**: `e` must equal the value in slot `a2` of the mark at depth
  `a1`, both read through `Datum`: the same object, or equal under `==`. An
  empty slot is a miss, and so is a `DatumFrame` on either side.
- **Implementation**: `koskari/lib/opcode/MATCH_SAME.c`

### KOS_OP_MATCH_FIELD_POS_VAR / KOS_OP_MATCH_FIELD_POS_FAST {#kos_op_match_field_pos_var}

- **Style**: EXTRA (LABEL, VAR, RAW) on `MATCH_FIELD_POS_VAR`, (LABEL,
  FASTVAR, RAW) on `MATCH_FIELD_POS_FAST`: label, class, position.
- **Stack**: `[..., o]` -> `[..., o, a]`; jump to `a0` with nothing pushed on
  a miss.
- **Semantics**: Push the attribute of `o`, read through `Datum`, that
  `__match_args__[a2]` of the class in the variable `a1` names; the variable
  is read by name on the `_VAR` form and by fast index on the `_FAST` form. A
  missing attribute is a miss. A class with no `__match_args__`, a
  `__match_args__` that is not a tuple, a position past its end, or an entry
  that is not a `str` raises `TypeError`. On Python 3.9 a dataclass or named
  tuple without `__match_args__` reads the equivalent tuple Python 3.10 would
  generate.
- **Implementation**: `koskari/lib/opcode/MATCH_FIELD_POS_VAR.c`,
  `koskari/lib/opcode/MATCH_FIELD_POS_FAST.c`

### KOS_OP_MATCH_FIELD_KW {#kos_op_match_field_kw}

- **Style**: EXTRA (LABEL, CONST): label, attribute name.
- **Stack**: `[..., o]` -> `[..., o, a]`; jump to `a0` with nothing pushed on
  a miss.
- **Semantics**: Push the attribute `a1` of `o` read through `Datum`. An
  `AttributeError` is a miss; any other exception propagates.
- **Implementation**: `koskari/lib/opcode/MATCH_FIELD_KW.c`

### KOS_OP_MATCH_MAPPING {#kos_op_match_mapping}

- **Style**: LABEL
- **Stack**: `[..., o]` -> `[..., o]`; jump to the label on a miss.
- **Semantics**: The mapping gate on `o` read through `Datum`: a `dict` or a
  type flagged as a mapping passes (on Python 3.9, an instance of
  `collections.abc.Mapping`); anything else misses.
- **Implementation**: `koskari/lib/opcode/MATCH_MAPPING.c`

### KOS_OP_MATCH_KEY {#kos_op_match_key}

- **Style**: EXTRA (LABEL, CONST): label, key.
- **Stack**: `[..., o]` -> `[..., o, v]`; jump to `a0` with nothing pushed on
  a miss.
- **Semantics**: Push the value under the constant key `a1` in the mapping `o`
  read through `Datum`. An absent key is a miss. The mapping is probed without
  inserting: a `dict` is read directly and any other mapping through its
  `get`, so a `defaultdict` neither inserts nor invents the key.
- **Implementation**: `koskari/lib/opcode/MATCH_KEY.c`

### KOS_OP_MATCH_KEY_VAR / KOS_OP_MATCH_KEY_FAST {#kos_op_match_key_var}

- **Style**: EXTRA (LABEL, VAR) on `MATCH_KEY_VAR`, (LABEL, FASTVAR) on
  `MATCH_KEY_FAST`: label, key.
- **Stack**: `[..., o]` -> `[..., o, v]`; jump to `a0` with nothing pushed on
  a miss.
- **Semantics**: Push the value under the key in the variable `a1`, read by
  name on the `_VAR` form and by fast index on the `_FAST` form, in the
  mapping `o` read through `Datum`. An absent key is a miss. The mapping is
  probed as `MATCH_KEY` probes it.
- **Implementation**: `koskari/lib/opcode/MATCH_KEY_VAR.c`,
  `koskari/lib/opcode/MATCH_KEY_FAST.c`


## 10. Introspection

### KOS_OP_GLOBALS

- **Style**: NOARG.
- **Stack**: `[...] → [..., globals_ns]`. Pushes the outermost parent of the frame's namespace (a KosNamespace, not a dict), found by walking `KosNamespace_PARENT`.
- **Invariants**: Frame has globals namespace.

**Example:** Push the frame's module namespace.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | GLOBALS| 0   | 0        | stack: [..., globals_ns] |
| 1     | RETURN | 1   | 1        | |

---

### KOS_OP_LOCALS

- **Style**: NOARG.
- **Stack**: `[...] → [..., locals_ns]`. Pushes the frame's current namespace (a KosNamespace, not a dict), revealed to the cyclic collector because it is now storable.
- **Invariants**: Frame has locals namespace.

**Example:** Push the frame's current namespace.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | LOCALS | 0   | 0        | stack: [..., locals_ns] |
| 1     | RETURN | 1   | 1        | |

---

### 10.1 Module import

The four import opcodes (IMPORT, IMPORT_FROM, IMPORT_STAR, IMPORT_USE) each
implement one Koskari import surface form.
They are **self-contained and stack-neutral**: each carries the module name
and relative-import level in its EXTRA row, imports its own module through
the shared `kos_import_module` helper (`koskari/lib/eval.c`), and stores its
own bindings into the current frame namespace. None of them touch the
operand stack, chain a module between instructions, or need a compensating
POP. See `koskari/compile/specials/imports.py` (`special_import`,
`special_import_from`, `special_import_all_from`, `special_use`) for the
compiler forms.

`kos_import_module(ctx, name, level, leaf)` builds, for a relative import
(`level > 0`), a temporary globals dict holding `__name__` / `__package__`
from the frame's globals namespace (Python requires a dict, not a
KosNamespace) so `PyImport_ImportModuleLevelObject` can resolve the anchor
package. The `leaf` flag selects the fromlist: the module-level sentinel
`_import_fromlist = ("*",)` returns the **leaf** submodule (matching
`importlib.import_module`), while a NULL fromlist returns the **root**
package while still importing the full dotted chain and attaching submodules
as attributes (matching a plain Python `import foo.bar`). `_import_fromlist`
is initialised once in `koskari_eval_init` and cleared in
`koskari_eval_clear`.

### KOS_OP_IMPORT {#kos_op_import}

- **Style**: EXTRA (`CONST`, `COUNT`, `COUNT`, `CONST`). Row: `a0` = cpool
  module name str (or None for a bare relative import), `a1` = level
  (leading dots; 0 = absolute), `a2` = leaf flag (0 = bind root, 1 = bind
  leaf), `a3` = cpool binding name str.
- **Stack**: no effect.
- **Semantics**: Import the module via `kos_import_module` (root or leaf per
  the flag) and store it into the current frame namespace under the binding
  name. With the leaf flag clear, the root package is bound while the full
  dotted chain is still imported (plain `(import foo.bar)` → binds `foo`);
  with it set, the deepest submodule is bound (the
  `(import [foo.bar as: fbar])` member form). Relative imports are always
  leaf-style.
- **Invariants**: `a0` is a str or None; `a3` is a str.
- **Implementation**: `koskari/lib/opcode/IMPORT.c`

**Example:** `(import os.path)` — import the `os.path` chain, bind the root
`os`. Extra row: `("os.path", 0, 0, "os")`.

| index | opcode | arg | arg view                 | comment |
|------:|--------|-----|--------------------------|---------|
| 0     | IMPORT | 0   | 0 ("os.path", 0, 0, "os")| bind "os"; stack: [...] |
| 1     | CONST  | 1   | 1 (None)                 | statement value; stack: [..., None] |
| 2     | RETURN | 1   | 1                        | |

Member form `(import [os.path as: p])` uses row `("os.path", 0, 1, "p")` to
bind the leaf; relative `(import .foo)` uses `("foo", 1, 1, "foo")`.

---

### KOS_OP_IMPORT_FROM {#kos_op_import_from}

- **Style**: EXTRA (`CONST`, `COUNT`, `CONST`, `CONST`). Row: `a0` = cpool
  module name str (or None), `a1` = level, `a2` = cpool tuple of attribute
  names, `a3` = cpool tuple of binding names (parallel to the attribute
  names; the same tuple object when there are no renames).
- **Stack**: no effect.
- **Semantics**: Import the leaf module via `kos_import_module`, then for
  each attribute name `getattr` it and store the value into the current
  frame namespace under the corresponding binding name.
- **Submodule fallback**: when the `getattr` raises `AttributeError`, import
  `"{module.__name__}.{attr}"` and retry — matching CPython's
  `from pkg import sub` behavior for submodules the package `__init__` does
  not import. Other exceptions propagate.
- **Invariants**: `a2` and `a3` are equal-length tuples of str.
- **Implementation**: `koskari/lib/opcode/IMPORT_FROM.c`

**Example:** `(import-from os path [sep as: s])` — import `os`, bind `path`
and bind `sep` as `s`. Extra row:
`("os", 0, ("path", "sep"), ("path", "s"))`.

| index | opcode      | arg | arg view                     | comment |
|------:|-------------|-----|------------------------------|---------|
| 0     | IMPORT_FROM | 0   | 0 ("os", 0, ("path","sep"), …)| bind "path", "s"; stack: [...] |
| 1     | CONST       | 1   | 1 (None)                     | statement value |
| 2     | RETURN      | 1   | 1                            | |

---

### KOS_OP_IMPORT_STAR {#kos_op_import_star}

- **Style**: EXTRA (`CONST`, `COUNT`, `NOARG`, `NOARG`). Row: `a0` = cpool
  module name str (or None), `a1` = level.
- **Stack**: no effect.
- **Semantics**: Import the leaf module via `kos_import_module` and bind its
  public names into the current frame namespace, matching Python's
  `from MODULE import *`. Name selection follows CPython's `import_all_from`:
  when the module defines `__all__` (a sequence of str; a non-str entry is a
  `TypeError`) exactly those names are bound; otherwise every name from
  `dir(module)` that does not start with an underscore. Existing bindings are
  overwritten.
- **Compiler restriction**: the `import-all-from` special emits this opcode
  only at module top level (guarded by `Compiler.scope_depth`); it is a
  `SpecialError` inside a function, lambda, `let`, or class body.
- **Invariants**: `a0` is a str or None.
- **Implementation**: `koskari/lib/opcode/IMPORT_STAR.c`

**Example:** `(import-all-from os.path)` — bind `os.path`'s public names.
Extra row: `("os.path", 0, 0, 0)`.

| index | opcode      | arg | arg view              | comment |
|------:|-------------|-----|-----------------------|---------|
| 0     | IMPORT_STAR | 0   | 0 ("os.path", 0, …)   | bind public names; stack: [...] |
| 1     | CONST       | 1   | 1 (None)              | statement value |
| 2     | RETURN      | 1   | 1                     | |

### KOS_OP_IMPORT_USE {#kos_op_import_use}

- **Style**: EXTRA (`CONST`, `COUNT`, `CONST`, `CONST`). Row: `a0` = cpool
  module name str (or None), `a1` = level, `a2` = cpool tuple of feature
  stems, `a3` = cpool tuple of bind stems (parallel; the same tuple object
  when there are no renames).
- **Stack**: no effect.
- **Semantics**: Import the leaf module via `kos_import_module`, then for
  each feature stem probe its candidate set — the stem itself,
  `compile/{stem}`, `layout/{stem}` — and store each hit under the parallel
  bind stem with the same facet prefix applied, so a rename substitutes the
  whole family. A stem already carrying a facet prefix is literal: one
  candidate, bound as written.
- **Miss handling**: a per-facet miss is tolerated. The bare candidate uses
  `kos_import_from_member` (submodule fallback included); its crafted
  missing-member `ImportError` counts as a miss, while a
  `ModuleNotFoundError` from inside a submodule's own import propagates. A
  stem with zero hits raises `ImportError` naming the spellings tried.
- **Compiler restriction**: the `use` special emits this opcode only at
  module top level (guarded by `Compiler.scope_depth`).
- **Invariants**: `a2` and `a3` are equal-length tuples of str.
- **Implementation**: `koskari/lib/opcode/IMPORT_USE.c`

**Example:** `(use tacotruck tacos)` where the module defines `tacos` and
`compile/tacos` — bind both. Extra row: `("tacotruck", 0, ("tacos",),
("tacos",))`.

| index | opcode     | arg | arg view                          | comment |
|------:|------------|-----|-----------------------------------|---------|
| 0     | IMPORT_USE | 0   | 0 ("tacotruck", 0, ("tacos",), …) | bind "tacos", "compile/tacos"; stack: [...] |
| 1     | CONST      | 1   | 1 (None)                          | statement value |
| 2     | RETURN     | 1   | 1                                 | |

---

**Trace snapshots:** There is **no** `KOS_OP_CAPTURE_FRAME` (or similar) in `koskari/lib/opcodes.incl`; a future opcode would be added there first, then documented here. The read-only views that exist are `Context.snapshot()` and the `__koskari_dynchain__` frame list on raised exceptions. The TraceView proposal is archived at [archive/introspection-trace.md](archive/introspection-trace.md).


## 11. Exception handling

### KOS_OP_EXCEPTION

- **Style**: NOARG.
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
- **Stack**: No change. Clear current exception state.
- **Invariants**: —

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
- **Stack**: `[...] → [..., value]`. Load value bound to name in frame namespace.
- **Invariants**: Name in constant pool.
- **Compiler form:** Pre-compile arg = name (string/symbol); finalize stores name in pool and rewrites arg to pool index. May be lowered to FAST with slot index for locals.

**Example:** Load variable "x". Pool: 0 = "x".

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | VAR    | 0   | 0 ('x')  | stack: [..., value] |
| 1     | RETURN | 1   | 1        | |

---

### KOS_OP_VAR2 {#kos_op_var2}

- **Style**: EXTRA (VAR, VAR, NOARG, NOARG). Row = two constant pool name
  indices (post-compile).
- **Stack**: `[...] → [..., a, b]`. Load both names from the frame namespace
  and push in row order. Both names resolve before either push, so a missing
  name errors with nothing pushed.
- **Invariants**: Names in constant pool.
- **Compiler form:** Never emitted by specials. Produced only by
  **`ByteCodeProcessor.pack_runs`** (the trailing pack phase), which combines
  a run of two consecutive bare **VAR** loads (quads first, then one triple
  or one pair). **`convert_fast`** lowers to **FAST2** when both names have
  fast slots (all-or-nothing; a mixed-eligibility row stays VAR2).
- **Implementation**: `koskari/lib/opcode/VAR2.c`
- **Covered by**: `tests/compile/test_pack_var_runs.py`,
  `tests/reproducers/test_var_pack.py`

**Example:** Load "x" then "y". Pool: 0 = "x", 1 = "y".

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | VAR2   | 0   | (0 ('x'), 1 ('y'), 0, 0) | stack: [..., x, y] |
| 1     | RETURN | 1   | 1        | |

---

### KOS_OP_VAR3 {#kos_op_var3}

- **Style**: EXTRA (VAR, VAR, VAR, NOARG). Row = three constant pool name indices
  (post-compile).
- **Stack**: `[...] → [..., a, b, c]`. Load the three names from the frame namespace and
  push in row order. Every name resolves before any push, so a missing name
  errors with nothing pushed.
- **Invariants**: Names in constant pool.
- **Compiler form:** As VAR2: produced only by **`ByteCodeProcessor.pack_runs`**
  from a run of three consecutive bare VAR loads (quads first, then one triple
  or one pair). **`convert_fast`** lowers to FAST3 when all three names have fast
  slots (all-or-nothing; a mixed-eligibility row stays VAR3).
- **Implementation**: `koskari/lib/opcode/VAR3.c`
- **Covered by**: `tests/compile/test_pack_var_runs.py`,
  `tests/reproducers/test_var_pack.py`

**Example:** Load "a", "b", "c". Pool: 0-2 = names.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | VAR3   | 0   | (0 ('a'), 1 ('b'), 2 ('c'), 0) | stack: [..., a, b, c] |
| 1     | RETURN | 1   | 1        | |

---

### KOS_OP_VAR4 {#kos_op_var4}

- **Style**: EXTRA (VAR, VAR, VAR, VAR). Row = four constant pool name
  indices (post-compile).
- **Stack**: `[...] → [..., a, b, c, d]`. Load all four names from the frame
  namespace and push in row order. All names resolve before any push, so a
  missing name errors with nothing pushed.
- **Invariants**: Names in constant pool.
- **Compiler form:** As **VAR2**: produced only by **`pack_runs`** from a run
  of four (or more, greedily) consecutive bare **VAR** loads.
  **`convert_fast`** lowers to **FAST4** when all four names have fast slots.
- **Implementation**: `koskari/lib/opcode/VAR4.c`
- **Covered by**: `tests/compile/test_pack_var_runs.py`,
  `tests/reproducers/test_var_pack.py`

**Example:** Load "a", "b", "c", "d". Pool: 0-3 = names.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | VAR4   | 0   | (0 ('a'), 1 ('b'), 2 ('c'), 3 ('d')) | stack: [..., a, b, c, d] |
| 1     | RETURN | 1   | 1        | |

---

### KOS_OP_VAR_BIND

- **Style**: VAR. Arg = constant pool index of name (post-compile).
- **Stack**: `[..., ref] → [...]`. **BIND**: use this reference to resolve the
  given name in the frame namespace. See §1.9.
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

### KOS_OP_VAR_CLEAR2 {#kos_op_var_clear2}

- **Style**: EXTRA (VAR, VAR, NOARG, NOARG). Row = 2 constant pool name indices (post-compile).
- **Stack**: No stack effect. Clear the 2 names in row order, as
  2 `VAR_CLEAR` would; an already empty one stays empty.
- **Compiler form:** Never emitted by specials. Produced only by
  `ByteCodeProcessor.pack_runs`, which combines a run of consecutive
  `VAR_CLEAR` rows (quads first, then one triple or one pair); `convert_fast`
  lowers it to `FAST_CLEAR2` when every name has a fast slot.

### KOS_OP_VAR_CLEAR3 {#kos_op_var_clear3}

- **Style**: EXTRA (VAR, VAR, VAR, NOARG). Row = 3 constant pool name indices (post-compile).
- **Stack**: No stack effect. Clear the 3 names in row order, as
  3 `VAR_CLEAR` would; an already empty one stays empty.
- **Compiler form:** Never emitted by specials. Produced only by
  `ByteCodeProcessor.pack_runs`, which combines a run of consecutive
  `VAR_CLEAR` rows (quads first, then one triple or one pair); `convert_fast`
  lowers it to `FAST_CLEAR3` when every name has a fast slot.

### KOS_OP_VAR_CLEAR4 {#kos_op_var_clear4}

- **Style**: EXTRA (VAR, VAR, VAR, VAR). Row = 4 constant pool name indices (post-compile).
- **Stack**: No stack effect. Clear the 4 names in row order, as
  4 `VAR_CLEAR` would; an already empty one stays empty.
- **Compiler form:** Never emitted by specials. Produced only by
  `ByteCodeProcessor.pack_runs`, which combines a run of consecutive
  `VAR_CLEAR` rows (quads first, then one triple or one pair); `convert_fast`
  lowers it to `FAST_CLEAR4` when every name has a fast slot.

### KOS_OP_VAR_TAKE {#kos_op_var_take}

- **Style**: VAR. Arg = constant pool index of name (post-compile).
- **Stack**: `[...] → [..., value]`. Deref name via `frame_ns_take`, clear the
  binding, push the value. **Errors with NameError if the binding is empty**
  (contrast **VAR_CLEAR**, which is idempotent on an already-empty declared
  slot).
- **Invariants**: Name in constant pool; binding must hold a value.
- **Compiler form:** Pre-compile arg = name; post-compile arg = pool index.
  Emitted by **`compile/takeq`**. May lower to **FAST_TAKE** via
  **`convert_fast`**.
- **Implementation**: `koskari/lib/opcode/VAR_TAKE.c`
- **Covered by**: `tests/lib/opcode/test_take_opcodes.py`,
  `tests/compile/specials/test_take_specials.py`

**Example:** Take and clear name "tmp". Pool: 0 = "tmp".

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 2     | VAR_TAKE | 0   | 0 ('tmp')| take "tmp"; stack: [..., value] |

---

### KOS_OP_VAR_REF

- **Style**: VAR. Arg = constant pool index of name (post-compile).
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
- **Stack**: `[..., value] → [...]`. **STORE**: store this value in the given
  name (through the reference that already resolves it). See §1.9.
- **Invariants**: Name must already be bound.
- **Compiler form:** Pre-compile arg = name; post-compile arg = pool index.

**Example:** Store value 10 into name "x". Pool: 0 = "x".

| index | opcode    | arg | arg view | comment |
|------:|-----------|-----|----------|---------|
| 0     | CONST     | 0   | 0 (10)   | stack: [..., 10] |
| 1     | VAR_STORE | 1   | 1 ('x')  | STORE: write 10 into name "x"; stack: [...] |

---

### KOS_OP_VAR_STORE_CONST {#kos_op_var_store_const}

- **Style**: EXTRA (VAR, CONST, NOARG, NOARG). Arg = extra-table index (post-compile).
- **Stack**: `[...] → [...]` (no stack effect). **STORE**: store the constant value into the given name via `frame_ns_store`. Same semantics as **VAR_STORE**, but the value is taken from the constant pool (extra row field `a1`), not from TOS.
- **Peephole**: **`Compiler.compress`** fuses **`KOS_OP_CONST`** `value` immediately followed by **`KOS_OP_VAR_STORE`** `name` into this opcode with pre-compile arg **`(name, value, None, None)`**; the **`CONST`** is removed from the stream. See §1.8.
- **Lowering**: **`Compiler.convert_fast`** (when the name has a fixed fast slot in FUNCTION mode) may replace **`VAR_STORE_CONST`** with **`FAST_STORE_CONST`**, rewriting the VAR field to a slot index and preserving the constant operand.
- **Invariants**: Name must already be bound. Name and value are internalized into the extra row at finalize.
- **Compiler form:** Pre-compile arg = 4-tuple `(name, value, None, None)`; post-compile arg = extra index.
- **Implementation**: `koskari/lib/opcode/VAR_STORE_CONST.c`
- **Covered by**: `tests/compile/test_compress.py`, `tests/compile/test_convert_fast.py`, `tests/lib/opcode/test_store_const.py`

**Example:** Store constant 42 into name `"x"` without pushing 42. Pre-compile stream after compress: one **`VAR_STORE_CONST`**; pool/extra filled at finalize. Post-compile arg view uses extra index **0** (illustrative).

| index | opcode          | arg | arg view              | comment |
|------:|-----------------|-----|-----------------------|---------|
| 0     | VAR_STORE_CONST | 0   | 0 → ('x', 42, …)      | STORE 42 into "x"; stack unchanged |
| 1     | RETURN          | 1   | 1                     | |

**Example (fusion source):** `(compile/setq x 42)` emits value, **`VAR_STORE`**, **`CONST`** `None`; compress turns **`CONST`** `42` + **`VAR_STORE`** `x` into **`VAR_STORE_CONST`**.


### KOS_OP_VAR_INPLACE {#kos_op_var_inplace}

- **Style**: EXTRA (OPERAND, VAR, NOARG, NOARG). Row a0 = the bare operator selector, a1 = the variable name.
- **Stack**: `[..., rhs] → [...]`. Pop the one rhs and apply the operator to the frame namespace binding with it, storing the result back; pushes nothing (the special appends CONST None). The operator switch lives in the shared `kos_var_inplace_operand`.
- **Compiler form:** Emitted by **`compile/inplace`** when the target is a symbol. May lower to **FAST_INPLACE**; a single constant operand squashes into **VAR_INPLACE_CONST**.
- **Implementation**: `koskari/lib/opcode/VAR_INPLACE.c`


### KOS_OP_VAR_INPLACE_CONST {#kos_op_var_inplace_const}

- **Style**: EXTRA (OPERAND, VAR, CONST, NOARG). Row a0 = bare operator selector, a1 = the variable name, a2 = the operand.
- **Stack**: No stack effect. Augmented assignment with a constant operand through the frame namespace: deref the name, apply the operator with the pool constant, store back.
- **Peephole**: the compressed form of **CONST** followed by **VAR_INPLACE** at a count of one, as VAR_STORE_CONST is of CONST and VAR_STORE. May lower to **FAST_INPLACE_CONST**.
- **Implementation**: `koskari/lib/opcode/VAR_INPLACE_CONST.c`


### 12.1 Attribute access on variables (VAR_ATTR_*)

These opcodes perform attribute get, set, clear, take, or reference on an object fetched from the frame namespace by name. The object is not on the stack; it is resolved via `frame_ns_deref(var_name)`. Each uses EXTRA schema: VAR (obj name), ATTR (attr name).

- **Compiler form:** Emitted by attr/del-attr/take-attr/set-attr specials when the object expression is a symbol (variable reference). May be lowered to FAST_ATTR_* when the name has a fixed slot.

### KOS_OP_VAR_ATTR {#kos_op_var_attr}

- **Style**: EXTRA (VAR, CONST, NOARG, NOARG).
- **Stack**: `[...] → [..., value]`. Get attribute on object held in namespace by name. Equivalent to ATTR but the object is loaded from the frame namespace, not from TOS.
- **Implementation**: `koskari/lib/opcode/VAR_ATTR.c`

### KOS_OP_VAR_ATTR_STORE {#kos_op_var_attr_store}

- **Style**: EXTRA (VAR, CONST, NOARG, NOARG).
- **Stack**: `[..., value] → [...]`. Set attribute. Equivalent to ATTR_STORE with object from namespace.
- **Implementation**: `koskari/lib/opcode/VAR_ATTR_STORE.c`

### KOS_OP_VAR_ATTR_CLEAR {#kos_op_var_attr_clear}

- **Style**: EXTRA (VAR, CONST, NOARG, NOARG).
- **Stack**: No stack effect. Delete attribute on object from namespace. Equivalent to ATTR_CLEAR with object from namespace.
- **Implementation**: `koskari/lib/opcode/VAR_ATTR_CLEAR.c`

### KOS_OP_VAR_ATTR_TAKE {#kos_op_var_attr_take}

- **Style**: EXTRA (VAR, CONST, NOARG, NOARG).
- **Stack**: `[...] → [..., value]`. Get attribute on object from namespace,
  delete it, push value. Errors if attribute missing (same as **VAR_ATTR** +
  **VAR_ATTR_CLEAR** sequence).
- **Compiler form:** Emitted by **`compile/take-attr`** when OBJ is a symbol.
  May lower to **FAST_ATTR_TAKE**.
- **Implementation**: `koskari/lib/opcode/VAR_ATTR_TAKE.c`
- **Covered by**: `tests/lib/opcode/test_take_opcodes.py`

### KOS_OP_VAR_ATTR_REF {#kos_op_var_attr_ref}

- **Style**: EXTRA (VAR, CONST, NOARG, NOARG).
- **Stack**: `[...] → [..., attr_ref]`. Push attribute reference. Equivalent to ATTR_REF with object from namespace.
- **Implementation**: `koskari/lib/opcode/VAR_ATTR_REF.c`


### KOS_OP_VAR_ATTR_INPLACE {#kos_op_var_attr_inplace}

- **Style**: EXTRA (OPCOUNT, VAR, ATTR, NOARG). Row a0 = operator selector and count, a1 = the object name, a2 = the attribute name.
- **Stack**: `[..., v0 .. v_{n-1}] → [...]`. Resolve the object by name, reduce obj.attr in place with the count values left to right, store it back; pushes nothing (the special appends CONST None). The operator switch lives in the shared `kos_inplace_reduce`.
- **Compiler form:** Emitted by **`compile/inplace-attr`** when OBJ is a symbol. May lower to **FAST_ATTR_INPLACE**.
- **Implementation**: `koskari/lib/opcode/VAR_ATTR_INPLACE.c`


### KOS_OP_ITEM_STORE_VAR {#kos_op_item_store_var}
- **Style**: VAR (the value's name).
- **Stack**: `[..., obj, key] → [...]`. Set obj[key] = value with the object on the stack, the key on the stack and the value resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR value, ITEM_STORE`. May lower to **ITEM_STORE_FAST**.
- **Implementation**: `koskari/lib/opcode/ITEM_STORE_VAR.c`

### KOS_OP_ITEM_CONST_STORE_VAR {#kos_op_item_const_store_var}
- **Style**: EXTRA (CONST, VAR, NOARG, NOARG).
- **Stack**: `[..., obj] → [...]`. Set obj[key] = value with the object on the stack, the key a pool constant and the value resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `CONST key, VAR value, ITEM_STORE`, or from `VAR value` ahead of the shaped **ITEM_CONST_STORE**. May lower to **ITEM_CONST_STORE_FAST**.
- **Implementation**: `koskari/lib/opcode/ITEM_CONST_STORE_VAR.c`

### KOS_OP_ITEM_VAR {#kos_op_item_var}
- **Style**: VAR (the key's name).
- **Stack**: `[..., obj] → [..., value]`. Get obj[key] with the object on the stack and the key resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR key, ITEM`. May lower to **ITEM_FAST**.
- **Implementation**: `koskari/lib/opcode/ITEM_VAR.c`

### KOS_OP_ITEM_VAR_CLEAR {#kos_op_item_var_clear}
- **Style**: VAR (the key's name).
- **Stack**: `[..., obj] → [...]`. Delete obj[key] with the object on the stack and the key resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR key, ITEM_CLEAR`. May lower to **ITEM_FAST_CLEAR**.
- **Implementation**: `koskari/lib/opcode/ITEM_VAR_CLEAR.c`

### KOS_OP_ITEM_VAR_TAKE {#kos_op_item_var_take}
- **Style**: VAR (the key's name).
- **Stack**: `[..., obj] → [..., value]`. Get obj[key], delete it, and push the value with the object on the stack and the key resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR key, ITEM_TAKE`. May lower to **ITEM_FAST_TAKE**.
- **Implementation**: `koskari/lib/opcode/ITEM_VAR_TAKE.c`

### KOS_OP_ITEM_VAR_REF {#kos_op_item_var_ref}
- **Style**: VAR (the key's name).
- **Stack**: `[..., obj] → [..., value]`. Push an ItemRef over obj[key] with the object on the stack and the key resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR key, ITEM_REF`. May lower to **ITEM_FAST_REF**.
- **Implementation**: `koskari/lib/opcode/ITEM_VAR_REF.c`

### KOS_OP_ITEM_VAR_STORE {#kos_op_item_var_store}
- **Style**: VAR (the key's name).
- **Stack**: `[..., obj, value] → [...]`. Set obj[key] = value with the object on the stack, the key resolved by name and the value on the stack.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Shaped by **`compile/set-item`** when OBJ is a symbol or KEY is a feeder and a value is not, so the fold could not reach the named operands. May lower to **ITEM_FAST_STORE**.
- **Implementation**: `koskari/lib/opcode/ITEM_VAR_STORE.c`

### KOS_OP_ITEM_VAR_STORE_CONST {#kos_op_item_var_store_const}
- **Style**: EXTRA (VAR, CONST, NOARG, NOARG).
- **Stack**: `[..., obj] → [...]`. Set obj[key] = value with the object on the stack, the key resolved by name and the value a pool constant.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR key, CONST value, ITEM_STORE`, or from `CONST value` ahead of the shaped **ITEM_VAR_STORE**. May lower to **ITEM_FAST_STORE_CONST**.
- **Implementation**: `koskari/lib/opcode/ITEM_VAR_STORE_CONST.c`

### KOS_OP_ITEM_VAR_STORE_VAR {#kos_op_item_var_store_var}
- **Style**: EXTRA (VAR, VAR, NOARG, NOARG).
- **Stack**: `[..., obj] → [...]`. Set obj[key] = value with the object on the stack, the key resolved by name and the value resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR key, VAR value, ITEM_STORE`, or from `VAR value` ahead of the shaped **ITEM_VAR_STORE**. May lower to **ITEM_FAST_STORE_FAST**.
- **Implementation**: `koskari/lib/opcode/ITEM_VAR_STORE_VAR.c`

### KOS_OP_ITEM_VAR_INPLACE {#kos_op_item_var_inplace}
- **Style**: EXTRA (OPCOUNT, VAR, NOARG, NOARG).
- **Stack**: `[..., obj, v0 .. v_{n-1}] → [...]`. Reduce obj[key] in place with the count values left to right and store it back with the object on the stack and the key resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Shaped by **`compile/inplace-item`** when OBJ is a symbol or KEY is a feeder and a value is not, so the fold could not reach the named operands. May lower to **ITEM_FAST_INPLACE**.
- **Implementation**: `koskari/lib/opcode/ITEM_VAR_INPLACE.c`

### KOS_OP_ITEM_VAR_INPLACE_CONST {#kos_op_item_var_inplace_const}
- **Style**: EXTRA (OPERAND, VAR, CONST, NOARG).
- **Stack**: `[..., obj] → [...]`. Reduce obj[key] in place with the pool constant and store it back with the object on the stack and the key resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR key, CONST rhs, ITEM_INPLACE` at a count of one, or from `CONST rhs` ahead of the shaped **ITEM_VAR_INPLACE**. May lower to **ITEM_FAST_INPLACE_CONST**.
- **Implementation**: `koskari/lib/opcode/ITEM_VAR_INPLACE_CONST.c`

### KOS_OP_VAR_ITEM {#kos_op_var_item}
- **Style**: VAR (the object's name).
- **Stack**: `[..., key] → [..., value]`. Get obj[key] with the object resolved by name and the key on the stack.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Shaped by **`compile/item`** when OBJ is a symbol and KEY is not a feeder, since no fold reaches an object load behind a computed key. May lower to **FAST_ITEM**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM.c`

### KOS_OP_VAR_ITEM_CLEAR {#kos_op_var_item_clear}
- **Style**: VAR (the object's name).
- **Stack**: `[..., key] → [...]`. Delete obj[key] with the object resolved by name and the key on the stack.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Shaped by **`compile/del-item`** when OBJ is a symbol and KEY is not a feeder, since no fold reaches an object load behind a computed key. May lower to **FAST_ITEM_CLEAR**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_CLEAR.c`

### KOS_OP_VAR_ITEM_TAKE {#kos_op_var_item_take}
- **Style**: VAR (the object's name).
- **Stack**: `[..., key] → [..., value]`. Get obj[key], delete it, and push the value with the object resolved by name and the key on the stack.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Shaped by **`compile/take-item`** when OBJ is a symbol and KEY is not a feeder, since no fold reaches an object load behind a computed key. May lower to **FAST_ITEM_TAKE**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_TAKE.c`

### KOS_OP_VAR_ITEM_REF {#kos_op_var_item_ref}
- **Style**: VAR (the object's name).
- **Stack**: `[..., key] → [..., value]`. Push an ItemRef over obj[key] with the object resolved by name and the key on the stack.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Shaped by **`compile/ref-item`** when OBJ is a symbol and KEY is not a feeder, since no fold reaches an object load behind a computed key. May lower to **FAST_ITEM_REF**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_REF.c`

### KOS_OP_VAR_ITEM_STORE {#kos_op_var_item_store}
- **Style**: VAR (the object's name).
- **Stack**: `[..., key, value] → [...]`. Set obj[key] = value with the object resolved by name, the key on the stack and the value on the stack.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Shaped by **`compile/set-item`** when OBJ is a symbol and KEY is not a feeder, since no fold reaches an object load behind a computed key. May lower to **FAST_ITEM_STORE**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_STORE.c`

### KOS_OP_VAR_ITEM_STORE_CONST {#kos_op_var_item_store_const}
- **Style**: EXTRA (VAR, CONST, NOARG, NOARG).
- **Stack**: `[..., key] → [...]`. Set obj[key] = value with the object resolved by name, the key on the stack and the value a pool constant.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `CONST value` ahead of **VAR_ITEM_STORE**, the row **`compile/set-item`** shapes for a symbol object under a computed key. May lower to **FAST_ITEM_STORE_CONST**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_STORE_CONST.c`

### KOS_OP_VAR_ITEM_STORE_VAR {#kos_op_var_item_store_var}
- **Style**: EXTRA (VAR, VAR, NOARG, NOARG).
- **Stack**: `[..., key] → [...]`. Set obj[key] = value with the object resolved by name, the key on the stack and the value resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR value` ahead of **VAR_ITEM_STORE**, the row **`compile/set-item`** shapes for a symbol object under a computed key. May lower to **FAST_ITEM_STORE_FAST**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_STORE_VAR.c`

### KOS_OP_VAR_ITEM_INPLACE {#kos_op_var_item_inplace}
- **Style**: EXTRA (OPCOUNT, VAR, NOARG, NOARG).
- **Stack**: `[..., key, v0 .. v_{n-1}] → [...]`. Reduce obj[key] in place with the count values left to right and store it back with the object resolved by name and the key on the stack.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Shaped by **`compile/inplace-item`** when OBJ is a symbol and KEY is not a feeder, since no fold reaches an object load behind a computed key. May lower to **FAST_ITEM_INPLACE**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_INPLACE.c`

### KOS_OP_VAR_ITEM_INPLACE_CONST {#kos_op_var_item_inplace_const}
- **Style**: EXTRA (OPERAND, VAR, CONST, NOARG).
- **Stack**: `[..., key] → [...]`. Reduce obj[key] in place with the pool constant and store it back with the object resolved by name and the key on the stack.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `CONST operand` ahead of **VAR_ITEM_INPLACE** at a count of one, the row **`compile/inplace-item`** shapes for a symbol object under a computed key. May lower to **FAST_ITEM_INPLACE_CONST**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_INPLACE_CONST.c`

### KOS_OP_VAR_ITEM_CONST {#kos_op_var_item_const}
- **Style**: EXTRA (VAR, CONST, NOARG, NOARG).
- **Stack**: `[...] → [..., value]`. Get obj[key] with the object resolved by name and the key a pool constant.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR obj, CONST key, ITEM`. May lower to **FAST_ITEM_CONST**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_CONST.c`

### KOS_OP_VAR_ITEM_CONST_CLEAR {#kos_op_var_item_const_clear}
- **Style**: EXTRA (VAR, CONST, NOARG, NOARG).
- **Stack**: No stack effect. Delete obj[key] with the object resolved by name and the key a pool constant.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR obj, CONST key, ITEM_CLEAR`. May lower to **FAST_ITEM_CONST_CLEAR**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_CONST_CLEAR.c`

### KOS_OP_VAR_ITEM_CONST_TAKE {#kos_op_var_item_const_take}
- **Style**: EXTRA (VAR, CONST, NOARG, NOARG).
- **Stack**: `[...] → [..., value]`. Get obj[key], delete it, and push the value with the object resolved by name and the key a pool constant.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR obj, CONST key, ITEM_TAKE`. May lower to **FAST_ITEM_CONST_TAKE**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_CONST_TAKE.c`

### KOS_OP_VAR_ITEM_CONST_REF {#kos_op_var_item_const_ref}
- **Style**: EXTRA (VAR, CONST, NOARG, NOARG).
- **Stack**: `[...] → [..., value]`. Push an ItemRef over obj[key] with the object resolved by name and the key a pool constant.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR obj, CONST key, ITEM_REF`. May lower to **FAST_ITEM_CONST_REF**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_CONST_REF.c`

### KOS_OP_VAR_ITEM_CONST_STORE {#kos_op_var_item_const_store}
- **Style**: EXTRA (VAR, CONST, NOARG, NOARG).
- **Stack**: `[..., value] → [...]`. Set obj[key] = value with the object resolved by name, the key a pool constant and the value on the stack.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Shaped by **`compile/set-item`** when OBJ is a symbol, KEY is a feeder and a value is not, so the fold could not reach the named operands. May lower to **FAST_ITEM_CONST_STORE**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_CONST_STORE.c`

### KOS_OP_VAR_ITEM_CONST_STORE_CONST {#kos_op_var_item_const_store_const}
- **Style**: EXTRA (VAR, CONST, CONST, NOARG).
- **Stack**: No stack effect. Set obj[key] = value with the object resolved by name, the key a pool constant and the value a pool constant.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR obj, CONST key, CONST value, ITEM_STORE`, or from `CONST value` ahead of the shaped **VAR_ITEM_CONST_STORE**. May lower to **FAST_ITEM_CONST_STORE_CONST**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_CONST_STORE_CONST.c`

### KOS_OP_VAR_ITEM_CONST_STORE_VAR {#kos_op_var_item_const_store_var}
- **Style**: EXTRA (VAR, CONST, VAR, NOARG).
- **Stack**: No stack effect. Set obj[key] = value with the object resolved by name, the key a pool constant and the value resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR obj, CONST key, VAR value, ITEM_STORE`, or from `VAR value` ahead of the shaped **VAR_ITEM_CONST_STORE**. May lower to **FAST_ITEM_CONST_STORE_FAST**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_CONST_STORE_VAR.c`

### KOS_OP_VAR_ITEM_CONST_INPLACE {#kos_op_var_item_const_inplace}
- **Style**: EXTRA (OPCOUNT, VAR, CONST, NOARG).
- **Stack**: `[..., v0 .. v_{n-1}] → [...]`. Reduce obj[key] in place with the count values left to right and store it back with the object resolved by name and the key a pool constant.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Shaped by **`compile/inplace-item`** when OBJ is a symbol, KEY is a feeder and a value is not, so the fold could not reach the named operands. May lower to **FAST_ITEM_CONST_INPLACE**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_CONST_INPLACE.c`

### KOS_OP_VAR_ITEM_CONST_INPLACE_CONST {#kos_op_var_item_const_inplace_const}
- **Style**: EXTRA (OPERAND, VAR, CONST, CONST).
- **Stack**: No stack effect. Reduce obj[key] in place with the pool constant and store it back with the object resolved by name and the key a pool constant.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR obj, CONST key, CONST rhs, ITEM_INPLACE` at a count of one, or from `CONST rhs` ahead of the shaped **VAR_ITEM_CONST_INPLACE**. May lower to **FAST_ITEM_CONST_INPLACE_CONST**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_CONST_INPLACE_CONST.c`

### KOS_OP_VAR_ITEM_VAR {#kos_op_var_item_var}
- **Style**: EXTRA (VAR, VAR, NOARG, NOARG).
- **Stack**: `[...] → [..., value]`. Get obj[key] with the object resolved by name and the key resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR obj, VAR key, ITEM`. May lower to **FAST_ITEM_FAST**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_VAR.c`

### KOS_OP_VAR_ITEM_VAR_CLEAR {#kos_op_var_item_var_clear}
- **Style**: EXTRA (VAR, VAR, NOARG, NOARG).
- **Stack**: No stack effect. Delete obj[key] with the object resolved by name and the key resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR obj, VAR key, ITEM_CLEAR`. May lower to **FAST_ITEM_FAST_CLEAR**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_VAR_CLEAR.c`

### KOS_OP_VAR_ITEM_VAR_TAKE {#kos_op_var_item_var_take}
- **Style**: EXTRA (VAR, VAR, NOARG, NOARG).
- **Stack**: `[...] → [..., value]`. Get obj[key], delete it, and push the value with the object resolved by name and the key resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR obj, VAR key, ITEM_TAKE`. May lower to **FAST_ITEM_FAST_TAKE**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_VAR_TAKE.c`

### KOS_OP_VAR_ITEM_VAR_REF {#kos_op_var_item_var_ref}
- **Style**: EXTRA (VAR, VAR, NOARG, NOARG).
- **Stack**: `[...] → [..., value]`. Push an ItemRef over obj[key] with the object resolved by name and the key resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR obj, VAR key, ITEM_REF`. May lower to **FAST_ITEM_FAST_REF**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_VAR_REF.c`

### KOS_OP_VAR_ITEM_VAR_STORE {#kos_op_var_item_var_store}
- **Style**: EXTRA (VAR, VAR, NOARG, NOARG).
- **Stack**: `[..., value] → [...]`. Set obj[key] = value with the object resolved by name, the key resolved by name and the value on the stack.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Shaped by **`compile/set-item`** when OBJ is a symbol, KEY is a feeder and a value is not, so the fold could not reach the named operands. May lower to **FAST_ITEM_FAST_STORE**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_VAR_STORE.c`

### KOS_OP_VAR_ITEM_VAR_STORE_CONST {#kos_op_var_item_var_store_const}
- **Style**: EXTRA (VAR, VAR, CONST, NOARG).
- **Stack**: No stack effect. Set obj[key] = value with the object resolved by name, the key resolved by name and the value a pool constant.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR obj, VAR key, CONST value, ITEM_STORE`, or from `CONST value` ahead of the shaped **VAR_ITEM_VAR_STORE**. May lower to **FAST_ITEM_FAST_STORE_CONST**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_VAR_STORE_CONST.c`

### KOS_OP_VAR_ITEM_VAR_STORE_VAR {#kos_op_var_item_var_store_var}
- **Style**: EXTRA (VAR, VAR, VAR, NOARG).
- **Stack**: No stack effect. Set obj[key] = value with the object resolved by name, the key resolved by name and the value resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR obj, VAR key, VAR value, ITEM_STORE`, or from `VAR value` ahead of the shaped **VAR_ITEM_VAR_STORE**. May lower to **FAST_ITEM_FAST_STORE_FAST**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_VAR_STORE_VAR.c`

### KOS_OP_VAR_ITEM_VAR_INPLACE {#kos_op_var_item_var_inplace}
- **Style**: EXTRA (OPCOUNT, VAR, VAR, NOARG).
- **Stack**: `[..., v0 .. v_{n-1}] → [...]`. Reduce obj[key] in place with the count values left to right and store it back with the object resolved by name and the key resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Shaped by **`compile/inplace-item`** when OBJ is a symbol, KEY is a feeder and a value is not, so the fold could not reach the named operands. May lower to **FAST_ITEM_FAST_INPLACE**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_VAR_INPLACE.c`

### KOS_OP_VAR_ITEM_VAR_INPLACE_CONST {#kos_op_var_item_var_inplace_const}
- **Style**: EXTRA (OPERAND, VAR, VAR, CONST).
- **Stack**: No stack effect. Reduce obj[key] in place with the pool constant and store it back with the object resolved by name and the key resolved by name.
- **Cache**: none; a VAR row survives lowering only for a name with no fast slot.
- **Compiler form:** Folded by the compress pipeline from `VAR obj, VAR key, CONST rhs, ITEM_INPLACE` at a count of one, or from `CONST rhs` ahead of the shaped **VAR_ITEM_VAR_INPLACE**. May lower to **FAST_ITEM_FAST_INPLACE_CONST**.
- **Implementation**: `koskari/lib/opcode/VAR_ITEM_VAR_INPLACE_CONST.c`


## 13. Fast variables

### KOS_OP_FAST

- **Style**: FASTVAR. Arg = fast local slot index.
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

### KOS_OP_FAST2 {#kos_op_fast2}

- **Style**: EXTRA (FASTVAR, FASTVAR, NOARG, NOARG). Row = two fast local
  slot indices.
- **Stack**: `[...] → [..., a, b]`. Load both slots and push in row order.
  Both slots resolve before either push, so an unbound slot errors with
  nothing pushed.
- **Invariants**: Slot indices in range for frame.
- **Compiler form:** Post-compile only: **`convert_fast`** lowering of
  **VAR2** when both names have fast slots. See §13 **FAST** for the slot
  model.
- **Implementation**: `koskari/lib/opcode/FAST2.c`
- **Covered by**: `tests/compile/test_pack_var_runs.py`,
  `tests/reproducers/test_var_pack.py`

**Example:** Load fast locals 0 and 1.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | FAST2  | 0   | (fast 0 (a), fast 1 (b), 0, 0) | stack: [..., a, b] |
| 1     | RETURN | 1   | 1        | |

---

### KOS_OP_FAST3 {#kos_op_fast3}

- **Style**: EXTRA (FASTVAR, FASTVAR, FASTVAR, NOARG). Row = three fast local slot indices.
- **Stack**: `[...] → [..., a, b, c]`. Load the three slots and push in row order. Every
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

---

### KOS_OP_FAST4 {#kos_op_fast4}

- **Style**: EXTRA (FASTVAR, FASTVAR, FASTVAR, FASTVAR). Row = four fast
  local slot indices.
- **Stack**: `[...] → [..., a, b, c, d]`. Load all four slots and push in row
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

---

### KOS_OP_FAST_BIND

- **Style**: FASTVAR. Arg = fast local slot index.
- **Stack**: `[..., ref] → [...]`. **BIND**: use this reference to resolve the
  given slot (name). See §1.9. Not interchangeable with FAST_STORE.
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

### KOS_OP_FAST_CLEAR2 {#kos_op_fast_clear2}

- **Style**: EXTRA (FASTVAR, FASTVAR, NOARG, NOARG). Row = 2 fast slot indices (post-compile).
- **Stack**: No stack effect. Clear the 2 fast slots in row order, as
  2 `FAST_CLEAR` would; an already empty one stays empty.
- **Compiler form:** Never emitted by specials. Produced only by
  `ByteCodeProcessor.pack_runs`, which combines a run of consecutive
  `FAST_CLEAR` rows (quads first, then one triple or one pair).

### KOS_OP_FAST_CLEAR3 {#kos_op_fast_clear3}

- **Style**: EXTRA (FASTVAR, FASTVAR, FASTVAR, NOARG). Row = 3 fast slot indices (post-compile).
- **Stack**: No stack effect. Clear the 3 fast slots in row order, as
  3 `FAST_CLEAR` would; an already empty one stays empty.
- **Compiler form:** Never emitted by specials. Produced only by
  `ByteCodeProcessor.pack_runs`, which combines a run of consecutive
  `FAST_CLEAR` rows (quads first, then one triple or one pair).

### KOS_OP_FAST_CLEAR4 {#kos_op_fast_clear4}

- **Style**: EXTRA (FASTVAR, FASTVAR, FASTVAR, FASTVAR). Row = 4 fast slot indices (post-compile).
- **Stack**: No stack effect. Clear the 4 fast slots in row order, as
  4 `FAST_CLEAR` would; an already empty one stays empty.
- **Compiler form:** Never emitted by specials. Produced only by
  `ByteCodeProcessor.pack_runs`, which combines a run of consecutive
  `FAST_CLEAR` rows (quads first, then one triple or one pair).

### KOS_OP_FAST_TAKE {#kos_op_fast_take}

- **Style**: FASTVAR. Arg = fast local slot index.
- **Stack**: `[...] → [..., value]`. Take value from slot via
  `KosNamespace_FastTake` (steal pointer on raw slots without extra INCREF),
  clear slot. **Errors with NameError if slot is empty** (contrast
  **FAST_CLEAR**, idempotent on empty declared slots).
- **Invariants**: Slot index in range; slot must hold a value.
- **Compiler form:** Lowered from **VAR_TAKE** when name has a fixed slot.
- **Implementation**: `koskari/lib/opcode/FAST_TAKE.c`
- **Covered by**: `tests/lib/opcode/test_take_opcodes.py`

**Example:** Take slot 1.

| index | opcode    | arg | arg view | comment |
|------:|-----------|-----|----------|---------|
| 2     | FAST_TAKE | 1   | 1        | take slot 1; stack: [..., value] |

---

### KOS_OP_FAST_REF

- **Style**: FASTVAR. Arg = fast local slot index.
- **Stack**: `[...] → [..., ref]`. Push reference to slot.
- **Invariants**: Slot index in range.

**Example:** Push reference to slot 0.

| index | opcode  | arg | arg view | comment |
|------:|---------|-----|----------|---------|
| 0     | FAST_REF| 0   | 0 (a)    | stack: [..., ref] |

---

### KOS_OP_FAST_STORE

- **Style**: FASTVAR. Arg = fast local slot index.
- **Stack**: `[..., value] → [...]`. **STORE**: store this value in the given
  slot (through the reference that already resolves it). See §1.9. Not
  interchangeable with FAST_BIND.
- **Invariants**: Slot must already be bound. Slot index in range.

**Example:** Store value 99 in slot 0.

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 0     | CONST      | 0   | 0 (99)   | stack: [..., 99] |
| 1     | FAST_STORE | 0   | 0 (a)    | STORE: write 99 into slot 0; stack: [...] |

---

### KOS_OP_FAST_STORE_CONST {#kos_op_fast_store_const}

- **Style**: EXTRA (FASTVAR, CONST, NOARG, NOARG). Arg = extra-table index (post-compile).
- **Stack**: `[...] → [...]` (no stack effect). **STORE**: store the constant value into the given fast slot via `KosNamespace_FastStore`. Same semantics as **FAST_STORE**, but the value is from the constant pool (extra row `a1`), not TOS.
- **Peephole**: Not fused directly from **`CONST`** + **`FAST_STORE`** in **`compress`** today; typically appears when **`VAR_STORE_CONST`** is lowered by **`convert_fast`** for a local or closure name.
- **Invariants**: Slot must already be bound; slot index in range; frame namespace must support fast locals (`KosNamespace_SupportsFast`).
- **Compiler form:** Pre-compile arg = 4-tuple `(slot, value, None, None)` (slot may be integer after **`convert_fast`**); post-compile arg = extra index.
- **Implementation**: `koskari/lib/opcode/FAST_STORE_CONST.c`
- **Covered by**: `tests/compile/test_convert_fast.py`, `tests/lib/opcode/test_store_const.py`

**Example:** Store constant 100 in slot 0 without pushing 100.

| index | opcode           | arg | arg view           | comment |
|------:|------------------|-----|--------------------|---------|
| 0     | FAST_STORE_CONST | 0   | 0 → (0, 100, …)    | STORE 100 in slot 0; stack unchanged |
| 1     | FAST             | 0   | 0 (a)              | stack: [..., 100] |
| 2     | RETURN           | 1   | 1                  | |


### KOS_OP_FAST_INPLACE {#kos_op_fast_inplace}

- **Style**: EXTRA (OPERAND, FASTVAR, NOARG, NOARG). Row a0 = the bare operator selector, a1 = the fast slot.
- **Stack**: `[..., rhs] → [...]`. Pop the one rhs and apply the operator to the slot's value with it, storing the result back; pushes nothing. Hot: prologue only, tail-calling the body the site's slot elected (an int, float, or str body for the accumulator and operand it first saw, with the result written into the accumulator in place when the slot is its only owner); an empty slot or an ungated site takes the electing function.
- **Compiler form:** The lowered form of **VAR_INPLACE**.
- **Implementation**: `koskari/lib/opcode/FAST_INPLACE.c`


### KOS_OP_FAST_INPLACE_CONST {#kos_op_fast_inplace_const}

- **Style**: EXTRA (OPERAND, FASTVAR, CONST, NOARG). Row a0 = bare operator selector, a1 = the fast slot, a2 = the operand.
- **Stack**: No stack effect. Augmented assignment with a constant operand, completely stack-free: deref the slot, apply the operator with the pool constant, store back. The compiled form of `(incr! i)`. Prologue only, as FAST_INPLACE; the operand's type was fixed at the election, so the body guards only the accumulator.
- **Compiler form:** The lowered form of **VAR_INPLACE_CONST**.
- **Implementation**: `koskari/lib/opcode/FAST_INPLACE_CONST.c`


### 13.1 Attribute access on fast locals (FAST_ATTR_*)

These opcodes perform attribute get, set, clear, take, or reference on an object fetched from the fast local slot. The object is not on the stack; it is resolved via `KosNamespace_FastDeref(ns, slot)`. Each uses EXTRA schema: FASTVAR (slot), ATTR (attr name).

- **Compiler form:** Emitted when VAR_ATTR_* is lowered (name has fixed slot).

### KOS_OP_FAST_ATTR {#kos_op_fast_attr}

- **Style**: EXTRA (FASTVAR, CONST, NOARG, NOARG).
- **Stack**: `[...] → [..., value]`. Get attribute on object in fast slot. Equivalent to ATTR but object from slot.
- **Implementation**: `koskari/lib/opcode/FAST_ATTR.c`

### KOS_OP_FAST_ATTR_STORE {#kos_op_fast_attr_store}

- **Style**: EXTRA (FASTVAR, CONST, NOARG, NOARG).
- **Stack**: `[..., value] → [...]`. Set attribute on object in fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ATTR_STORE.c`

### KOS_OP_FAST_ATTR_CLEAR {#kos_op_fast_attr_clear}

- **Style**: EXTRA (FASTVAR, CONST, NOARG, NOARG).
- **Stack**: No stack effect. Delete attribute on object in fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ATTR_CLEAR.c`

### KOS_OP_FAST_ATTR_TAKE {#kos_op_fast_attr_take}

- **Style**: EXTRA (FASTVAR, CONST, NOARG, NOARG).
- **Stack**: `[...] → [..., value]`. Get attribute on object in fast slot,
  delete it, push value.
- **Implementation**: `koskari/lib/opcode/FAST_ATTR_TAKE.c`

### KOS_OP_FAST_ATTR_REF {#kos_op_fast_attr_ref}

- **Style**: EXTRA (FASTVAR, CONST, NOARG, NOARG).
- **Stack**: `[...] → [..., attr_ref]`. Push attribute reference for object in fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ATTR_REF.c`


### KOS_OP_FAST_ATTR_INPLACE {#kos_op_fast_attr_inplace}

- **Style**: EXTRA (OPCOUNT, FASTVAR, ATTR, NOARG). Row a0 = operator selector and count, a1 = the object's fast slot, a2 = the attribute name.
- **Stack**: `[..., v0 .. v_{n-1}] → [...]`. Read the object from its slot, reduce obj.attr in place with the count values left to right, store it back; pushes nothing. Hot: prologue only, tail-calling the body the elector slot holds, which guards through the field slot and reads, computes, and stores through its location; an empty elector slot or an ungated site takes the electing function, and the generic path runs `kos_attr_inplace_cached` through the field slot.
- **Compiler form:** The lowered form of **VAR_ATTR_INPLACE**.
- **Implementation**: `koskari/lib/opcode/FAST_ATTR_INPLACE.c`


### KOS_OP_ITEM_STORE_FAST {#kos_op_item_store_fast}
- **Style**: FASTVAR (the value's slot).
- **Stack**: `[..., obj, key] → [...]`. Set obj[key] = value with the object on the stack, the key on the stack and the value read from a fast slot.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see §15).
- **Compiler form:** The lowering of **ITEM_STORE_VAR**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/ITEM_STORE_FAST.c`

### KOS_OP_ITEM_CONST_STORE_FAST {#kos_op_item_const_store_fast}
- **Style**: EXTRA (CONST, FASTVAR, NOARG, NOARG).
- **Stack**: `[..., obj] → [...]`. Set obj[key] = value with the object on the stack, the key a pool constant and the value read from a fast slot.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see §15).
- **Compiler form:** The lowering of **ITEM_CONST_STORE_VAR**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/ITEM_CONST_STORE_FAST.c`

### KOS_OP_ITEM_FAST {#kos_op_item_fast}
- **Style**: FASTVAR (the key's slot).
- **Stack**: `[..., obj] → [..., value]`. Get obj[key] with the object on the stack and the key read from a fast slot.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see §15).
- **Compiler form:** The lowering of **ITEM_VAR**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/ITEM_FAST.c`

### KOS_OP_ITEM_FAST_CLEAR {#kos_op_item_fast_clear}
- **Style**: FASTVAR (the key's slot).
- **Stack**: `[..., obj] → [...]`. Delete obj[key] with the object on the stack and the key read from a fast slot.
- **Compiler form:** The lowering of **ITEM_VAR_CLEAR**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/ITEM_FAST_CLEAR.c`

### KOS_OP_ITEM_FAST_TAKE {#kos_op_item_fast_take}
- **Style**: FASTVAR (the key's slot).
- **Stack**: `[..., obj] → [..., value]`. Get obj[key], delete it, and push the value with the object on the stack and the key read from a fast slot.
- **Compiler form:** The lowering of **ITEM_VAR_TAKE**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/ITEM_FAST_TAKE.c`

### KOS_OP_ITEM_FAST_REF {#kos_op_item_fast_ref}
- **Style**: FASTVAR (the key's slot).
- **Stack**: `[..., obj] → [..., value]`. Push an ItemRef over obj[key] with the object on the stack and the key read from a fast slot.
- **Compiler form:** The lowering of **ITEM_VAR_REF**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/ITEM_FAST_REF.c`

### KOS_OP_ITEM_FAST_STORE {#kos_op_item_fast_store}
- **Style**: FASTVAR (the key's slot).
- **Stack**: `[..., obj, value] → [...]`. Set obj[key] = value with the object on the stack, the key read from a fast slot and the value on the stack.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see §15).
- **Compiler form:** The lowering of **ITEM_VAR_STORE**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/ITEM_FAST_STORE.c`

### KOS_OP_ITEM_FAST_STORE_CONST {#kos_op_item_fast_store_const}
- **Style**: EXTRA (FASTVAR, CONST, NOARG, NOARG).
- **Stack**: `[..., obj] → [...]`. Set obj[key] = value with the object on the stack, the key read from a fast slot and the value a pool constant.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see §15).
- **Compiler form:** The lowering of **ITEM_VAR_STORE_CONST**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/ITEM_FAST_STORE_CONST.c`

### KOS_OP_ITEM_FAST_STORE_FAST {#kos_op_item_fast_store_fast}
- **Style**: EXTRA (FASTVAR, FASTVAR, NOARG, NOARG).
- **Stack**: `[..., obj] → [...]`. Set obj[key] = value with the object on the stack, the key read from a fast slot and the value read from a fast slot.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see §15).
- **Compiler form:** The lowering of **ITEM_VAR_STORE_VAR**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/ITEM_FAST_STORE_FAST.c`

### KOS_OP_ITEM_FAST_INPLACE {#kos_op_item_fast_inplace}
- **Style**: EXTRA (OPCOUNT, FASTVAR, NOARG, NOARG).
- **Stack**: `[..., obj, v0 .. v_{n-1}] → [...]`. Reduce obj[key] in place with the count values left to right and store it back with the object on the stack and the key read from a fast slot.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see §15).
- **Compiler form:** The lowering of **ITEM_VAR_INPLACE**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/ITEM_FAST_INPLACE.c`

### KOS_OP_ITEM_FAST_INPLACE_CONST {#kos_op_item_fast_inplace_const}
- **Style**: EXTRA (OPERAND, FASTVAR, CONST, NOARG).
- **Stack**: `[..., obj] → [...]`. Reduce obj[key] in place with the pool constant and store it back with the object on the stack and the key read from a fast slot.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see §15).
- **Compiler form:** The lowering of **ITEM_VAR_INPLACE_CONST**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/ITEM_FAST_INPLACE_CONST.c`

### KOS_OP_FAST_ITEM {#kos_op_fast_item}
- **Style**: FASTVAR (the object's slot).
- **Stack**: `[..., key] → [..., value]`. Get obj[key] with the object read from a fast slot and the key on the stack.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see §15).
- **Compiler form:** The lowering of **VAR_ITEM**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM.c`

### KOS_OP_FAST_ITEM_CLEAR {#kos_op_fast_item_clear}
- **Style**: FASTVAR (the object's slot).
- **Stack**: `[..., key] → [...]`. Delete obj[key] with the object read from a fast slot and the key on the stack.
- **Compiler form:** The lowering of **VAR_ITEM_CLEAR**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_CLEAR.c`

### KOS_OP_FAST_ITEM_TAKE {#kos_op_fast_item_take}
- **Style**: FASTVAR (the object's slot).
- **Stack**: `[..., key] → [..., value]`. Get obj[key], delete it, and push the value with the object read from a fast slot and the key on the stack.
- **Compiler form:** The lowering of **VAR_ITEM_TAKE**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_TAKE.c`

### KOS_OP_FAST_ITEM_REF {#kos_op_fast_item_ref}
- **Style**: FASTVAR (the object's slot).
- **Stack**: `[..., key] → [..., value]`. Push an ItemRef over obj[key] with the object read from a fast slot and the key on the stack.
- **Compiler form:** The lowering of **VAR_ITEM_REF**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_REF.c`

### KOS_OP_FAST_ITEM_STORE {#kos_op_fast_item_store}
- **Style**: FASTVAR (the object's slot).
- **Stack**: `[..., key, value] → [...]`. Set obj[key] = value with the object read from a fast slot, the key on the stack and the value on the stack.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see §15).
- **Compiler form:** The lowering of **VAR_ITEM_STORE**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_STORE.c`

### KOS_OP_FAST_ITEM_STORE_CONST {#kos_op_fast_item_store_const}
- **Style**: EXTRA (FASTVAR, CONST, NOARG, NOARG).
- **Stack**: `[..., key] → [...]`. Set obj[key] = value with the object read from a fast slot, the key on the stack and the value a pool constant.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see §15).
- **Compiler form:** The lowering of **VAR_ITEM_STORE_CONST**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_STORE_CONST.c`

### KOS_OP_FAST_ITEM_STORE_FAST {#kos_op_fast_item_store_fast}
- **Style**: EXTRA (FASTVAR, FASTVAR, NOARG, NOARG).
- **Stack**: `[..., key] → [...]`. Set obj[key] = value with the object read from a fast slot, the key on the stack and the value read from a fast slot.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see §15).
- **Compiler form:** The lowering of **VAR_ITEM_STORE_VAR**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_STORE_FAST.c`

### KOS_OP_FAST_ITEM_INPLACE {#kos_op_fast_item_inplace}
- **Style**: EXTRA (OPCOUNT, FASTVAR, NOARG, NOARG).
- **Stack**: `[..., key, v0 .. v_{n-1}] → [...]`. Reduce obj[key] in place with the count values left to right and store it back with the object read from a fast slot and the key on the stack.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see §15).
- **Compiler form:** The lowering of **VAR_ITEM_INPLACE**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_INPLACE.c`

### KOS_OP_FAST_ITEM_INPLACE_CONST {#kos_op_fast_item_inplace_const}
- **Style**: EXTRA (OPERAND, FASTVAR, CONST, NOARG).
- **Stack**: `[..., key] → [...]`. Reduce obj[key] in place with the pool constant and store it back with the object read from a fast slot and the key on the stack.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see §15).
- **Compiler form:** The lowering of **VAR_ITEM_INPLACE_CONST**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_INPLACE_CONST.c`

### KOS_OP_FAST_ITEM_CONST {#kos_op_fast_item_const}
- **Style**: EXTRA (FASTVAR, CONST, NOARG, NOARG).
- **Stack**: `[...] → [..., value]`. Get obj[key] with the object read from a fast slot and the key a pool constant.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see §15).
- **Compiler form:** The lowering of **VAR_ITEM_CONST**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_CONST.c`

### KOS_OP_FAST_ITEM_CONST_CLEAR {#kos_op_fast_item_const_clear}
- **Style**: EXTRA (FASTVAR, CONST, NOARG, NOARG).
- **Stack**: No stack effect. Delete obj[key] with the object read from a fast slot and the key a pool constant.
- **Compiler form:** The lowering of **VAR_ITEM_CONST_CLEAR**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_CONST_CLEAR.c`

### KOS_OP_FAST_ITEM_CONST_TAKE {#kos_op_fast_item_const_take}
- **Style**: EXTRA (FASTVAR, CONST, NOARG, NOARG).
- **Stack**: `[...] → [..., value]`. Get obj[key], delete it, and push the value with the object read from a fast slot and the key a pool constant.
- **Compiler form:** The lowering of **VAR_ITEM_CONST_TAKE**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_CONST_TAKE.c`

### KOS_OP_FAST_ITEM_CONST_REF {#kos_op_fast_item_const_ref}
- **Style**: EXTRA (FASTVAR, CONST, NOARG, NOARG).
- **Stack**: `[...] → [..., value]`. Push an ItemRef over obj[key] with the object read from a fast slot and the key a pool constant.
- **Compiler form:** The lowering of **VAR_ITEM_CONST_REF**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_CONST_REF.c`

### KOS_OP_FAST_ITEM_CONST_STORE {#kos_op_fast_item_const_store}
- **Style**: EXTRA (FASTVAR, CONST, NOARG, NOARG).
- **Stack**: `[..., value] → [...]`. Set obj[key] = value with the object read from a fast slot, the key a pool constant and the value on the stack.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see §15).
- **Compiler form:** The lowering of **VAR_ITEM_CONST_STORE**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_CONST_STORE.c`

### KOS_OP_FAST_ITEM_CONST_STORE_CONST {#kos_op_fast_item_const_store_const}
- **Style**: EXTRA (FASTVAR, CONST, CONST, NOARG).
- **Stack**: No stack effect. Set obj[key] = value with the object read from a fast slot, the key a pool constant and the value a pool constant.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see §15).
- **Compiler form:** The lowering of **VAR_ITEM_CONST_STORE_CONST**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_CONST_STORE_CONST.c`

### KOS_OP_FAST_ITEM_CONST_STORE_FAST {#kos_op_fast_item_const_store_fast}
- **Style**: EXTRA (FASTVAR, CONST, FASTVAR, NOARG).
- **Stack**: No stack effect. Set obj[key] = value with the object read from a fast slot, the key a pool constant and the value read from a fast slot.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see §15).
- **Compiler form:** The lowering of **VAR_ITEM_CONST_STORE_VAR**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_CONST_STORE_FAST.c`

### KOS_OP_FAST_ITEM_CONST_INPLACE {#kos_op_fast_item_const_inplace}
- **Style**: EXTRA (OPCOUNT, FASTVAR, CONST, NOARG).
- **Stack**: `[..., v0 .. v_{n-1}] → [...]`. Reduce obj[key] in place with the count values left to right and store it back with the object read from a fast slot and the key a pool constant.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see §15).
- **Compiler form:** The lowering of **VAR_ITEM_CONST_INPLACE**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_CONST_INPLACE.c`

### KOS_OP_FAST_ITEM_CONST_INPLACE_CONST {#kos_op_fast_item_const_inplace_const}
- **Style**: EXTRA (OPERAND, FASTVAR, CONST, CONST).
- **Stack**: No stack effect. Reduce obj[key] in place with the pool constant and store it back with the object read from a fast slot and the key a pool constant.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see §15).
- **Compiler form:** The lowering of **VAR_ITEM_CONST_INPLACE_CONST**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_CONST_INPLACE_CONST.c`

### KOS_OP_FAST_ITEM_FAST {#kos_op_fast_item_fast}
- **Style**: EXTRA (FASTVAR, FASTVAR, NOARG, NOARG).
- **Stack**: `[...] → [..., value]`. Get obj[key] with the object read from a fast slot and the key read from a fast slot.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see §15).
- **Compiler form:** The lowering of **VAR_ITEM_VAR**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_FAST.c`

### KOS_OP_FAST_ITEM_FAST_CLEAR {#kos_op_fast_item_fast_clear}
- **Style**: EXTRA (FASTVAR, FASTVAR, NOARG, NOARG).
- **Stack**: No stack effect. Delete obj[key] with the object read from a fast slot and the key read from a fast slot.
- **Compiler form:** The lowering of **VAR_ITEM_VAR_CLEAR**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_FAST_CLEAR.c`

### KOS_OP_FAST_ITEM_FAST_TAKE {#kos_op_fast_item_fast_take}
- **Style**: EXTRA (FASTVAR, FASTVAR, NOARG, NOARG).
- **Stack**: `[...] → [..., value]`. Get obj[key], delete it, and push the value with the object read from a fast slot and the key read from a fast slot.
- **Compiler form:** The lowering of **VAR_ITEM_VAR_TAKE**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_FAST_TAKE.c`

### KOS_OP_FAST_ITEM_FAST_REF {#kos_op_fast_item_fast_ref}
- **Style**: EXTRA (FASTVAR, FASTVAR, NOARG, NOARG).
- **Stack**: `[...] → [..., value]`. Push an ItemRef over obj[key] with the object read from a fast slot and the key read from a fast slot.
- **Compiler form:** The lowering of **VAR_ITEM_VAR_REF**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_FAST_REF.c`

### KOS_OP_FAST_ITEM_FAST_STORE {#kos_op_fast_item_fast_store}
- **Style**: EXTRA (FASTVAR, FASTVAR, NOARG, NOARG).
- **Stack**: `[..., value] → [...]`. Set obj[key] = value with the object read from a fast slot, the key read from a fast slot and the value on the stack.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see §15).
- **Compiler form:** The lowering of **VAR_ITEM_VAR_STORE**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_FAST_STORE.c`

### KOS_OP_FAST_ITEM_FAST_STORE_CONST {#kos_op_fast_item_fast_store_const}
- **Style**: EXTRA (FASTVAR, FASTVAR, CONST, NOARG).
- **Stack**: No stack effect. Set obj[key] = value with the object read from a fast slot, the key read from a fast slot and the value a pool constant.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see §15).
- **Compiler form:** The lowering of **VAR_ITEM_VAR_STORE_CONST**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_FAST_STORE_CONST.c`

### KOS_OP_FAST_ITEM_FAST_STORE_FAST {#kos_op_fast_item_fast_store_fast}
- **Style**: EXTRA (FASTVAR, FASTVAR, FASTVAR, NOARG).
- **Stack**: No stack effect. Set obj[key] = value with the object read from a fast slot, the key read from a fast slot and the value read from a fast slot.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see §15).
- **Compiler form:** The lowering of **VAR_ITEM_VAR_STORE_VAR**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_FAST_STORE_FAST.c`

### KOS_OP_FAST_ITEM_FAST_INPLACE {#kos_op_fast_item_fast_inplace}
- **Style**: EXTRA (OPCOUNT, FASTVAR, FASTVAR, NOARG).
- **Stack**: `[..., v0 .. v_{n-1}] → [...]`. Reduce obj[key] in place with the count values left to right and store it back with the object read from a fast slot and the key read from a fast slot.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see §15).
- **Compiler form:** The lowering of **VAR_ITEM_VAR_INPLACE**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_FAST_INPLACE.c`

### KOS_OP_FAST_ITEM_FAST_INPLACE_CONST {#kos_op_fast_item_fast_inplace_const}
- **Style**: EXTRA (OPERAND, FASTVAR, FASTVAR, CONST).
- **Stack**: No stack effect. Reduce obj[key] in place with the pool constant and store it back with the object read from a fast slot and the key read from a fast slot.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see §15).
- **Compiler form:** The lowering of **VAR_ITEM_VAR_INPLACE_CONST**: every named operand has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITEM_FAST_INPLACE_CONST.c`


## 14. Attributes

### KOS_OP_ATTR

- **Style**: CONST. Arg = constant pool index of attribute name (post-compile).
- **Stack**: `[..., obj] → [..., value]`. Get attribute; the value replaces the object.
- **Cache**: one field slot; a `__slots__` member or an inline instance value is read directly on a hit, as FAST_ATTR does.
- **Invariants**: TOS is the object. Name in constant pool.
- **Compiler form:** Pre-compile arg = attribute name (string); post-compile arg = pool index.

**Example:** Get obj.name. Pool: 0 = "name".

| index | opcode | arg | arg view   | comment |
|------:|--------|-----|------------|---------|
| 0     | VAR    | 0   | 0 ('obj')  | stack: [..., obj] |
| 1     | ATTR   | 1   | 1 ('name') | stack: [..., value] |
| 2     | RETURN | 1   | 1          | |

---

### KOS_OP_ATTR_STORE

- **Style**: CONST. Arg = constant pool index of attribute name.
- **Stack**: `[..., obj, value] → [...]`. Set attribute on obj; pop both.
- **Cache**: one field slot; a `__slots__` member or an inline instance value is written directly on a hit, as FAST_ATTR_STORE does.
- **Invariants**: Below TOS is object; name in pool.

**Example:** Set obj.x = TOS. Pool: 0 = "x".

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 0     | VAR        | 0   | 0 ('obj')| stack: [..., obj] |
| 1     | CONST      | 1   | 1 (10)   | stack: [..., obj, 10] |
| 2     | ATTR_STORE | 2   | 2 ('x')  | obj.x = 10; stack: [...] |

---

### KOS_OP_ATTR_CLEAR

- **Style**: CONST. Arg = constant pool index of attribute name.
- **Stack**: `[..., obj] → [...]`. Delete attribute.
- **Invariants**: TOS is the object.

**Example:** Delete obj.attr. Pool: 0 = "attr".

| index | opcode     | arg | arg view  | comment |
|------:|------------|-----|-----------|---------|
| 0     | VAR        | 0   | 0 ('obj') | stack: [..., obj] |
| 1     | ATTR_CLEAR | 1   | 1 ('attr')| del obj.attr |

---

### KOS_OP_ATTR_TAKE {#kos_op_attr_take}

- **Style**: CONST. Arg = constant pool index of attribute name.
- **Stack**: `[..., obj] → [..., value]`. Get attribute, delete it, push value.
  Errors if attribute missing.
- **Invariants**: TOS is the object.
- **Compiler form:** Emitted by **`compile/take-attr`** when OBJ is not a
  symbol.
- **Implementation**: `koskari/lib/opcode/ATTR_TAKE.c`
- **Covered by**: `tests/lib/opcode/test_take_opcodes.py`

**Example:** Take obj.attr. Pool: 0 = "attr".

| index | opcode    | arg | arg view  | comment |
|------:|-----------|-----|-----------|---------|
| 0     | VAR       | 0   | 0 ('obj') | stack: [..., obj] |
| 1     | ATTR_TAKE | 1   | 1 ('attr')| take obj.attr; stack: [..., value] |

---

### KOS_OP_ATTR_REF

- **Style**: CONST. Arg = constant pool index of attribute name.
- **Stack**: `[..., obj] → [..., obj, attr_ref]`. Push attribute reference.
- **Invariants**: TOS is the object.

**Example:** Push reference for obj.prop. Pool: 0 = "prop".

| index | opcode   | arg | arg view  | comment |
|------:|----------|-----|-----------|---------|
| 0     | VAR      | 0   | 0 ('obj') | stack: [..., obj] |
| 1     | ATTR_REF | 1   | 1 ('prop')| stack: [..., obj, attr_ref] |


### KOS_OP_ATTR_INPLACE {#kos_op_attr_inplace}

- **Style**: EXTRA (OPCOUNT, ATTR, NOARG, NOARG). Row a0 = operator selector and count, a1 = the attribute name.
- **Stack**: `[..., obj, v0 .. v_{n-1}] → [...]`. Reduce obj.attr in place with the count values left to right, store it back, drop the object and the values; pushes nothing (the special appends CONST None). The operator switch lives in the shared `kos_inplace_reduce`.
- **Compiler form:** Emitted by **`compile/inplace-attr`** when OBJ is an expression.
- **Implementation**: `koskari/lib/opcode/ATTR_INPLACE.c`


## 15. Items

The item rows are named by a three-position grammar: the leading
marker names the object's origin, ITEM is followed by the key's, and a
STORE or INPLACE verb by its value's. An unmarked operand rides the
stack; CONST is a pool constant, VAR a name resolved in the frame
namespace (§12), FAST a fast slot (§13). FAST_ITEM_CONST_STORE_FAST is
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
the value, and the compress pipeline folds the run of feeders directly
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

### KOS_OP_ITEM {#kos_op_item}
- **Style**: NOARG.
- **Stack**: `[..., obj, key] → [..., value]`. Get obj[key] with the object on the stack and the key on the stack.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see §15).
- **Compiler form:** Emitted by **`compile/item`**; the compress pipeline folds the run of feeders (bare VAR and CONST loads) ahead of it into the rows that name them.
- **Implementation**: `koskari/lib/opcode/ITEM.c`

### KOS_OP_ITEM_CLEAR {#kos_op_item_clear}
- **Style**: NOARG.
- **Stack**: `[..., obj, key] → [...]`. Delete obj[key] with the object on the stack and the key on the stack.
- **Compiler form:** Emitted by **`compile/del-item`**; the compress pipeline folds the run of feeders (bare VAR and CONST loads) ahead of it into the rows that name them.
- **Implementation**: `koskari/lib/opcode/ITEM_CLEAR.c`

### KOS_OP_ITEM_TAKE {#kos_op_item_take}
- **Style**: NOARG.
- **Stack**: `[..., obj, key] → [..., value]`. Get obj[key], delete it, and push the value with the object on the stack and the key on the stack.
- **Compiler form:** Emitted by **`compile/take-item`**; the compress pipeline folds the run of feeders (bare VAR and CONST loads) ahead of it into the rows that name them.
- **Implementation**: `koskari/lib/opcode/ITEM_TAKE.c`

### KOS_OP_ITEM_REF {#kos_op_item_ref}
- **Style**: NOARG.
- **Stack**: `[..., obj, key] → [..., value]`. Push an ItemRef over obj[key] with the object on the stack and the key on the stack.
- **Compiler form:** Emitted by **`compile/ref-item`**; the compress pipeline folds the run of feeders (bare VAR and CONST loads) ahead of it into the rows that name them.
- **Implementation**: `koskari/lib/opcode/ITEM_REF.c`

### KOS_OP_ITEM_STORE {#kos_op_item_store}
- **Style**: NOARG.
- **Stack**: `[..., obj, key, value] → [...]`. Set obj[key] = value with the object on the stack, the key on the stack and the value on the stack.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see §15).
- **Compiler form:** Emitted by **`compile/set-item`**; the compress pipeline folds the run of feeders (bare VAR and CONST loads) ahead of it into the rows that name them.
- **Implementation**: `koskari/lib/opcode/ITEM_STORE.c`

### KOS_OP_ITEM_STORE_CONST {#kos_op_item_store_const}
- **Style**: CONST (the value).
- **Stack**: `[..., obj, key] → [...]`. Set obj[key] = value with the object on the stack, the key on the stack and the value a pool constant.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see §15).
- **Compiler form:** Folded by the compress pipeline from `CONST value, ITEM_STORE`.
- **Implementation**: `koskari/lib/opcode/ITEM_STORE_CONST.c`

### KOS_OP_ITEM_INPLACE {#kos_op_item_inplace}
- **Style**: OPCOUNT (operator selector, value count).
- **Stack**: `[..., obj, key, v0 .. v_{n-1}] → [...]`. Reduce obj[key] in place with the count values left to right and store it back with the object on the stack and the key on the stack.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see §15).
- **Compiler form:** Emitted by **`compile/inplace-item`**; the compress pipeline folds the run of feeders (bare VAR and CONST loads) ahead of it into the rows that name them.
- **Implementation**: `koskari/lib/opcode/ITEM_INPLACE.c`

### KOS_OP_ITEM_INPLACE_CONST {#kos_op_item_inplace_const}
- **Style**: EXTRA (OPERAND, CONST, NOARG, NOARG).
- **Stack**: `[..., obj, key] → [...]`. Reduce obj[key] in place with the pool constant and store it back with the object on the stack and the key on the stack.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see §15).
- **Compiler form:** Folded by the compress pipeline from `CONST rhs, ITEM_INPLACE` at a count of one, or from `CONST rhs` ahead of the shaped **ITEM_INPLACE**.
- **Implementation**: `koskari/lib/opcode/ITEM_INPLACE_CONST.c`

### KOS_OP_ITEM_CONST {#kos_op_item_const}
- **Style**: CONST (the key).
- **Stack**: `[..., obj] → [..., value]`. Get obj[key] with the object on the stack and the key a pool constant.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see §15).
- **Compiler form:** Folded by the compress pipeline from `CONST key, ITEM`.
- **Implementation**: `koskari/lib/opcode/ITEM_CONST.c`

### KOS_OP_ITEM_CONST_CLEAR {#kos_op_item_const_clear}
- **Style**: CONST (the key).
- **Stack**: `[..., obj] → [...]`. Delete obj[key] with the object on the stack and the key a pool constant.
- **Compiler form:** Folded by the compress pipeline from `CONST key, ITEM_CLEAR`.
- **Implementation**: `koskari/lib/opcode/ITEM_CONST_CLEAR.c`

### KOS_OP_ITEM_CONST_TAKE {#kos_op_item_const_take}
- **Style**: CONST (the key).
- **Stack**: `[..., obj] → [..., value]`. Get obj[key], delete it, and push the value with the object on the stack and the key a pool constant.
- **Compiler form:** Folded by the compress pipeline from `CONST key, ITEM_TAKE`.
- **Implementation**: `koskari/lib/opcode/ITEM_CONST_TAKE.c`

### KOS_OP_ITEM_CONST_REF {#kos_op_item_const_ref}
- **Style**: CONST (the key).
- **Stack**: `[..., obj] → [..., value]`. Push an ItemRef over obj[key] with the object on the stack and the key a pool constant.
- **Compiler form:** Folded by the compress pipeline from `CONST key, ITEM_REF`.
- **Implementation**: `koskari/lib/opcode/ITEM_CONST_REF.c`

### KOS_OP_ITEM_CONST_STORE {#kos_op_item_const_store}
- **Style**: CONST (the key).
- **Stack**: `[..., obj, value] → [...]`. Set obj[key] = value with the object on the stack, the key a pool constant and the value on the stack.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see §15).
- **Compiler form:** Shaped by **`compile/set-item`** when OBJ is a symbol or KEY is a feeder and a value is not, so the fold could not reach the named operands.
- **Implementation**: `koskari/lib/opcode/ITEM_CONST_STORE.c`

### KOS_OP_ITEM_CONST_STORE_CONST {#kos_op_item_const_store_const}
- **Style**: EXTRA (CONST, CONST, NOARG, NOARG).
- **Stack**: `[..., obj] → [...]`. Set obj[key] = value with the object on the stack, the key a pool constant and the value a pool constant.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see §15).
- **Compiler form:** Folded by the compress pipeline from `CONST key, CONST value, ITEM_STORE`, or from `CONST value` ahead of the shaped **ITEM_CONST_STORE**.
- **Implementation**: `koskari/lib/opcode/ITEM_CONST_STORE_CONST.c`

### KOS_OP_ITEM_CONST_INPLACE {#kos_op_item_const_inplace}
- **Style**: EXTRA (OPCOUNT, CONST, NOARG, NOARG).
- **Stack**: `[..., obj, v0 .. v_{n-1}] → [...]`. Reduce obj[key] in place with the count values left to right and store it back with the object on the stack and the key a pool constant.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see §15).
- **Compiler form:** Shaped by **`compile/inplace-item`** when OBJ is a symbol or KEY is a feeder and a value is not, so the fold could not reach the named operands.
- **Implementation**: `koskari/lib/opcode/ITEM_CONST_INPLACE.c`

### KOS_OP_ITEM_CONST_INPLACE_CONST {#kos_op_item_const_inplace_const}
- **Style**: EXTRA (OPERAND, CONST, CONST, NOARG).
- **Stack**: `[..., obj] → [...]`. Reduce obj[key] in place with the pool constant and store it back with the object on the stack and the key a pool constant.
- **Cache**: one KOS_CACHE_ITEM slot; the site elects a body for the container kind it first sees through the shared helper in `eval/eval_item.c` (see §15).
- **Compiler form:** Folded by the compress pipeline from `CONST key, CONST rhs, ITEM_INPLACE` at a count of one, or from `CONST rhs` ahead of the shaped **ITEM_CONST_INPLACE**.
- **Implementation**: `koskari/lib/opcode/ITEM_CONST_INPLACE_CONST.c`


## 15.1 Reference dereference

### KOS_OP_DEREF

- **Style**: NOARG.
- **Stack**: `[..., ref] → [..., value]`. Pop one value (a Ref with a deref method). Call `ref.deref()`, push result. Raises TypeError if TOS has no deref method.
- **Use-case**: Fast path for dereferencing a mutable reference. Used by the `*identifier` reader atom and `(compile/deref EXPR)` special form.

**Example:** Dereference a ref. For `*r` where r holds a Ref: VAR (or FAST) loads r, DEREF calls ref.deref().

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | VAR    | 0   | 0 ('r')  | stack: [..., ref] |
| 1     | DEREF  | 0   | 0        | stack: [..., value] |


## 16. Argument production

### KOS_OP_BUILD_ARGS

- **Style**: COUNT. Arg = number of positional arguments.
- **Stack**: `[..., v_0, ..., v_{n-1}] → [..., tuple]`. Pop n elements and push a tuple of them.
- **Example:** Build empty args. Covered by `tests/lib/opcode/test_arg_opcodes.py`.

| index | opcode    | arg | arg view | comment |
|------:|-----------|-----|----------|---------|
| 0     | BUILD_ARGS| 0   | 0        | stack: [..., ()] |
| 1     | RETURN    | 1   | 1        | |

---

### KOS_OP_BUILD_LIST

- **Style**: COUNT. Arg = number of elements.
- **Stack**: `[..., v_0, ..., v_{n-1}] → [..., list]`. Pop n elements into a new list and push it. Empty (arg 0) pushes `[]`.
- **Compiler form:** `compile/#list` for a leading/all dynamic run (`collections.py`).

### KOS_OP_BUILD_LIST_CONST

- **Style**: CONST. Arg = constant pool index of a sequence (typically a pooled constant tuple).
- **Stack**: `[...] → [..., list]`. Build a **fresh** list from the pooled sequence (`PySequence_List`) and push it; each eval mints a new list, never aliasing the immutable pool value.
- **Compiler form:** `compile/#list` all-constant body, or the seed of a list whose first run is constant.

### KOS_OP_VAR_LIST_APPEND

- **Style**: VAR. Arg = variable name (may lower to FAST_LIST_APPEND).
- **Stack**: `[..., value] → [...]`. Append TOS to the list bound to the named variable, in place. Used by `compile/#list-append` on a symbol binding.

### KOS_OP_FAST_LIST_APPEND

- **Style**: FASTVAR. Arg = frame slot index.
- **Stack**: `[..., value] → [...]`. Fast-slot form of VAR_LIST_APPEND.

### KOS_OP_BUILD_LIST_APPEND

- **Style**: COUNT. Arg = number of values to append.
- **Stack**: `[..., list, v_0, ..., v_{n-1}] → [..., list]`. Append the top n values to the list at depth n, in place; pop the values; leave the list. TypeError when the list slot is not a list. (Widened from the former NOARG append-one; `compile/#list-append` on a list expression emits count 1.)
- **Compiler form:** `compile/#list` dynamic tail run of k; `compile/#list-append` list-expression path (count 1).

### KOS_OP_BUILD_LIST_APPEND_CONST

- **Style**: CONST. Arg = constant pool index of a single value.
- **Stack**: `[..., list] → [..., list]`. Append the pooled value to the TOS list in place. TypeError when TOS is not a list.
- **Compiler form:** `compile/#list` tail constant run of length 1.

### KOS_OP_BUILD_LIST_EXTEND

- **Style**: NOARG.
- **Stack**: `[..., list, iterable] → [..., list]`. Extend the list in place from the TOS iterable (`PySequence_InPlaceConcat`, so any iterable the list API accepts); pop the iterable; leave the list. TypeError when the list slot is not a list.
- **Compiler form:** `compile/#list` `*:` splat segment.

### KOS_OP_BUILD_LIST_EXTEND_CONST

- **Style**: CONST. Arg = constant pool index of a sequence (typically a pooled constant tuple).
- **Stack**: `[..., list] → [..., list]`. Extend the TOS list in place from the pooled sequence (tuple fast path, general-iterable fallback). TypeError when TOS is not a list.
- **Compiler form:** `compile/#list` tail constant run of length ≥ 2.

---

### KOS_OP_BUILD_FUNCTION

- **Style**: EXTRA (`CONST`, `CONST`, `NOARG`, `NOARG`). Instruction arg =
  index into the code object's **extra** table; that row has `a0` = constant
  pool index of the **keyword-defaults names tuple** and `a1` = constant pool
  index of the **annotation names tuple**. Either or both tuples may be the
  empty tuple, meaning "no values of that kind on the stack".
- **Semantics**: Pop `fspec` (TOS). If the defaults names tuple is non-empty
  (length N), pop N values into a defaults dict keyed by it
  (`KosContext_StackPopTailDictFromNames`, same discipline as
  BUILD_KWDS_NAMES). Then, if the annotation names tuple is non-empty
  (length M), pop M more values into an annotations dict the same way.
  Build the function against the current frame namespace via
  `KosFunction_New(fspec, defaults, frame_ns)`, or
  `KosFunction_NewWithAnnotations(fspec, defaults, frame_ns, annotations)`
  when an annotations dict was built, and push the result. The annotations
  dict is the function's **creation-time** annotations: values are real
  objects, except str values, which remain forward references resolved
  lazily on first `__annotations__` / `__signature__` access.
- **Stack**: `[..., ann_1, ..., ann_M, def_1, ..., def_N, fspec] →
  [..., function]` with M = len(annotation names), N = len(defaults names);
  either may be 0. Annotation values sit **below** the default values.
- **Invariants**: Both extra-row fields must be constant-pool tuples; the
  stack must hold M + N values below fspec. The compiler pushes annotation
  values first, then default values, matching pop order (defaults popped
  first, annotations second).
- **Compiler form:** Pre-compile extra operand is a 4-tuple
  `(defnames_tuple, annotation_names_tuple, 0, 0)`; `NOARG` slots are
  ignored by the assembler. Emitted by `helper_function`
  (`koskari/compile/specials/lambda_.py`); the annotation names tuple is
  annotated parameter names in order, followed by `"return"` when a return
  annotation is present.
- **Covered by**: `tests/lib/opcode/test_build_function.py`

**Example:** Build annotated function with one keyword default:
`(lambda [(a :: int value: 9)] a)`. Pool: 0 = int (via VAR in practice;
CONST here for brevity), 1 = 9, 2 = fspec; extra row: `(("a",), ("a",), 0, 0)`.

| index | opcode         | arg | arg view              | comment |
|------:|----------------|-----|-----------------------|---------|
| 0     | CONST          | 0   | 0 (int)               | annotation value; stack: [..., int] |
| 1     | CONST          | 1   | 1 (9)                 | default value; stack: [..., int, 9] |
| 2     | CONST          | 2   | 2 (fspec)             | stack: [..., int, 9, fspec] |
| 3     | BUILD_FUNCTION | 0   | 0 (("a",), ("a",), …) | stack: [..., function] |
| 4     | RETURN         | 1   | 1                     | |

Un-annotated, no-defaults form: extra row `((), (), 0, 0)`; only fspec is
popped.

---

### KOS_OP_BUILD_KWDS

- **Style**: PAIR_COUNT. Arg = number of keyword pairs.
- **Stack**: `[..., k_0, v_0, ..., k_{n-1}, v_{n-1}] → [..., dict]`. Pop 2*opa elements as key,value pairs and push a dict.

### KOS_OP_BUILD_KWDS_NAMES

- **Style**: CONST. Arg = constant pool index of names tuple.
- **Stack**: `[..., v_0, ..., v_{n-1}] → [..., dict]`. Pop n values, map names[i] → value[i], push dict. Names tuple from constant pool; values from stack (val_0 deepest, val_{n-1} at TOS).

### KOS_OP_COMBINE_ARGS

- **Style**: COUNT. Arg = count of sequences to combine.
- **Stack**: `[..., t_0, ..., t_{m-1}] → [..., tuple]`. Concatenate the m sequences (each via iteration) into a single tuple. Non-tuple values are converted with PySequence_Tuple when m == 1.

### KOS_OP_COMBINE_KWDS

- **Style**: COUNT. Arg = count of dicts to merge.
- **Stack**: `[..., d_0, ..., d_{m-1}] → [..., dict]`. Merge the m dicts (later overwrites earlier on key clash) and push result.


## 17. Calls

### KOS_OP_CALL

- **Style**: COUNT. Arg = number of positional arguments.
- **Stack**: `[..., callable, arg_1, ..., arg_n] → [..., result]`. KosExpression/KosFunction: new frame pushed, in-loop execution. Python callable: may suspend on awaitable.
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

### KOS_OP_CALL_NAMES {#kos_op_call_names}

- **Style**: COUNT. Arg = N (number of positional arguments).
- **Stack**: `[..., callable, pos_0, ..., pos_{N-1}, val_0, ..., val_{M-1}, names_tuple] → [..., result]`. TOS = names_tuple (tuple of M keyword names); below it the M keyword values in the same order as names; below that the N positional args; then callable. The names tuple is validated, then the window of callable, positionals and keyword values goes to `kos_call_invoke` with the names; no tuple or dict allocation.
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

### KOS_OP_CALL_STAR {#kos_op_call_star}

- **Style**: STAR. Arg = bitflags (1 = has *args, 2 = has **kwargs).
- **Stack**: `[..., callable, pos, kwd]` with kwd at TOS. pos = *args (tuple or iterable); kwd = **kwargs (dict). Pop per opa: both (opa & 3 == 3): pop 3; **kwargs only (opa & 2): pop 2, pos = (); *args only (opa & 1): pop 2, kwd = NULL; neither: pop 1, pos = (), kwd = NULL. `kos_op_call_star` pops the operands the flags name, coerces pos to a tuple and kwd to a dict (TypeError naming the type otherwise), and calls through the shared core.
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

### KOS_OP_CALL_VAR {#kos_op_call_var}

- **Style**: EXTRA. Schema: VAR (name cpool idx), COUNT (npos), NOARG, NOARG.
- **Stack**: `[..., pos_0, ..., pos_{N-1}] → [..., result]`. Callable is loaded from the frame namespace by name (`frame_ns_deref`, owned); not on stack. The argv window is borrowed and handed with the callable to `kos_call_invoke` under `KOS_CALL_CALLABLE_OWNED`, which owns both the window drop and the callable release.
- **Compiler form:** Emitted by `compile/call` when the callee is a symbol that reads a variable (`symbol_is_variable`), and by `compile/quasiquote` for its runtime bracket helpers. `convert_fast` lowers it to **CALL_FAST** when the name has a fast slot; `promote_tail_calls` relabels a tail-position row to **TAIL_CALL_VAR**.

---

### KOS_OP_CALL_FAST {#kos_op_call_fast}

- **Style**: EXTRA. Schema: FASTVAR (slot index), COUNT (npos), NOARG, NOARG.
- **Stack**: Same as CALL_VAR. Callable is loaded from the fast local slot (`kos_ns_fast_deref`, owned); not on stack. A non-tail row first tries the frame cache (`kos_call_cached_frame_install`); otherwise it hands the window to `kos_call_invoke` as CALL_VAR does.
- **Compiler form:** The `convert_fast` lowering of **CALL_VAR**; never emitted by a special.

---

### KOS_OP_CALL_VAR_NAMES {#kos_op_call_var_names}

- **Style**: EXTRA. Schema: VAR (name cpool idx), CONST (kw names tuple cpool idx), COUNT (npos), NOARG.
- **Stack**: `[..., pos_0, ..., pos_{N-1}, kwd_val_0, ..., kwd_val_{M-1}] → [..., result]`. Callable from namespace by name; kw names tuple from constant pool. Same call semantics as CALL_NAMES.
- **Compiler form:** Emitted by `compile/call` for a variable callee with keyword arguments and no `*:` or `**:`. Lowers to **CALL_FAST_NAMES**.

---

### KOS_OP_CALL_FAST_NAMES {#kos_op_call_fast_names}

- **Style**: EXTRA. Schema: FASTVAR (slot index), CONST (kw names tuple cpool idx), COUNT (npos), NOARG.
- **Stack**: Same as CALL_VAR_NAMES. Callable from fast slot via `KosNamespace_FastDeref`.
- **Compiler form:** The `convert_fast` lowering of **CALL_VAR_NAMES**; never emitted by a special.

---

### KOS_OP_CALL_VAR_STAR {#kos_op_call_var_star}

- **Style**: EXTRA. Schema: VAR (name cpool idx), STAR (bitflags: 1 = *args, 2 = **kwargs), NOARG, NOARG.
- **Stack**: Same as CALL_STAR minus callable. `[..., pos, kwd]` or subsets per flags. Callable from namespace by name (owned), stolen by `kos_op_call_star`, which pops per flags, coerces pos and kwd, and calls through the shared core.
- **Compiler form:** Emitted by `compile/call` for a variable callee with `*:` or `**:`. Lowers to **CALL_FAST_STAR**.

---

### KOS_OP_CALL_FAST_STAR {#kos_op_call_fast_star}

- **Style**: EXTRA. Schema: FASTVAR (slot index), STAR (bitflags), NOARG, NOARG.
- **Stack**: Same as CALL_VAR_STAR. Callable from fast slot via `KosNamespace_FastDeref`.
- **Compiler form:** The `convert_fast` lowering of **CALL_VAR_STAR**; never emitted by a special.

---

### KOS_OP_CALL_ATTR {#kos_op_call_attr}

- **Style**: EXTRA. Schema: ATTR (attr name), COUNT (npos), NOARG, NOARG.
- **Stack**: `[..., obj, arg_0, ..., arg_{N-1}] → [..., result]`. Attr name from constant pool (EXTRA row). Resolve `obj.name` via `kos_resolve_named_attr`; then invoke. Unbound class method (plain PyFunction/KosFunction from MRO): call `fn(obj, *args)`. Bound or instance-dict callable: call `callable(*args)`. Resolver return: -1 err, 0 = send self, 1 = omit self.
- **Invariants**: Stack has at least 1 + N elements (obj, N args). Resolved attribute must be callable or TypeError.
- **Design**: [design/archive/attr-lookup-cache.md](archive/attr-lookup-cache.md)

**Implementation notes** (koskari/lib/opcode/CALL_ATTR.c, eval/eval_call.c):

- **Stack arrangement**: `ptr = KosContext_StackTailPtr(ctx, n + 1)`; `ptr[0]` = obj, `ptr[1..n]` = positional args. Attr name from the EXTRA row. The whole window stays borrowed; `kos_call_invoke` owns its drop.

- **`kos_resolve_named_attr` return value**: -1 = error (exception set); 0 = send self (unbound function found on the type -- the call becomes `fn(obj, *args)`); 1 = omit self (bound method, instance dict, data descriptor result, etc. -- the call is `callable(*args)`). The resolution goes through the row's cache slot.

- **Invocation**: one call to `kos_call_invoke` with the resolved callable, `self_arg = obj` when send_self is 0 (the core prepends it through a borrowed-pointer buffer; self never lives on the caller's stack) and NULL otherwise, the borrowed argv window, and `KOS_CALL_ARGV_WINDOW` plus the tail bit. The core pushes a frame for a Koskari callable, or calls a Python callable and awaits an awaitable result in place.

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

### KOS_OP_CALL_ATTR_NAMES {#kos_op_call_attr_names}

- **Style**: EXTRA. Schema: ATTR (attr name), CONST (kwarg names tuple), COUNT (npos), NOARG.
- **Stack**: `[..., obj, pos_0, ..., pos_{N-1}, kwd_val_0, ..., kwd_val_{M-1}] → [..., result]`. Resolve `obj.name` via `kos_resolve_named_attr`; then call like CALL_NAMES. The kwarg names tuple and attr name are in the constant pool (EXTRA row), not on the stack.
- **Compiler form:** Emitted by `compile/call-attr` when the call has keyword arguments and no `*:` or `**:`.

---

### KOS_OP_CALL_FAST_ATTR {#kos_op_call_fast_attr}

- **Style**: EXTRA. Schema: FASTVAR (slot index for `obj`), ATTR (attr name), COUNT (npos), NOARG.
- **Stack**: `[..., arg_0, ..., arg_{N-1}] → [..., result]`. Same resolution and invocation rules as [`KOS_OP_CALL_ATTR`](#kos_op_call_attr), but `obj` is **not** on the stack: it is loaded from the current frame namespace via `KosNamespace_FastDeref` (slot `a0`). Requires a namespace that supports fast locals (`KosNamespace_SupportsFast`). When `send_self=0` (unbound function) `obj` is passed to `kos_call_invoke` as `self_arg` and the core prepends it; when `send_self=1` only the `N` args go.
- **Implementation** (koskari/lib/opcode/CALL_FAST_ATTR.c): `obj` is an owned deref from the fast slot (`kos_ns_fast_deref` increfs); the handler releases it after the core returns, on every path, same discipline as `KOS_OP_CALL_VAR_ATTR`.
- **Compiler form:** The `convert_fast` lowering of **CALL_VAR_ATTR**; never emitted by a special.

---

### KOS_OP_CALL_VAR_ATTR {#kos_op_call_var_attr}

- **Style**: EXTRA. Schema: VAR (name cpool index for `obj`), ATTR (attr name), COUNT (npos), NOARG.
- **Stack**: Same as `KOS_OP_CALL_FAST_ATTR`. `obj` is obtained with `frame_ns_deref(ctx, var_name)` (owned reference); the handler releases it with `Py_DECREF(obj)` on every exit path.
- **Implementation** (koskari/lib/opcode/CALL_VAR_ATTR.c): Otherwise identical control flow to `KOS_OP_CALL_FAST_ATTR` / `KOS_OP_CALL_ATTR` (resolve via `kos_resolve_named_attr`, then `kos_call_invoke` with `obj` as `self_arg` when `send_self=0`).
- **Compiler form:** Emitted by `compile/call-attr` when the receiver is a symbol that reads a variable and the call has only positional arguments. Lowers to **CALL_FAST_ATTR**.

---

### KOS_OP_CALL_FAST_ATTR_NAMES {#kos_op_call_fast_attr_names}

- **Style**: EXTRA. Schema: FASTVAR (slot index for obj), ATTR (attr name), CONST (kwarg names tuple), COUNT (npos).
- **Stack**: `[..., pos_0, ..., pos_{N-1}, kwd_val_0, ..., kwd_val_{M-1}] → [..., result]`. Obj is loaded from fast slot `a0` via `KosNamespace_FastDeref`; not on stack. One fewer element than CALL_ATTR_NAMES. Resolve and call like CALL_ATTR_NAMES. When `send_self=0` (unbound function) `obj` goes to `kos_call_invoke` as `self_arg`; when `send_self=1` only the args go.
- **Compiler form:** The `convert_fast` lowering of **CALL_VAR_ATTR_NAMES**; never emitted by a special.

---

### KOS_OP_CALL_VAR_ATTR_NAMES {#kos_op_call_var_attr_names}

- **Style**: EXTRA. Schema: VAR (name/cpool for obj), ATTR (attr name), CONST (kwarg names tuple), COUNT (npos).
- **Stack**: Same as CALL_FAST_ATTR_NAMES. Obj is loaded from frame namespace by name (`frame_ns_deref`) instead of fast slot.
- **Compiler form:** Emitted by `compile/call-attr` for a variable receiver with keyword arguments and no `*:` or `**:`. Lowers to **CALL_FAST_ATTR_NAMES**.

---

### KOS_OP_CALL_ATTR_STAR {#kos_op_call_attr_star}

- **Style**: EXTRA. Schema: ATTR (attr name), COUNT (opa bitflags: 1 = has *args, 2 = has **kwargs), NOARG, NOARG.
- **Stack**: `obj.name(*args, **kwargs)`. Stack layout per opa flags (bottom … TOS): opa=0 → `[obj]`; opa=1 → `[obj, pos]` (pos = *args); opa=2 → `[obj, kwd]` (kwd = **kwargs); opa=3 → `[obj, pos, kwd]`. Pop in reverse order (kwd then pos then obj when both star forms are present). `pos` is ensured to be a tuple (coerced via PySequence_Tuple if needed). `kos_op_call_star` resolves obj.name through the row's cache, prepends `obj` to the positional tuple when the resolution asks for it, and calls through the shared core.
- **Invariants**: attr name in constant pool. opa bits: 1 = has *args, 2 = has **kwargs.

---

### KOS_OP_CALL_VAR_ATTR_STAR {#kos_op_call_var_attr_star}

- **Style**: EXTRA. Schema: VAR (name cpool index for `obj`), ATTR (attr name), COUNT (opa bitflags: 1 = has *args, 2 = has **kwargs), NOARG.
- **Stack**: Same semantics as [`KOS_OP_CALL_ATTR_STAR`](#kos_op_call_attr_star), but `obj` is **not** on the stack; it is loaded with `frame_ns_deref(ctx, var_name)` (owned reference, stolen by `kos_op_call_star`). Stack layout per opa (bottom … TOS): opa=0 → `[]`; opa=1 → `[pos]`; opa=2 → `[kwd]`; opa=3 → `[pos, kwd]` with `kwd` at TOS (same relative order as CALL_ATTR_STAR without the receiver). Pop order and `opa` handling match [`KOS_OP_CALL_VAR_STAR`](#kos_op_call_var_star) combined with CALL_ATTR_STAR; `kos_op_call_star` resolves, prepends `obj` to the tuple args when the resolution asks for it, and calls through the shared core.
- **Invariants**: `var_name` and attr name in constant pool; same opa bit meanings as CALL_ATTR_STAR.
- **Implementation** (koskari/lib/opcode/CALL_VAR_ATTR_STAR.c): Parallel to CALL_ATTR_STAR with VAR receiver semantics (see [`KOS_OP_CALL_VAR_ATTR`](#kos_op_call_var_attr)).
- **Compiler form:** Emitted by `compile/call-attr` for a variable receiver with `*:` or `**:`. Lowers to **CALL_FAST_ATTR_STAR**.

---

### KOS_OP_CALL_FAST_ATTR_STAR {#kos_op_call_fast_attr_star}

- **Style**: EXTRA. Schema: FASTVAR (slot index for `obj`), ATTR (attr name), COUNT (opa bitflags), NOARG.
- **Stack**: Same stack layout and `opa` behavior as [`KOS_OP_CALL_VAR_ATTR_STAR`](#kos_op_call_var_attr_star). `obj` comes from `KosNamespace_FastDeref(ns, slot)` (slot `a0`); requires `KosNamespace_SupportsFast`. Pop/resolve/call semantics match CALL_ATTR_STAR. `obj` is an owned deref stolen by `kos_op_call_star`, which releases it; the handler holds nothing after the call.
- **Invariants**: Same opa and tuple/`pos` rules as CALL_ATTR_STAR; fast slot must be in range and bound.
- **Implementation** (koskari/lib/opcode/CALL_FAST_ATTR_STAR.c): Parallel to CALL_FAST_ATTR / CALL_ATTR_STAR.
- **Compiler form:** The `convert_fast` lowering of **CALL_VAR_ATTR_STAR**; never emitted by a special.

---

### KOS_OP_TAIL_CALL

- **Style**: COUNT. Arg = number of positional arguments.
- **Stack**: Same as CALL: `[..., callable, arg_1, ..., arg_n]`. Shares the CALL handler (`op_TAIL_CALL` is `op_CALL`); the opcode adds `KOS_CALL_TAIL`, which `kos_call_invoke` turns into a reset of the current frame instead of a push for a Koskari callable. For a Python callable the core calls it and the return settles a pending wind thunk.
- **Execution model**: When the callable is a KosExpression or KosFunction, the VM replaces the current frame (tail install); an exact non-yielding KosFunction takes the bind-before-reset subpath. For all other callable types the core calls the callable and the result is pushed, with an awaitable result awaited in place.
- **Invariants**: Stack has at least 1 + arg elements. KosExpression allows no arguments (arg must be 0); otherwise TypeError.
- **Compiler form:** Produced by the processor's `promote_tail_calls` pass relabeling a tail-position CALL in function-body code; never emitted by a special.

**Example:** Tail call with 0 args (replace frame with callee). Pool: 0 = fn.

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 0     | CONST    | 0   | 0 (fn)   | stack: [..., callable] |
| 1     | TAIL_CALL| 0   | 0        | replace frame; run callee. stack: [...,] (new frame) |

---

### KOS_OP_TAIL_CALL_NAMES {#kos_op_tail_call_names}

- **Style**: COUNT. Arg = N (number of positional arguments).
- **Stack**: Same layout as CALL_NAMES: `[..., callable, pos_0, ..., pos_{N-1}, val_0, ..., val_{M-1}, names_tuple]`. Shares the CALL_NAMES handler with the tail bit set; tail semantics as [`KOS_OP_TAIL_CALL`](#kos_op_tail_call).

---

### KOS_OP_TAIL_CALL_STAR {#kos_op_tail_call_star}

- **Style**: STAR. Arg = bitflags (1 = has *args, 2 = has **kwargs).
- **Stack**: Same layout as CALL_STAR: `[..., callable, pos, kwd]`. Shares the CALL_STAR handler; `kos_op_call_star` runs with the tail bit set. Same replace-frame semantics as TAIL_CALL.

---

### KOS_OP_TAIL_CALL_ATTR {#kos_op_tail_call_attr}

- **Style**: EXTRA. Schema: ATTR (attr name), COUNT (npos), NOARG, NOARG. One cache slot (attr resolution).
- **Stack**: Same layout as CALL_ATTR: `[..., obj, arg_0, ..., arg_{N-1}]`. Resolve `obj.name` via `kos_resolve_named_attr`, then call in tail position: the caller's frame is reused rather than a new frame pushed (`KOS_CALL_TAIL` through `kos_call_invoke`; an exact non-yielding KosFunction takes the bind-before-reset subpath, every other callable class takes the core's general tail path).
- **Compiler form:** Produced by the processor's `promote_tail_calls` pass relabeling a tail-position CALL_ATTR in function-body code; never emitted by a special.

---

### KOS_OP_TAIL_CALL_ATTR_NAMES {#kos_op_tail_call_attr_names}

- **Style**: EXTRA. Schema: ATTR (attr name), CONST (kwarg names tuple), COUNT (npos), NOARG. One cache slot.
- **Stack**: Same layout as CALL_ATTR_NAMES. Tail semantics as [`KOS_OP_TAIL_CALL_ATTR`](#kos_op_tail_call_attr).
- **Compiler form:** Promotion relabel of a tail-position CALL_ATTR_NAMES; never emitted by a special.

---

### KOS_OP_TAIL_CALL_ATTR_STAR {#kos_op_tail_call_attr_star}

- **Style**: EXTRA. Schema: ATTR (attr name), COUNT (opa bitflags: 1 = has *args, 2 = has **kwargs), NOARG, NOARG. One cache slot.
- **Stack**: Same layout and pop order as CALL_ATTR_STAR; call via `KosContext_CallArgs` with the tail flag set.
- **Compiler form:** Promotion relabel of a tail-position CALL_ATTR_STAR; never emitted by a special.

---

### KOS_OP_TAIL_CALL_VAR {#kos_op_tail_call_var}

- **Style**: EXTRA. Schema: VAR (name cpool idx), COUNT (npos), NOARG, NOARG.
- **Stack**: Same layout as CALL_VAR: `[..., pos_0, ..., pos_{N-1}]`; the callable is loaded from the frame namespace by name, not on the stack. Tail semantics as [`KOS_OP_TAIL_CALL_ATTR`](#kos_op_tail_call_attr).
- **Compiler form:** Promotion relabel of a tail-position CALL_VAR; convert_fast lowers it to TAIL_CALL_FAST when the name has a fast slot.

---

### KOS_OP_TAIL_CALL_VAR_NAMES {#kos_op_tail_call_var_names}

- **Style**: EXTRA. Schema: VAR (name), CONST (kwarg names tuple), COUNT (npos).
- **Stack**: Same layout as CALL_VAR_NAMES. Tail semantics as [`KOS_OP_TAIL_CALL_ATTR`](#kos_op_tail_call_attr).
- **Compiler form:** Promotion relabel of a tail-position CALL_VAR_NAMES; lowers to TAIL_CALL_FAST_NAMES.

---

### KOS_OP_TAIL_CALL_VAR_STAR {#kos_op_tail_call_var_star}

- **Style**: EXTRA. Schema: VAR (name), STAR (bitflags), NOARG, NOARG.
- **Stack**: Same layout and pop order as CALL_VAR_STAR; call via `KosContext_CallArgs` with the tail flag set.
- **Compiler form:** Promotion relabel of a tail-position CALL_VAR_STAR; lowers to TAIL_CALL_FAST_STAR.

---

### KOS_OP_TAIL_CALL_VAR_ATTR {#kos_op_tail_call_var_attr}

- **Style**: EXTRA. Schema: VAR (name for obj), ATTR (attr name), COUNT (npos), NOARG. One cache slot.
- **Stack**: Same layout as CALL_VAR_ATTR: args only; obj is loaded from the frame namespace by name (owned deref, released on every path). Tail semantics as [`KOS_OP_TAIL_CALL_ATTR`](#kos_op_tail_call_attr).
- **Compiler form:** Promotion relabel of a tail-position CALL_VAR_ATTR; lowers to TAIL_CALL_FAST_ATTR.

---

### KOS_OP_TAIL_CALL_VAR_ATTR_NAMES {#kos_op_tail_call_var_attr_names}

- **Style**: EXTRA. Schema: VAR (name for obj), ATTR (attr name), CONST (kwarg names tuple), COUNT (npos). One cache slot.
- **Stack**: Same layout as CALL_VAR_ATTR_NAMES. Tail semantics as [`KOS_OP_TAIL_CALL_ATTR`](#kos_op_tail_call_attr).
- **Compiler form:** Promotion relabel of a tail-position CALL_VAR_ATTR_NAMES; lowers to TAIL_CALL_FAST_ATTR_NAMES.

---

### KOS_OP_TAIL_CALL_VAR_ATTR_STAR {#kos_op_tail_call_var_attr_star}

- **Style**: EXTRA. Schema: VAR (name for obj), ATTR (attr name), COUNT (opa bitflags), NOARG. One cache slot.
- **Stack**: Same layout and pop order as CALL_VAR_ATTR_STAR; call via `KosContext_CallArgs` with the tail flag set.
- **Compiler form:** Promotion relabel of a tail-position CALL_VAR_ATTR_STAR; lowers to TAIL_CALL_FAST_ATTR_STAR.

---

### KOS_OP_TAIL_CALL_FAST {#kos_op_tail_call_fast}

- **Style**: EXTRA. Schema: FASTVAR (slot index), COUNT (npos), NOARG, NOARG.
- **Stack**: Same layout as CALL_FAST; the callable is loaded from the fast local slot (owned deref). Tail semantics as [`KOS_OP_TAIL_CALL_ATTR`](#kos_op_tail_call_attr).
- **Compiler form:** Reached through promotion of CALL_VAR plus the fast lowering, or promotion of an already-lowered CALL_FAST; never emitted by a special.

---

### KOS_OP_TAIL_CALL_FAST_NAMES {#kos_op_tail_call_fast_names}

- **Style**: EXTRA. Schema: FASTVAR (slot), CONST (kwarg names tuple), COUNT (npos).
- **Stack**: Same layout as CALL_FAST_NAMES. Tail semantics as [`KOS_OP_TAIL_CALL_ATTR`](#kos_op_tail_call_attr).
- **Compiler form:** Fast tier of TAIL_CALL_VAR_NAMES; see [`KOS_OP_TAIL_CALL_FAST`](#kos_op_tail_call_fast).

---

### KOS_OP_TAIL_CALL_FAST_STAR {#kos_op_tail_call_fast_star}

- **Style**: EXTRA. Schema: FASTVAR (slot), STAR (bitflags), NOARG, NOARG.
- **Stack**: Same layout and pop order as CALL_FAST_STAR; call via `KosContext_CallArgs` with the tail flag set.
- **Compiler form:** Fast tier of TAIL_CALL_VAR_STAR; see [`KOS_OP_TAIL_CALL_FAST`](#kos_op_tail_call_fast).

---

### KOS_OP_TAIL_CALL_FAST_ATTR {#kos_op_tail_call_fast_attr}

- **Style**: EXTRA. Schema: FASTVAR (slot for obj), ATTR (attr name), COUNT (npos), NOARG. One cache slot.
- **Stack**: Same layout as CALL_FAST_ATTR: args only; obj is loaded from the fast local slot (owned deref, released on every path). Tail semantics as [`KOS_OP_TAIL_CALL_ATTR`](#kos_op_tail_call_attr).
- **Compiler form:** Fast tier of TAIL_CALL_VAR_ATTR; see [`KOS_OP_TAIL_CALL_FAST`](#kos_op_tail_call_fast).

---

### KOS_OP_TAIL_CALL_FAST_ATTR_NAMES {#kos_op_tail_call_fast_attr_names}

- **Style**: EXTRA. Schema: FASTVAR (slot for obj), ATTR (attr name), CONST (kwarg names tuple), COUNT (npos). One cache slot.
- **Stack**: Same layout as CALL_FAST_ATTR_NAMES. Tail semantics as [`KOS_OP_TAIL_CALL_ATTR`](#kos_op_tail_call_attr).
- **Compiler form:** Fast tier of TAIL_CALL_VAR_ATTR_NAMES; see [`KOS_OP_TAIL_CALL_FAST`](#kos_op_tail_call_fast).

---

### KOS_OP_TAIL_CALL_FAST_ATTR_STAR {#kos_op_tail_call_fast_attr_star}

- **Style**: EXTRA. Schema: FASTVAR (slot for obj), ATTR (attr name), COUNT (opa bitflags), NOARG. One cache slot.
- **Stack**: Same layout and pop order as CALL_FAST_ATTR_STAR; call via `KosContext_CallArgs` with the tail flag set.
- **Compiler form:** Fast tier of TAIL_CALL_VAR_ATTR_STAR; see [`KOS_OP_TAIL_CALL_FAST`](#kos_op_tail_call_fast).

---


## 18. Block control

### KOS_OP_PUSH_FOREACH

- **Style**: NOARG. Argument unused; break target is derived at runtime from the extent table (`pop_pc + 1`).
- **Stack**: Pops iterable (TOS); creates iterator; pushes the FOREACH node, which owns its segment. Region loop result starts unset. ITER_NEXT uses the node's iterator. Break target = `extent.pop_pc + 1` (instruction after the matching POP_FOREACH).
- **Pushes:** KOS_DYN_FOREACH (static extent KOS_EXT_FOREACH; node holds iterator and loop state).
- **Invariants**: A KOS_EXT_FOREACH extent must exist at the PUSH_FOREACH PC; otherwise RuntimeError. Extent kind KOS_EXT_FOREACH.
- **Compiler form:** Pre-compile arg = 0 (unused). Break target is not stored in the instruction; it is derived at runtime via `kos_extent_find(KOS_EXT_FOREACH)` → `ext->pop_pc + 1`.

**Example:** for x in [1,2,3]: body. Stack at entry: [..., iterable]. Pool: 0 = [1,2,3].

| index | opcode       | arg | arg view | comment |
|------:|--------------|-----|----------|---------|
| 0     | CONST        | 0   | 0 ([1,2,3]) | stack: [..., iterable] |
| 1     | PUSH_FOREACH | 0   | 0        | pop iterable; push FOREACH region; break_pc = pop_pc+1 from extent table |
| 2     | ITER_NEXT    | 6   | 6 (-> 6) | next from region; else → 6. stack: [..., x] |
| 3     | POP          | 1   | 1        | body: consume x |
| 4     | JUMP         | 2   | 2 (-> 2) | loop back |
| 5     | POP_FOREACH  | 0   | 0        | push loop result (None if unset) |
| 6     | LABEL        | —   | —        | break / exhausted (= pop_pc+1) |

---

### 18.1 Context managers (with / dyn-with)

Full design: [interpreter/with-dyn-with.md](interpreter/with-dyn-with.md). Summary below.

### KOS_OP_PUSH_WITH {#kos_op_push_with}

- **Style**: CONST. Arg = constant pool index of binding name (symbol/string) or sentinel for no binding.
- **Stack before:** `[..., mgr]`. **Stack after:** body runs in a fresh operand segment.
- **Behaviour:** Pop `mgr`; resolve and cache enter/exit; call enter (auto-await if needed); on success bind enter result if name given; push new segment; look up WITH extent at current PC, compute `resume_pc = ext->pop_pc + 1`; push `KOS_DYN_WITH` with `resume_pc` stored on the node. On enter failure, no node or block is installed; exception propagates.
- **Pushes:** KOS_DYN_WITH (single-shot: exits once on unwind or transfer, never re-entered). Node stores `resume_pc` (instruction after POP_WITH) for use when exit swallows an exception.
- **Invariants:** Node owns one operand segment; stack restoration is structural (segment pop), not height-mark. A WITH extent must exist at the PUSH_WITH PC; otherwise RuntimeError.

### KOS_OP_POP_WITH {#kos_op_pop_with}

- **Style**: NOARG (or COUNT per existing opcodes.incl; design assumes no arg).
- **Behaviour:** Store the body value on the node as the pop's value; call exit (no-exception form); auto-await if needed; pop the owned segment; pop dyn node; push the stored body value. The pop is `kos_dyn_unlink_top`: the node's leave calls exit; when exit returns an awaitable the leave answers AWAIT and the node stays in `KOS_WITH_PHASE_PENDING_EXIT_AWAIT`; on resume, revive settles it (`kos_dyn_settle_top`) and the unlink finishes by pushing the body value (exit return is never left on stack). On exception unwind (via no-region path or dyn_pop_top_with): call exit(exc_info); if swallow then clear exception, pop block, pop node, push `None`, set frame PC to `KosDynWith_RESUME_PC(node)` (the instruction after POP_WITH), resume; else pop block, pop node, re-raise.
- **Invariants:** Top of chain must be KOS_DYN_WITH. With-expression value is the body's value, or `None` when exit swallowed an exception; exit return is used only for swallow logic and is never the value seen by RETURN.
- **Covered by:** `tests/lib/opcode/test_with.py`, `tests/compile/specials/test_with.py` (sync and async).

### KOS_OP_PUSH_DYN_WITH {#kos_op_push_dyn_with}

- **Style**: CONST. Arg = constant pool index of binding name or sentinel for no binding (same as PUSH_WITH).
- **Stack / behaviour:** Same as PUSH_WITH except pushes KOS_DYN_DYN_WITH. The node's shot is `KOS_DYN_WITH_SHOT_MULTI`: a transfer goal leaves through it and re-enters it when it lands the extent (a landing re-enters a **`KOS_DYN_CAP_CONTROL_HOOK`** node unless it is a single-shot WITH).
- **Pushes:** KOS_DYN_DYN_WITH.

### KOS_OP_POP_DYN_WITH {#kos_op_pop_dyn_with}

- **Style**: NOARG.
- **Behaviour:** Same as POP_WITH for normal and exception unwind. Pops KOS_DYN_DYN_WITH.

---
### KOS_OP_PUSH_COLLECT

- **Style**: CONST. Arg = constant pool index of NamespaceSpec.
- **Stack**: Does not consume the stack. Loads NamespaceSpec from constant pool at
  arg, creates a CollectorNamespace parented to the current namespace, and
  pushes KOS_DYN_COLLECT owning it (`KOS_DYN_CAP_OWNS_NS`). The live namespace
  follows the chain top, so the body runs in the collector while the node is
  on the chain; nothing is saved or restored. The node's operand segment
  attaches at push; expression results (e.g. None from compile/var) stay in
  it and are discarded on POP_COLLECT, which pops the node and pushes the
  collected namespace.
- **Pushes:** KOS_DYN_COLLECT (static extent KOS_EXT_COLLECT) owning the
  collector namespace, with its segment for the body.
- **Invariants**: Extent kind KOS_EXT_COLLECT. Frame namespace must be KosNamespace.

---

### KOS_OP_POP_COLLECT

- **Style**: NOARG. Argument unused.
- **Stack**: Verifies dyn_top is KOS_DYN_COLLECT. Restores frame ns, drops the stack
  block (discarding body expression results), pops the dyn node, **pushes the
  collected namespace** onto the value stack. `[..., pos] → [..., pos, ns]`.
- **Invariants**: Top of chain must be KOS_DYN_COLLECT.

---

### KOS_OP_BUILD_CLASS

- **Style**: NOARG. Argument unused.
- **Stack**: Expects `[..., pos, ns]` with TOS = ns, TOS-1 = pos. Pops ns, pos.
  pos = (name, bases) or (name, bases, metaclass). Builds the class via
  `kos_build_class` (namespace materialization, optional explicit metaclass,
  derived/composite metaclass, `__prepare__`, merge, metaclass call); pushes
  resulting class.
- **Compiler form:** Build pos with BUILD_ARGS; use with_collector_namespace for
  PUSH_COLLECT/body/POP_COLLECT; then BUILD_CLASS. The class build path is
  `kos_build_class` in `object.c`; the KosType design it replaced is archived at
  design/archive/OBJECT-MODEL.md.

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
- **Compiler form:** Pre-compile arg = 0 (unused). Break target is not stored in the instruction; it is derived at runtime via `kos_extent_find(KOS_EXT_LOOP)` → `ext->pop_pc + 1`.

**Example:** while loop. Break target derived from extent table (pop_pc+1 = 6).

| index | opcode    | arg | arg view | comment |
|------:|-----------|-----|----------|---------|
| 0     | PUSH_LOOP | 0   | 0        | push LOOP region; break_pc = pop_pc+1 from extent table; continue_pc = 1 |
| 1     | CONST     | 0   | 0 (False)| condition |
| 2     | JUMP_FALSE| 5   | 5 (-> 5) | exit loop if false; jumps to POP_LOOP |
| 3     | …         | …   | …        | body |
| 4     | JUMP      | 1   | 1 (-> 1) | continue (loop head) |
| 5     | POP_LOOP  | 0   | 0        | |
| 6     | LABEL     | —   | —        | break target (= pop_pc+1) |

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
| 1     | …      | …   | …        | body |
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
| 1     | …      | …   | …        | try body |
| 2     | POP_TRY | 0   | 0        | normal exit: pop TRY |
| 3     | JUMP   | 5   | 5 (-> 5)  | skip handler |
| 4     | LABEL  | —   | —        | except handler (except_pc) |
| 5     | LABEL  | —   | —        | exit |

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
| 1     | …           | …   | …        | protected body |
| 2     | POP_FINALLY | 0   | 0        | normal exit |
| 3     | LABEL       | —   | —        | finally block |
| 4     | END_FINALLY | 0   | 0        | (conceptual) |

---

### KOS_OP_CLEAR_FINALLY {#kos_op_clear_finally}

- **Style**: NOARG.
- **Stack**: No stack effect. Clears FINALLY region state on non-local exit; does not pop the region (POP_FINALLY does at target). Does not touch the segment (only disables the region); the segment is dropped by POP_FINALLY or unwind.
- **Example:** Exception or unwind path before transferring to a finally handler.

---

### KOS_OP_CLEAR_LOOP {#kos_op_clear_loop}

- **Style**: NOARG.
- **Stack**: Clears the loop segment. Disables the top LOOP region (clears its **`KOS_DYN_CAP_LOOP_REGION`**): a BREAK or CONTINUE issued while it is disabled passes through it to the enclosing loop, and POP_LOOP takes the loop result from the region's stack. Emitted on normal exit from a while-loop body ahead of an else clause.
- **Invariants**: Top of chain must be KOS_DYN_LOOP. Use CLEAR_FOREACH for FOREACH extents.
- **Implementation**: `koskari/lib/opcode/CLEAR_LOOP.c`

---

### KOS_OP_CLEAR_FOREACH {#kos_op_clear_foreach}

- **Style**: NOARG.
- **Stack**: Clears the loop segment. Disables the top FOREACH region (clears its **`KOS_DYN_CAP_LOOP_REGION`**): a BREAK or CONTINUE issued while it is disabled passes through it to the enclosing loop, and POP_FOREACH takes the loop result from the region's stack. Emitted when a for-each iterator is exhausted naturally, ahead of an else clause.
- **Invariants**: Top of chain must be KOS_DYN_FOREACH. Use CLEAR_LOOP for LOOP extents.
- **Implementation**: `koskari/lib/opcode/CLEAR_FOREACH.c`

---

### KOS_OP_CLEAR_TRY {#kos_op_clear_try}

- **Style**: NOARG.
- **Stack**: No stack effect. Clears TRY region state on non-local exit; does not touch the segment; POP_TRY drops the segment at the target.
- **Invariants**: Used with structured exception and non-local control flow.

---

### KOS_OP_POP_TRY {#kos_op_pop_try}

- **Style**: NOARG. Argument unused (or 0).
- **Stack**: Promote one value: pop TOS from the try segment, pop the region (dropping its segment), and push the value onto the revealed segment. The segment must hold exactly one value. Clear exception state before pop.
- **Invariants**: Top of chain must be KOS_DYN_TRY.

---

### KOS_OP_POP_FINALLY {#kos_op_pop_finally}

- **Style**: NOARG.
- **Stack**: Same structural behaviour as POP_TRY for the finally segment: promote one value, drop the segment, pop the region.
- **Invariants**: Top of chain must be KOS_DYN_FINALLY.

---

### KOS_OP_POP_LOOP {#kos_op_pop_loop}

- **Style**: NOARG.
- **Stack**: Pop LOOP region and drop its segment; push the loop result. An enabled region delivers the result stored on the region; a region CLEAR_LOOP disabled ran its else clause, and the clause's value, the top of the region's stack, is the result. A disabled region with an empty stack is a pop underrun.
- **Invariants**: Top of chain must be KOS_DYN_LOOP (paired with PUSH_LOOP).
- **Implementation**: `koskari/lib/opcode/POP_LOOP.c`

---

### KOS_OP_POP_FOREACH {#kos_op_pop_foreach}

- **Style**: NOARG.
- **Stack**: Pop FOREACH region and drop its segment; push the loop result. An enabled region delivers the result stored on the region, or `None` when no CONTINUE set one; a region CLEAR_FOREACH disabled ran its else clause, and the clause's value, the top of the region's stack, is the result. A disabled region with an empty stack is a pop underrun.
- **Invariants**: Top of chain must be KOS_DYN_FOREACH (paired with PUSH_FOREACH).
- **Implementation**: `koskari/lib/opcode/POP_FOREACH.c`

---

### KOS_OP_POP_NS {#kos_op_pop_ns}

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

- **Comparisons and predicates**: OBJECT_COMPARE (CMPCOUNT) — stack `[..., a1, a2, ... aN] → [..., result]`, `N >= 2`, selector-keyed (KosCompare packs with the count); the fused OBJECT_COMPARE_VAR / OBJECT_COMPARE_FAST carry the ternary bounds idiom with no stack inputs.
- **Numeric**: OBJECT_OPERAND (OPCOUNT) — one opcode for every arithmetic and bitwise operator; the arg packs the KosOperand selector with the operand count, and the site elects a specialized body. At a count of two the compress pipeline fuses a CONST or VAR feeder on either side into OBJECT_OPERAND_CONST, VAR_OPERAND_CONST, OBJECT_OPERAND_VAR and VAR_OPERAND_VAR; the fast pass lowers the named ones to FAST_OPERAND_CONST, OBJECT_OPERAND_FAST and FAST_OPERAND_FAST when the name has a slot.
- **Numeric, in-place**: OBJECT_INPLACE (OPCOUNT) — stack `[..., a1, a2, ... aN] → [..., result]`, `N >= 2`, reducing left-to-right via the `PyNumber_InPlace*` API (POWER is strictly binary); used by the inplace-item and inplace-attr special forms.
- **General**: OBJECT_ITER, OBJECT_LEN, OBJECT_NOT, OBJECT_REPR, OBJECT_STR, OBJECT_FORMAT, OBJECT_FORMAT_CONST, OBJECT_TYPE, OBJECT_DIR, OBJECT_VARS, OBJECT_TRUTH, OBJECT_DIVMOD, OBJECT_BIT_INVERT.

Each `OBJECT_*` opcode in `opcodes.incl` has its own subsection below.

### KOS_OP_OBJECT_COMPARE {#kos_op_object_compare}

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
  by `normalize_opcount`. When immediately followed by `JUMP_FALSE`,
  `compress` fuses the pair into [CHECK_COMPARE](#kos_op_check_compare).
- **Implementation**: `koskari/lib/opcode/OBJECT_COMPARE.c`; core
  dispatch `kos_compare_reduce` in `operator.c`.
- **Covered by**: `tests/lib/opcode/test_compare.py`,
  `tests/lib/opcode/test_object_comparison.py`

### KOS_OP_OBJECT_COMPARE_VAR {#kos_op_object_compare_var}

- **Style**: EXTRA (CMP, CONST, VAR, CONST). Row = bare selector, low
  bound (pool), middle operand, high bound (pool). Schema-static slots:
  no source bits, the shape is the opcode.
- **Stack**: `[...] → [..., result]`. The ternary bounds idiom
  (`const < x < const`): chain-compare the three operands under the
  selector and push the chain result (for JUMP_FALSE / JUMP_TRUE or
  value use).
- **Invariants**: Selector is a valid KosCompare ordinal (richcompare
  chain semantics; extended selectors are never emitted at arity 3).
- **Compiler form:** Produced by `ByteCodeProcessor.fuse_feeders` when the exact CONST / VAR / CONST feeder shape meets an arity-3 comparison. In value position the OBJECT_COMPARE quartet collapses to this row alone; in branch position the arity-3 CHECK_COMPARE cannot also carry its label (five fields), so it splits into this row plus `JUMP_FALSE`. Lowers to OBJECT_COMPARE_FAST through the generic schema-driven `convert_fast` path.
- **Implementation**: `koskari/lib/opcode/OBJECT_COMPARE_VAR.c`
- **Covered by**: `tests/compile/test_fuse_feeders.py`,
  `tests/lib/opcode/test_compare.py`,
  `tests/reproducers/test_compare_fusion.py`

**Example:** `(< 0 x 100)` in value position -- one dispatch.

| index | opcode              | arg | arg view                     | comment |
|------:|---------------------|-----|------------------------------|---------|
| 0     | OBJECT_COMPARE_VAR | 0 | (LT, c#0 (0), v#1 ('x'), c#2 (100)) | stack: [..., bool] |

---

### KOS_OP_OBJECT_COMPARE_FAST {#kos_op_object_compare_fast}

- **Style**: EXTRA (CMP, CONST, FASTVAR, CONST). Row = bare selector, low
  bound (pool), middle operand, high bound (pool). Schema-static slots:
  no source bits, the shape is the opcode.
- **Stack**: `[...] → [..., result]`. The ternary bounds idiom
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

### KOS_OP_OBJECT_OPERAND {#kos_op_object_operand}

- **Style**: OPCOUNT. Arg packs a KosOperand selector (high byte) with the
  operand count (low byte).
- **Stack**: `[..., a1, ... aN] → [..., result]`. Pop the N operands, apply
  the selected operator (reducing left-to-right for N > 2), push the result.
- **Semantics**: The hottest opcode by dispatch count and by time. The
  handler is a prologue only: it tail-calls the body the site's slot elected
  (`eval/eval_electors.c`, int and float fast paths with their own guards);
  an empty slot or an ungated site runs the electing function once, which
  settles the slot. See [interpreter/electors.md](interpreter/electors.md).
- **Compiler form:** Emitted by the operator specials in
  `compile/specials/object.py`; the selector is a KosOperand name.
- **Implementation**: `koskari/lib/opcode/OBJECT_OPERAND.c`

### KOS_OP_OBJECT_OPERAND_CONST {#kos_op_object_operand_const}

- **Style**: EXTRA (OPERAND, CONST, NOARG, NOARG). Row a0 = the bare
  operator selector, a1 = the right operand, a pool constant.
- **Stack**: `[..., left] → [..., result]`. Apply the operator to the stack
  top and the constant and replace the top with the result.
- **Semantics**: Elected like OBJECT_OPERAND. The handler is a prologue
  that tail-calls the site's elected body, else the electing function;
  the int and float bodies carry their own guards and hand a miss to the
  generic body (`eval/eval_electors.c`).
- **Compiler form:** Folded by the compress pipeline from `CONST k,
  OBJECT_OPERAND` at a count of two.
- **Implementation**: `koskari/lib/opcode/OBJECT_OPERAND_CONST.c`

### KOS_OP_VAR_OPERAND_CONST {#kos_op_var_operand_const}

- **Style**: EXTRA (OPERAND, VAR, CONST, NOARG). Row a0 = the bare operator
  selector, a1 = the left operand's name, a2 = the right operand, a pool
  constant.
- **Stack**: `[...] → [..., result]`. Resolve the name through the frame
  namespace, apply the operator to the value and the constant, and push
  the result.
- **Semantics**: No cache slot; the handler syncs the pc, dereferences the
  name and reduces through the shared two-operand path. Lowered to
  FAST_OPERAND_CONST by the fast pass when the name has a slot.
- **Compiler form:** Folded by the compress pipeline from `VAR x, CONST k,
  OBJECT_OPERAND` at a count of two, ahead of the one-feeder rules so a
  site with both feeders takes the row naming both.
- **Implementation**: `koskari/lib/opcode/VAR_OPERAND_CONST.c`

### KOS_OP_FAST_OPERAND_CONST {#kos_op_fast_operand_const}

- **Style**: EXTRA (OPERAND, FASTVAR, CONST, NOARG). Row a0 = the bare
  operator selector, a1 = the left operand's fast slot, a2 = the right
  operand, a pool constant.
- **Stack**: `[...] → [..., result]`. Apply the operator to the slot's value
  and the constant and push the result; the slot is untouched.
- **Semantics**: Elected like OBJECT_OPERAND, with the same prologue and
  body set as OBJECT_OPERAND_CONST.
- **Compiler form:** Lowered from VAR_OPERAND_CONST by the fast pass.
- **Implementation**: `koskari/lib/opcode/FAST_OPERAND_CONST.c`

### KOS_OP_OBJECT_OPERAND_VAR {#kos_op_object_operand_var}

- **Style**: EXTRA (OPERAND, VAR, NOARG, NOARG). Row a0 = the bare operator
  selector, a1 = the right operand's name.
- **Stack**: `[..., left] → [..., result]`. Resolve the name through the
  frame namespace, apply the operator to the stack top and the resolved
  value, and replace the top with the result.
- **Semantics**: No cache slot; the handler syncs the pc, dereferences the
  name and reduces through the shared two-operand path. Lowered to
  OBJECT_OPERAND_FAST by the fast pass when the name has a slot.
- **Compiler form:** Folded by the compress pipeline from `VAR y,
  OBJECT_OPERAND` at a count of two, after the CONST rules so a site with
  a constant keeps its CONST opcode.
- **Implementation**: `koskari/lib/opcode/OBJECT_OPERAND_VAR.c`

### KOS_OP_OBJECT_OPERAND_FAST {#kos_op_object_operand_fast}

- **Style**: EXTRA (OPERAND, FASTVAR, NOARG, NOARG). Row a0 = the bare
  operator selector, a1 = the right operand's fast slot.
- **Stack**: `[..., left] → [..., result]`. Apply the operator to the stack
  top and the slot's value and replace the top with the result; the slot
  is untouched.
- **Semantics**: Elected like OBJECT_OPERAND. The handler is a prologue
  that tail-calls the site's elected body, else the electing function;
  the int and float bodies carry their own guards and hand a miss to the
  generic body (`eval/eval_electors.c`).
- **Compiler form:** Lowered from OBJECT_OPERAND_VAR by the fast pass.
- **Implementation**: `koskari/lib/opcode/OBJECT_OPERAND_FAST.c`

### KOS_OP_VAR_OPERAND_VAR {#kos_op_var_operand_var}

- **Style**: EXTRA (OPERAND, VAR, VAR, NOARG). Row a0 = the bare operator
  selector, a1 = the left name, a2 = the right name.
- **Stack**: `[...] → [..., result]`. Resolve both names through the frame
  namespace, left first, apply the operator and push the result.
- **Semantics**: No cache slot; the handler syncs the pc, dereferences
  both names and reduces through the shared two-operand path. Lowered to
  FAST_OPERAND_FAST by the fast pass when both names have slots.
- **Compiler form:** Folded by the compress pipeline from `VAR x, VAR y,
  OBJECT_OPERAND` at a count of two, after the CONST rules and ahead of
  the one-name rule so both names ride the row. A site of three or more
  names stays OBJECT_OPERAND behind a packed load.
- **Implementation**: `koskari/lib/opcode/VAR_OPERAND_VAR.c`

### KOS_OP_FAST_OPERAND_FAST {#kos_op_fast_operand_fast}

- **Style**: EXTRA (OPERAND, FASTVAR, FASTVAR, NOARG). Row a0 = the bare
  operator selector, a1 = the left fast slot, a2 = the right fast slot.
- **Stack**: `[...] → [..., result]`. Apply the operator to the two slots'
  values and push the result; both slots are untouched.
- **Semantics**: Elected like OBJECT_OPERAND, with the same prologue and
  body set as OBJECT_OPERAND_FAST.
- **Compiler form:** Lowered from VAR_OPERAND_VAR by the fast pass.
- **Implementation**: `koskari/lib/opcode/FAST_OPERAND_FAST.c`

### KOS_OP_OBJECT_INPLACE {#kos_op_object_inplace}

- **Style**: OPCOUNT. Arg packs a KosOperand selector (high byte) with the
  operand count (low byte).
- **Stack**: `[..., a1, ... aN] → [..., result]`. Reduce the N operands
  left-to-right through the operator's `PyNumber_InPlace*` protocol and push
  the result.
- **Semantics**: The stack in-place operation behind the inplace-item and
  inplace-attr special forms; elected like OBJECT_OPERAND.
- **Implementation**: `koskari/lib/opcode/OBJECT_INPLACE.c`

### KOS_OP_OBJECT_BIT_INVERT {#kos_op_object_bit_invert}

- **Style**: NOARG.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_BIT_INVERT.c`

### KOS_OP_OBJECT_DIR {#kos_op_object_dir}

- **Style**: NOARG.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_DIR.c`

### KOS_OP_OBJECT_DIVMOD {#kos_op_object_divmod}

- **Style**: COUNT (operand count; always 2).
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_DIVMOD.c`

### KOS_OP_OBJECT_ITER {#kos_op_object_iter}

- **Style**: NOARG.
- **Semantics**: Uniform iterator acquisition over both protocols (shared with PUSH_FOREACH via `kos_iter_acquire`): a Koskari AsyncGenerator passes through as itself (native, never demand-wrapped), an object with `am_aiter` yields its async iterator, otherwise the sync protocol (`PyObject_GetIter`, including the sequence fallback). See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_ITER.c`


### KOS_OP_VAR_ITER {#kos_op_var_iter}

- **Style**: VAR. Arg = constant index of a name.
- **Stack**: `[...] -> [..., iterator]`. Fused VAR + OBJECT_ITER: resolve the name in the current frame's namespace and push its iterator, acquired uniformly over both protocols (`kos_iter_acquire`).
- **Compiler form:** Produced by `ByteCodeProcessor.fuse_feeders` when OBJECT_ITER is fed by a bare VAR load. Lowers to FAST_ITER through the simple `convert_fast` path.
- **Implementation**: `koskari/lib/opcode/VAR_ITER.c`
- **Covered by**: `tests/compile/test_fuse_feeders.py`,
  `tests/lib/opcode/test_object_unary.py`

### KOS_OP_FAST_ITER {#kos_op_fast_iter}

- **Style**: FASTVAR. Arg = slot index.
- **Stack**: As VAR_ITER, with the iterable loaded from its fast slot.
- **Compiler form:** Post-compile only: the `convert_fast` lowering of VAR_ITER when the name has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ITER.c`
- **Covered by**: `tests/compile/test_fuse_feeders.py`,
  `tests/lib/opcode/test_object_unary.py`

### KOS_OP_OBJECT_LEN {#kos_op_object_len}

- **Style**: NOARG.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_LEN.c`

### KOS_OP_OBJECT_NOT {#kos_op_object_not}

- **Style**: NOARG.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_NOT.c`

### KOS_OP_OBJECT_REPR {#kos_op_object_repr}

- **Style**: NOARG.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_REPR.c`

### KOS_OP_OBJECT_STR {#kos_op_object_str}

- **Style**: COUNT.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
  The core is `KosOperator_BuildStr`, exported from lib as `build_str`
  and bound to the runtime `#str`: an exact str is taken as it is,
  any other piece through its `__str__`, and the pieces joined once.
- **Implementation**: `koskari/lib/opcode/OBJECT_STR.c`

### KOS_OP_OBJECT_FORMAT {#kos_op_object_format}

- **Style**: COUNT.
- **Semantics**: format operation. Arg must be 1 or 2.
  - arg=1: pop object, call `PyObject_Format(obj, NULL)`.
  - arg=2: pop rules then object, call `PyObject_Format(obj, rules)`.
  Stack effect: `[..., obj] -> [..., str]` or `[..., obj, rules] -> [..., str]`.
- **Implementation**: `koskari/lib/opcode/OBJECT_FORMAT.c`

### KOS_OP_OBJECT_FORMAT_CONST {#kos_op_object_format_const}

- **Style**: CONST.
- **Semantics**: format operation with constant rules from cpool.
  Pop object, fetch format rules string from constant pool arg, call
  `PyObject_Format(obj, rules_const)`, push resulting string.
  Stack effect: `[..., obj] -> [..., str]`.
- **Implementation**: `koskari/lib/opcode/OBJECT_FORMAT_CONST.c`

### KOS_OP_VAR_FORMAT {#kos_op_var_format}

- **Style**: VAR. Arg = constant index of a name.
- **Stack**: `[...] -> [..., str]`. Fused VAR + OBJECT_FORMAT (arg=1): resolve the name in the current frame's namespace and push `PyObject_Format(obj, NULL)`.
- **Compiler form:** Produced by `ByteCodeProcessor.fuse_feeders` when OBJECT_FORMAT (arg=1) is fed by a bare VAR load. Lowers to FAST_FORMAT through the simple `convert_fast` path.
- **Implementation**: `koskari/lib/opcode/VAR_FORMAT.c`
- **Covered by**: `tests/compile/test_fuse_feeders.py`,
  `tests/lib/opcode/test_format_fused.py`

### KOS_OP_VAR_FORMAT_CONST {#kos_op_var_format_const}

- **Style**: EXTRA (VAR, CONST). Row = obj name (pool), rules string (pool).
- **Stack**: `[...] -> [..., str]`. Fused VAR + OBJECT_FORMAT_CONST: resolve the name in the current frame's namespace and push `PyObject_Format(obj, rules)`.
- **Compiler form:** Produced by `ByteCodeProcessor.fuse_feeders` when OBJECT_FORMAT_CONST is fed by a bare VAR load. The 2-arg OBJECT_FORMAT (dynamic rules) stays unfused: absorbing the object load would move its name resolution past the rules evaluation. Lowers to FAST_FORMAT_CONST through the generic schema-driven `convert_fast` path.
- **Implementation**: `koskari/lib/opcode/VAR_FORMAT_CONST.c`
- **Covered by**: `tests/compile/test_fuse_feeders.py`,
  `tests/lib/opcode/test_format_fused.py`

### KOS_OP_RETURN_VAR {#kos_op_return_var}

- **Style**: VAR. Arg = pool index of the name.
- **Stack**: `[...] -> [...]`. Resolve the name in the current frame's namespace (`frame_ns_deref`, NameError when unbound) and tail-call `kos_return_deliver` with the value, as RETURN 1 would with a popped one.
- **Compiler form:** Produced by `ByteCodeProcessor.fuse_feeders` when RETURN 1 is fed by a bare VAR load. Lowers to RETURN_FAST through `convert_fast` (registered beside the other suffix twins in `build_var_to_fast`).
- **Implementation**: `koskari/lib/opcode/RETURN_VAR.c`
- **Covered by**: `tests/lib/opcode/test_control_operand.py`,
  `tests/compile/test_fuse_feeders.py`

### KOS_OP_YIELD_VAR {#kos_op_yield_var}

- **Style**: VAR. Arg = pool index of the name.
- **Stack**: `[...] -> [...]`. Resolve the name as RETURN_VAR does and tail-call `kos_yield_deliver` with the value. Sets **`KOS_CODE_YIELDS`** at finalize and counts as a yield for tail-call demotion.
- **Compiler form:** Produced by `ByteCodeProcessor.fuse_feeders` when YIELD 1 is fed by a bare VAR load. Lowers to YIELD_FAST through `convert_fast`.
- **Implementation**: `koskari/lib/opcode/YIELD_VAR.c`
- **Covered by**: `tests/lib/opcode/test_control_operand.py`,
  `tests/compile/test_fuse_feeders.py`

### KOS_OP_FAST_FORMAT {#kos_op_fast_format}

- **Style**: FASTVAR. Arg = slot index.
- **Stack**: As VAR_FORMAT, with the object loaded from its fast slot.
- **Compiler form:** Post-compile only: the `convert_fast` lowering of VAR_FORMAT when the name has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_FORMAT.c`
- **Covered by**: `tests/compile/test_fuse_feeders.py`,
  `tests/lib/opcode/test_format_fused.py`

### KOS_OP_FAST_FORMAT_CONST {#kos_op_fast_format_const}

- **Style**: EXTRA (FASTVAR, CONST). Row = obj slot, rules string (pool).
- **Stack**: As VAR_FORMAT_CONST, with the object loaded from its fast slot.
- **Compiler form:** Post-compile only: the `convert_fast` lowering of VAR_FORMAT_CONST when the name has a fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_FORMAT_CONST.c`
- **Covered by**: `tests/compile/test_fuse_feeders.py`,
  `tests/lib/opcode/test_format_fused.py`

### KOS_OP_RETURN_FAST {#kos_op_return_fast}

- **Style**: FASTVAR. Arg = slot index.
- **Stack**: As RETURN_VAR, with the value read from its fast slot (`kos_ns_fast_deref`, NameError when empty).
- **Compiler form:** Post-compile only: the `convert_fast` lowering of RETURN_VAR when the name has a fast slot.
- **Implementation**: `koskari/lib/opcode/RETURN_FAST.c`
- **Covered by**: `tests/lib/opcode/test_control_operand.py`,
  `tests/compile/test_fuse_feeders.py`

### KOS_OP_YIELD_FAST {#kos_op_yield_fast}

- **Style**: FASTVAR. Arg = slot index.
- **Stack**: As YIELD_VAR, with the value read from its fast slot; sets **`KOS_CODE_YIELDS`** at finalize.
- **Compiler form:** Post-compile only: the `convert_fast` lowering of YIELD_VAR when the name has a fast slot.
- **Implementation**: `koskari/lib/opcode/YIELD_FAST.c`
- **Covered by**: `tests/lib/opcode/test_control_operand.py`,
  `tests/compile/test_fuse_feeders.py`

### KOS_OP_OBJECT_TRUTH {#kos_op_object_truth}

- **Style**: NOARG.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_TRUTH.c`

### KOS_OP_OBJECT_TYPE {#kos_op_object_type}

- **Style**: NOARG.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_TYPE.c`

### KOS_OP_OBJECT_VARS {#kos_op_object_vars}

- **Style**: NOARG.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_VARS.c`

### 19.1 In-place variable update (VAR_INPLACE / FAST_INPLACE and _CONST forms)

Augmented assignment: fetch the current value from a name (VAR) or fast slot
(FAST), apply the in-place operator with the rhs, store the result back. No
value is pushed (the form evaluates to None). The operator is a KosOperand
selector dispatched through `kos_inplace_reduce` (`operator.c`), the same
selector set as OBJECT_OPERAND / OBJECT_INPLACE. OBJECT_OPERAND,
OBJECT_INPLACE, FAST_INPLACE, FAST_INPLACE_CONST, CHECK_COMPARE, and
OBJECT_COMPARE each carry one cache slot of kind KOS_CACHE_ELECTOR
holding the body the site elected on its first operands: an int, a
float, or (for `+`) a str body per operator or selector, on the two
stack families a fold or join body for a run of three or more, or the
generic reduce. FAST_ATTR_INPLACE carries two: its field cache first
and an elector slot second, whose bodies guard through the field slot;
see [interpreter/electors.md](interpreter/electors.md).

**VAR_INPLACE** -- EXTRA (OPERAND, VAR). Row a0 is the bare operator
selector; a1 = constant pool index of the name. One rhs is popped from the
stack. Stack: `[..., rhs] → [...]`.

**FAST_INPLACE** -- EXTRA (OPERAND, FASTVAR). As above with a fast
slot. Produced by `convert_fast` lowering of the VAR form.

**VAR_INPLACE_CONST** -- EXTRA (OPERAND, VAR, CONST). The compressed
single-operand form with a constant rhs: a0 is the same bare KosOperand selector,
a1 the name, a2 the pooled operand. **Completely stack-free**: no push, no pop. Produced by
`Compiler.compress` fusing a prior CONST into a VAR_INPLACE --
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
POP_SHIFT delimit the shift block (handler). The captured continuation is the
delimited region with the shift expression as its hole -- a cloned chain
segment, not a bytecode span -- so a shift hosted in a callee captures
through the calling frames up to the prompt. Invoking k re-delimits, per
Danvy/Filinski: every invocation runs under a fresh prompt *instance*, and
prompts are resolved by instance identity (node_id), never by chain
position. The SHIFT node records the instance it captured against, capture
falsifies the ids of prompt clones in the template, the first invoke resumes
the live region in place (an INPLACE delim plus a fresh invocation-prompt
marker), and re-invokes graft a fresh copy of the template. An invocation
prompt pairs with no POP_PROMPT instruction; its completion routes through
the paired DELIM bookmark to the invoke site.

### KOS_OP_PUSH_PROMPT

- **Style**: CONST. Arg = prompt_id (constant pool index, post-compile).
- **Stack**: The prompt owns a fresh operand segment (so the reset body has an isolated stack), resolves the PROMPT extent at the push site and records the region's closing pc on the node (POP_SHIFT resumes at that pop_pc + 1), then pushes the prompt node onto the dynamic chain.
- **Pushes:** KOS_DYN_PROMPT (static extent KOS_EXT_PROMPT; PUSH_SHIFT captures up to this prompt). PROMPT owns the segment for the delimited region; SHIFT does not create one.
- **Invariants**: PUSH_SHIFT captures up to this prompt. An exception unwinding past a PROMPT drops the segment and unlinks the node in one step: the raise walk unlinks a **`KOS_DYN_CAP_BARRIER`** node with the barrier allowed (`kos_dyn_unlink_top`), with no kind-specific check in the walk.
- **Compiler form:** Pre-compile arg may be prompt id value or symbol; post-compile arg = pool index.

**Example:** reset (push prompt). Pool: 0 = prompt_id.

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 0     | PUSH_PROMPT| 0   | 0 (pid)  | push prompt (owning its segment) onto chain |
| 1     | …          | …   | …        | body before shift (continuation prefix) |
| 2     | PUSH_SHIFT | 0   | 0 (pid)  | capture k; enter shift block with k on stack |
| 3     | …          | …   | …        | shift body (k available) |
| 4     | POP_SHIFT  | 0   | 0        | return from shift without invoking |
| 5     | …          | …   | …        | continuation (body after shift) |
| 6     | POP_PROMPT | 0   | 0        | drop segment; pop prompt |

---

### KOS_OP_PUSH_SHIFT

- **Style**: CONST. Arg = prompt_id (constant pool index, post-compile).
- **Stack**: Captures the continuation: one walk from `dyn_top` locates the nearest matching prompt while offering `handle(SHIFT)` to each node crossed (GEN denies -- a shift cannot cross a generator boundary). Clones the chain segment from `dyn_top` through the prompt and its owning FRAME (including heritage nodes such as WIND); a capture bounded at an *invocation* prompt stops at the prompt and appends a compressed baseline frame instead. Prompt clones in the template are falsified -- fresh instance node_id plus KOS_DYNF_INVOCATION, with in-template SHIFT records remapped -- so grafted instances never collide with the live original. Pushes a KOS_DYN_SHIFT marker recording the captured prompt's node_id, pushes the captured continuation wrapper on stack, and enters the shift block. Does **not** peel nodes above the prompt.
- **Pushes:** KOS_DYN_SHIFT (bookmark recording the captured prompt instance; owns the handler body's operand segment, no handlers).
- **Invariants**: Extent KOS_EXT_SHIFT pairs with POP_SHIFT. The region's operands stay on the PROMPT's block; the handler body's go on the SHIFT's, so an in-place invoke finds each where it belongs.
- **Errors**: A walk that exhausts the chain without meeting a matching prompt raises `KoskariPromptNotFound`; a node that denies `handle(SHIFT)` (GEN) raises `KoskariPromptUnreachable` with the denying boundary recorded. Both carry `prompt_id` and `operation` (see `koskari.exceptions`, [archive/continuation-capture-errors.md](archive/continuation-capture-errors.md)).
- **Compiler form:** Pre-compile arg may be prompt id value/symbol; post-compile arg = pool index.

**Example:** shift k: body. Capture k in place, run shift body with k on stack. Pool: 0 = prompt_id.

| index | opcode    | arg | arg view | comment |
|------:|-----------|-----|----------|---------|
| 2     | PUSH_SHIFT| 0   | 0 (pid)  | capture k in place; push SHIFT on dyn_top; enter shift block |

---

### KOS_OP_POP_SHIFT

- **Style**: NOARG.
- **Stack**: Pops TOS as result and resolves the delimiter by the instance id recorded on the SHIFT node -- never by chain position or the current code object's extent tree. Live reset: pushes a transfer goal with no target whose seam is the prompt's parent; LEAVING unlinks through the SHIFT and the PROMPT (crossing the barrier), DELIVERING pushes the result onto the outer frame's stack, and execution resumes at the pop_pc the prompt recorded at push, + 1. Invocation prompt (created by a k-invoke; no POP_PROMPT pairs with it): the result is the value of that k-invocation and is delivered at the invoke site through the paired DELIM bookmark below the prompt.
- **Invariants**: Must be inside a PUSH_SHIFT extent. The resolved prompt carries everything the resume needs; no extent lookup happens at pop time.
- **Compiler form:** No argument.

**Example:** Shift handler returns value without invoking k.

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 4     | POP_SHIFT| 0   | 0        | pop result; unwind through SHIFT+PROMPT; drop segment; inject result onto outer stack |

---

### KOS_OP_POP_PROMPT

- **Style**: NOARG.
- **Stack**: Normal case (no active continuation return): pops TOS as the reset result, drops the PROMPT's segment, unlinks the prompt from the chain, and pushes the result onto the parent stack. Invoked-continuation return case: the walk down from the current node finds a **`KOS_DYN_DELIM`** bookmark (mode **`POP_PROMPT`** for a graft, **`INPLACE`** for an in-place invoke), passing invocation prompts and stopping at a live (non-invocation) PROMPT; it pops TOS as result, unwinds to the delim (freeing the invocation prompt and any remnants), delivers the result at the invoke site per the delim's return metadata (an INPLACE delim restitches the displaced state and re-links the stashed span first), and resumes there.
- **Invariants**: Top of chain must be the matching prompt when POP_PROMPT executes. When returning from an invoked delimited continuation (return path recorded on **`KOS_DYN_DELIM`**), POP_PROMPT transfers control back to the call site with the result value instead of exiting normally. Exactly one value is consumed from the PROMPT's segment and one is injected to the outer stack.
- **Push-return model**: Delimited invoke uses PUSH (graft frames onto current chain). When the continuation runs and hits POP_PROMPT, it consults the **`KOS_DYN_DELIM`** marker installed for that invoke and resumes at the call site with the value on stack.
- **Exception propagation**: If an exception escapes past the PROMPT during unwinding, the raise walk drops the segment and unlinks the PROMPT in the same unlink that crosses the barrier (`kos_dyn_unlink_top` with the barrier allowed), then continues unwinding. No POP_PROMPT opcode executes in this path.

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

- **Style**: COUNT, 0 or 1.
- **Stack**: Captures the continuation of the CC dynamic extent (from current pc
  until the extent is exited at POP_CC); pushes it (KosPyContinuation) onto
  stack. With arg 0 (`let/cc`) the stack effect is `[...] → [..., continuation]`.
  With arg 1 (`call/cc`) the callee on TOS is set aside for the capture and put
  back beneath the continuation, `[..., fn] → [..., fn, continuation]`, so the
  captured stack holds the form's enclosing operands and not the callee; the
  CALL 1 that follows applies fn to the continuation. Invoking the continuation
  is a non-local jump to the POP_CC opcode with a value pushed onto the stack.
- **Example:** See the PUSH_CC disassembly table: at index 1, CAPTURE_CC pushes
  the continuation. Invoking it (e.g. CALL 1 with 42) jumps to POP_CC (index 4)
  with stack [..., 42]; POP_CC runs; execution continues at index 5 (resume).


## 22. Dynamic wind

The extent contract and the single unlink site are described in [interpreter/extent-wind-unwind.md](interpreter/extent-wind-unwind.md): `kos_dyn_extent_enter` / `kos_dyn_extent_leave` on WIND and WITH, `kos_dyn_unlink_top` for every pop, walk, and transfer goal.

### KOS_OP_PUSH_WIND

- **Style**: COUNT. Arg = 1 (before only) or 2 (both before and after).
- **Stack**: For arg=2: `[..., before, after]` (before below, after above). Pops thunks; each must be callable or nil. For arg=1: `[..., before]`. Pushes KOS_DYN_WIND extent and asks it to enter (`kos_dyn_extent_enter`): the before thunk runs as a normal frame, and its return settles the node (`kos_dyn_settle_top`) before the body runs.
- **Pushes:** KOS_DYN_WIND (static extent KOS_EXT_WIND).
- **Example:** See extent-wind-unwind.md; thunks are invoked via kos_callable_call.

---

### KOS_OP_POP_WIND

- **Style**: NOARG.
- **Stack**: Stores TOS (the body result) on the wind node as a VALUE obligation (`kos_dyn_ob_store_value`) and unlinks it (`kos_dyn_unlink_top`): the leave runs the after thunk (if entered and non-null) as a normal frame, and the unlink delivers the body result to the revealed segment once the thunk has returned and settled (or at once when there is none). Answers REPROLOGUE while the thunk's frame is current, AWAIT when its call suspended.
- **Invariants**: Dyn top must be KOS_DYN_WIND; otherwise RuntimeError.


## 23. Parameterize extent and parameter reads

### KOS_OP_PUSH_PARAM

- **Style**: CONST. Arg = constant pool index of names tuple `(name_0, name_1, ...)`.
- **Stack**: `[..., val_0, val_1, ..., val_{n-1}]` → `[]`. Values consumed; names come from constant.
- **Pushes:** KOS_DYN_PARAM (extent kind KOS_EXT_PARAM; owns its operand segment). Builds dict from names and values, enters param extent.
- **Invariants**: Constant at arg must be a tuple of names; stack must have at least `len(names)` values.
- **Example:** `(parameterize ((x 1) (y 2)) body)` compiles to push values 1, 2, then PUSH_PARAM with names `("x", "y")`.

| index | opcode     | arg | arg view     | comment                    |
|------:|------------|-----|--------------|----------------------------|
| 0     | CONST      | 1   | 1 (1)        | push 1                     |
| 1     | CONST      | 2   | 2 (2)        | push 2                     |
| 2     | PUSH_PARAM | 0   | 0 (names)    | pop 2 values, push param   |
| 3     | …          | …   | …            | body                       |
| N     | POP_PARAM  | 0   | 0            | pop param extent           |

---

### KOS_OP_POP_PARAM

- **Style**: NOARG.
- **Stack**: No change.
- **Pops:** KOS_DYN_PARAM from dynamic chain. Restores the cells the extent displaced in the context's shadow table. The PARAM node owns its operand segment; any values remaining on it are delivered onto the revealed segment, so the observable stack effect is unchanged.
- **Invariants**: Dyn top must be KOS_DYN_PARAM; otherwise RuntimeError.

---

### KOS_OP_PARAM

- **Style**: EXTRA. Schema CONST (parameter name), COUNT (0 or 1 stacked defaults).
- **Stack**: `[]` → `[value]` at count 0; `[default]` → `[value]` at count 1.
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
- **Stack**: `[]` → `[value]`.
- **Resolves:** as PARAM, with the default coming from the constant pool rather than the stack. Never raises for an unbound name.
- **Invariants**: Both constants must be present in the pool.
- **Example:** `(parameterq x 42)` compiles to PARAM_CONST `("x", 42)`.
- **Example:** See PUSH_PARAM table.


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


---

## See also

- [interpreter/bytecode.md](interpreter/bytecode.md) — Bytecode format, KosCode layout, API
- [archive/bytecode-metadata-tables.md](archive/bytecode-metadata-tables.md) — Extents, location table, jump locality
- [interpreter/execution-invariants.md](interpreter/execution-invariants.md) — Execution model
- [EVALUATOR.md](EVALUATOR.md) — Evaluation loop
- [FEATURES.md](FEATURES.md) — Implementation status and feature list
- `koskari/lib/opcodes.incl` — Authoritative opcode enum and order
- `koskari.dis` — Runtime disassembly of KosCode objects


## Document status

This document is the design target for Koskari opcodes. When semantics or
invariants change, update this file and the implementation together. Last
substantive update: pre/post-compile representation (§1.5–1.10), LABEL/POSITION
stripping, quick-reference index, glossary, maintaining checklist, and
intended/current notes for stubs.
