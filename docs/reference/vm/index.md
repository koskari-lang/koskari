# Koskari bytecode overview
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
cross-linking between sections of this reference.

### 1.2 Notation

- **Stack**: `[a, b, c]` with TOS (top of stack) on the right. "TOS" = top of
  stack; "below TOS" = second element, etc.
- **Stack effect**: Written as "Before → After" (e.g. `[..., callable, a, b] → [..., result]`).
- **Chain**: The dynamic chain (frames, prompts, regions, etc.) is described
  when an opcode pushes or pops nodes. See §1.5 (global invariants)
  and §1.5 and per-opcode extent notes.

### 1.3 Glossary

- **BIND** (verb): Establish or replace the name→ref mapping. "I will now use this
  reference to resolve the given name." Opposed to STORE. See §1.9.
- **STORE** (verb): Write a value through the reference that already resolves a
  name. "I will now store this value in the given name." Opposed to BIND. See §1.9.
- **pc**: Instruction index (program counter) into the code array.
- **TOS**: Top of stack; rightmost element in stack notation.
- **cpool**: Constant pool; the read-only array of constants in a KosCode (`cpool[i]`).
- **extent**: A statically described block (stored in the code object's extent
  table) with kind `KosExtentKind` (KOS_EXT_TRY, KOS_EXT_LOOP, KOS_EXT_FOREACH,
  KOS_EXT_PROMPT, KOS_EXT_NS, KOS_EXT_COLLECT, etc.), enter/exit pcs, and
  kind-specific targets; see §1.5 and per-opcode extent notes.
- **dynamic chain / chain node**: At runtime, the evaluator maintains a chain of
  nodes (`KosDynKind`: KOS_DYN_FRAME, KOS_DYN_TRY, KOS_DYN_FINALLY, KOS_DYN_LOOP,
  KOS_DYN_FOREACH, KOS_DYN_NS, KOS_DYN_COLLECT, KOS_DYN_PROMPT, KOS_DYN_CC,
  KOS_DYN_CC_RAISE, KOS_DYN_WIND, KOS_DYN_WITH, KOS_DYN_DYN_WITH, KOS_DYN_YIELD_FROM,
  KOS_DYN_PARAM, KOS_DYN_SHIFT). PUSH_* opcodes push a node onto this chain;
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
| **opcode** | Opcode name (e.g. `KOS_OP_CHECK_IN`). |
| **arg** | Raw argument value (uint16). |
| **arg view** | Decoded argument for readability (e.g. constant as `index (repr)`, branch as `N (-> N)`). |
| **comment** | Stack state before the instruction and/or control-flow or stack effect. |

Long comments (stack, branch conditions) may span the cell; keep index and opcode compact so the table stays scannable.

### 1.5 Global invariants

Unless specified otherwise for an opcode:

- Execution obeys the invariants in §1.5 (global invariants).
- Extent and location metadata are as in §1.5 and per-opcode extent notes.
- **Jump locality**: Control-flow jump opcodes (JUMP, JUMP_FALSE, JUMP_TRUE,
  JUMP_AND, JUMP_OR, ITER_NEXT, CHECK_*) may only target a pc inside the current
  extent or at its `pop_pc` (leaving the block). See §1.5 and per-opcode extent notes
  § "Jump Legality and Locality".

The **Invariants** bullets per opcode (and this section) form the **contract**:
the evaluator must preserve them, and the compiler must produce code that
satisfies them. Violations are undefined behaviour (or may be asserted in debug
builds).

### 1.6 Implementation status

**This document is the single authoritative reference for opcode implementation
status.** Each opcode section below notes: **Implemented**, **Stub**
(unimplemented; runtime error if executed), or **No runtime** (e.g. THEEND;
hardcoded error; compile-time only).

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
| Name (string/symbol)   | Constant pool index     | VAR, VAR_BIND, VAR_CLEAR, VAR_REF, VAR_STORE; ATTR*, CONST_ITEM* (key); INVOKE_K_VAR |
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
| Constant + `JUMP_TRUE` | Prior **`KOS_OP_CONST`**: if truthy, replace with **`KOS_OP_JUMP`** to the same label; if falsy, drop both (no jump). |
| Constant + `JUMP_FALSE` | Prior **`KOS_OP_CONST`**: if the constant is falsy, unconditional **`KOS_OP_JUMP`**; if truthy, drop both. |
| Compare + `JUMP_FALSE` | Prior opcode is in **`_CHECK_SQUASHES`** (see table below): delete the prior instruction and replace **`JUMP_FALSE`** with the mapped single opcode (same jump target **arg**). |
| `SWITCH_EQ` fusion | Prior opcode in **`_SWITCH_SQUASHES`**: replace **`KOS_OP_SWITCH_EQ`** with the mapped opcode; **arg** becomes **`(switch_label, *pargs[:3])`** if the prior **arg** was a tuple, else **`(switch_label, pargs, None, None)`** ( **`switch_label`** is this instruction’s original **arg**). |
| `LABEL` vs prior jump | If the **`KOS_OP_LABEL`** name equals the prior instruction’s target: for prior **`KOS_OP_JUMP`**, drop the jump; for prior **`KOS_OP_JUMP_TRUE`** / **`KOS_OP_JUMP_FALSE`**, if the instruction before the jump is **`KOS_OP_CONST`**, drop const and jump; else replace the conditional jump with **`KOS_OP_POP`** **1**. |
| Implicit `None` | **`KOS_OP_CONST`** **`None`** immediately before **`KOS_OP_RETURN`** / **`KOS_OP_BREAK`** / **`KOS_OP_CONTINUE`** / **`KOS_OP_YIELD`** with **arg 1** → remove the const and set the control opcode’s **arg** to **0**. |
| `CAR` / `CDR` merge | Two consecutive **same** opcodes **`KOS_OP_CAR`** or **`KOS_OP_CDR`** → one instruction with **args summed**. |
| `VAR_STORE` fusion | Prior **`KOS_OP_CONST`** `value` immediately followed by **`KOS_OP_VAR_STORE`** `name` → **`KOS_OP_VAR_STORE_CONST`** with arg **`(name, value, None, None)`** (drop the **`CONST`**). Only the **immediate** prior instruction counts. |
| Epilogue | If the stream does not end with **`KOS_OP_RETURN`**: if the last op is **`KOS_OP_CONST`** **`None`**, replace it with **`KOS_OP_RETURN`** **0**; otherwise append **`KOS_OP_RETURN`** **1**. |

**`_CHECK_SQUASHES`** (prior opcode → opcode replacing the **`JUMP_FALSE`** pair):

| Prior | Replaced with |
|-------|----------------|
| `KOS_OP_OBJECT_GE` | `KOS_OP_CHECK_GE` |
| `KOS_OP_OBJECT_LE` | `KOS_OP_CHECK_LE` |
| `KOS_OP_OBJECT_LT` | `KOS_OP_CHECK_LT` |
| `KOS_OP_OBJECT_GT` | `KOS_OP_CHECK_GT` |
| `KOS_OP_OBJECT_EQ` | `KOS_OP_CHECK_EQ` |
| `KOS_OP_OBJECT_NE` | `KOS_OP_CHECK_NE` |
| `KOS_OP_OBJECT_IN` | `KOS_OP_CHECK_IN` |
| `KOS_OP_OBJECT_NOT_IN` | `KOS_OP_CHECK_NOT_IN` |
| `KOS_OP_OBJECT_IS` | `KOS_OP_CHECK_IS` |
| `KOS_OP_OBJECT_IS_NOT` | `KOS_OP_CHECK_IS_NOT` |
| `KOS_OP_OBJECT_IS_INSTANCE` | `KOS_OP_CHECK_IS_INSTANCE` |
| `KOS_OP_OBJECT_IS_SUBCLASS` | `KOS_OP_CHECK_IS_SUBCLASS` |
| `KOS_OP_OBJECT_NOT` | `KOS_OP_JUMP_TRUE` |
| `KOS_OP_EXCEPTION_MATCH` | `KOS_OP_CHECK_EXCEPTION` |

**`_SWITCH_SQUASHES`** (prior opcode → fused switch opcode):

| Prior | Replaced with |
|-------|----------------|
| `KOS_OP_CONST` | `KOS_OP_SWITCH_EQ_CONST` |
| `KOS_OP_VAR` | `KOS_OP_SWITCH_EQ_VAR` |
| `KOS_OP_VAR_ATTR` | `KOS_OP_SWITCH_EQ_VAR_ATTR` |

**Example.** `KOS_OP_OBJECT_EQ`, **`KOS_OP_JUMP_FALSE`** `L` → one **`KOS_OP_CHECK_EQ`** `L` (same label argument as the jump).

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
   helpful. For stubs, add **Intended** and **Current** (stub/error) if helpful.
4. **Add or extend tests** that match the documented behaviour; consider adding a
   "Covered by …" line in the doc (e.g. `tests/lib/test_ceval.py`).
5. If you change **`Compiler.compress`** or its squash maps, update **§1.8** and
   **`tests/compile/test_compress.py`**.

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


### KOS_OP_UNPACK {#kos_op_unpack}

- **Style**: COUNT. Arg = `n` (number of values to push).
- **Status**: Implemented. (`koskari/lib/opcode/UNPACK.c`)
- **Stack**: `[..., raw] → [..., v_{n-1}, ..., v_1, v_0]` where `v_i` is the
  `i`-th element of the realized sequence and **TOS = v_0 = seq[0]** (so the
  first binding in source order can use `VAR_STORE` / `FAST_STORE` on TOS
  first). Implementation: pop TOS, `PySequence_List(raw)` (new list), require
  `len(seq) == n`, then `KosStackBlock_PushRef` for indices `n-1` down to `0`,
  then `Py_DECREF(seq)`.
- **Invariants**: `n >= 0`. For `n == 0`, TOS must be an empty sequence after
  realization.
- **Errors**: `ValueError` if length ≠ `n`; `TypeError` or other error if
  `PySequence_List` fails.
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
- **Status**: Implemented. (`koskari/lib/opcode/UNPACK_STAR.c`)
- **Semantics**: Pop TOS, `PySequence_List(raw)`. Require `left + right > 0`
  and `len(seq) >= left + right`. Let `L = len(seq)`. The **middle** slice is
  `seq[left : L - right]` (inclusive start, exclusive end), materialized as a
  **tuple** (new reference). Pushes are ordered so that after all pushes,
  **TOS is the first prefix element** `seq[0]` (when `left > 0`):
  1. **Suffix** scalars: indices `L-1, L-2, ..., L-right` (each via
     `KosStackBlock_PushRef`).
  2. **Middle**: one value — the tuple — via `KosStackBlock_PUSH` (**stolen**
     reference from `PySequence_Tuple`).
  3. **Prefix** scalars: indices `left-1, ..., 0` (each via `PushRef`).
- **Stack (TOS right)**: `[..., raw] → [..., seq[L-1], ..., seq[L-right],
  middle, seq[left-1], ..., seq[0]]` when all parts are non-empty; adjust when
  `left` or `right` is zero (no prefix or suffix pushes for that side).
- **Invariants**: Extra row matches schema; `left + right` fits in uint16;
  realized value must be a valid sequence for `PySequence_List` / slicing.
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
- **Status**: Implemented. (`koskari/lib/opcode/UNPACK_VAR.c`)
- **Stack**: `[..., raw] → [...]`. No values pushed; **STORE** semantics only
  (same as `VAR_STORE` / `KosNamespace_Store` through `frame_ns_store`): `seq[0]`
  into the first name, `seq[1]` into the second. Pop TOS, `PySequence_List`,
  require `len == 2`, then store; release the temporary list.
- **Invariants**: Both names must already be bound in the frame namespace (see
  §1.9 STORE vs BIND).
- **Errors**: `ValueError` if length ≠ 2.
- **Compiler form:** Pre-compile extra tuple `(name0, name1, 0, 0)`; names
  intern to cpool like other `VAR` extra fields.

---

### KOS_OP_UNPACK_FAST {#kos_op_unpack_fast}

- **Style**: EXTRA (`FASTVAR`, `FASTVAR`, `NOARG`, `NOARG`). Row: `a0`, `a1` =
  fast local slot indices.
- **Status**: Implemented. (`koskari/lib/opcode/UNPACK_FAST.c`)
- **Stack**: `[..., raw] → [...]`. Pop TOS, `PySequence_List`, require `len == 2`,
  then `KosNamespace_FastStore` for slot `a0` with `seq[0]` and slot `a1` with
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


### 5.1 Pair construction and access

### KOS_OP_CAR {#kos_op_car}

- **Style**: COUNT. Arg = repeat count: apply **car** that many times to the
  stack top (each step pops one pair and pushes its car).
- **Status**: Implemented.
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
- **Status**: Implemented.
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
- **Status**: Implemented.
- **Stack**: `[..., e_0, ..., e_{count-1}] → [..., result]` where result =
  (e_0 . (e_1 . ... (e_{count-1}) ...)). Implemented via KosStackBlock_TailPtr,
  KosPair_ConsFromPtr, KosStackBlock_Drop, then push result. count 0 → push
  nil (INCREF).


### KOS_OP_PROPER {#kos_op_proper}

- **Style**: COUNT. Arg = number of element values on stack.
- **Status**: Implemented. (`koskari/lib/opcode/PROPER.c`)
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


### KOS_OP_PAIR_SPLICE {#kos_op_pair_splice}

- **Style**: COUNT. Arg = number of segments on stack (bottom = first segment,
  top = last segment).
- **Status**: Implemented. (`koskari/lib/opcode/PAIR_SPLICE.c`)
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

## 6. Mode control

### KOS_OP_AWAIT

- **Style**: COUNT (stack element count).
- **Status**: Implemented.
- **Arg**: `1` — pop TOS (awaitable), dispatch; may suspend with `KOS_EV_AWAIT` or push the resolved value; on resume from a Python awaitable, result is pushed onto the operand stack. `0` — bare cooperative suspend: no stack pop or push; suspends with `KOS_EV_AWAIT` and `Py_None` as the event payload (no `py_await_iter`); `revive_await` does not push the resume value onto the operand stack (e.g. next instruction may be `CONST None` for `(compile/await)`).

**Example:** Await a coroutine; TOS is the awaitable. On resume, result is pushed.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (coro)  | stack: [..., awaitable] |
| 1     | AWAIT  | 1   | 1        | pop one; suspend; on resume stack: [..., value] |
| 2     | RETURN | 1   | 1        | |

---

### KOS_OP_BREAK

- **Style**: COUNT. Arg = number of stack elements to pop before jumping.
- **Status**: Implemented.
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
- **Stack**: Pops the return value(s) per `arg`. Does **not** push onto the caller inside the opcode handler: after **`kos_dyn_transition`** (**`KOS_TRANS_WALK_RETURN_TO_FRAME`**) and **`kos_dyn_pop_frame`**, a **non-GEN** parent path returns **`KOS_OP_POST_PARENT_RETURN()`** (**`KosOpOutcome` / `KOS_LOP_POST`**), and **`eval_parent_return_deliver`** (post-opcode) performs iterator-boundary delivery, native-handler delivery, or **`KosStackBlock_PushRef`** onto the caller, then **`kos_dynnode_on_control_returned`**. **Top-level** or **GEN** parent: **`KOS_EV_RETURN`** in **`out_ev`**, **`KOS_OP_POST_SUSPEND()`** — driver-visible completion boundary. **Delimited** continuation returns (`RETURN_TARGET_CHAIN`) are a separate early path.
- **Invariants**: See execution-invariants: in-loop flattening for RETURN.
- **See also**: [compile/return](../specials/compile-return.md); **`koskari/lib/opcode/RETURN.c`**.

**Example:** Return TOS (arg = 1). Pool: 0 = 42.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (42)   | stack: [..., 42] |
| 1     | RETURN | 1   | 1        | return 42 (post-opcode deliver to caller or boundary `KOS_EV_RETURN`) |

---

### KOS_OP_YIELD

- **Style**: COUNT. Arg = number of values to yield (typically 1).
- **Status**: Implemented.
- **Stack**: Pops the yielded value(s). Sets **`KOS_CODE_YIELDS`** on the code object. Opcode sets **`out_ev = KOS_EV_YIELD`** and returns **`KOS_OP_POST_SUSPEND()`**; **`eval_event_yield`** (post-opcode) runs **`eval_yield_handle_walk_and_freeze`** (**`kos_dyn_walk_handle_yield`** via **`kos_dyn_transition`**), then either **`eval_event_yield_deliver_to_boundary`** when **`kos_dyn_need_kos_agen_boundary_deliver`** (flattened **NEXT** / **FOREACH** / **YIELD_FROM**, often **REPROLOGUE** without stopping **`koskari_eval`**) or **`STOP_EVENT`** with **`KOS_SUSP_YIELD`** for the outer driver.
- **Invariants**: Code is treated as async generator when YIELDS flag set.
- **See also**: [compile/yield](../specials/compile-yield.md); **`koskari/lib/opcode/YIELD.c`**.

**Example:** Yield TOS (arg = 1). Pool: 0 = 100.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (100)  | stack: [..., 100] |
| 1     | YIELD  | 1   | 1        | yield 100; on resume continue |

---

### KOS_OP_YIELD_FROM

- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: On first entry: consume TOS (subexpr_result), push `KOS_DYN_YIELD_FROM`, roll back PC so the next loop iteration re-dispatches to YIELD_FROM. On each resume: drive one step of the delegate (or handle send/throw/close inject). On completion: pop the dyn node, push final value. PC does not advance until delegation completes.
- **Execution model**: Single-opcode delegation primitive (conduit); not loop sugar. Full send/throw/close propagation; async flattening for Python and Koskari async gens. Backed by dynamic chain node `KOS_DYN_YIELD_FROM`, which holds the delegate, its kind, and pending action/arg for send/throw/close and for resume-from-await.
- **Delegate kinds** (chosen at first entry by normalizing TOS): **sync iterator** (PyIter_Next); **sync generator** (send/throw/close); **Python async iterator** (__anext__); **Python async generator** (__anext__/asend/athrow/aclose); **Koskari async generator** (driven in-process via koskari_eval). Callables are called once; the return value is classified as above. Fallback: `iter(obj)` for sync iteration.
- **Rejected**: Coroutines (Koskari or Python). Normalization raises `TypeError: "coroutine object is not async iterable"`. Delegate results (yielded or final value) are never re-awaited; they are pushed or emitted as values only.
- **Invariants**: TOS on first entry must be an iterable or async-iterable (or callable returning one). The opcode is re-entered on each loop iteration until the delegate exhausts, yields (boundary KOS_EV_YIELD), or suspends on await (boundary KOS_EV_AWAIT).
- **Implementation**: Handler and all YIELD_FROM-dedicated helpers (normalize, rollback PC, fetch StopIteration value, emit yield, complete with value, drive sync iter/gen and Python/Koskari async) live in `koskari/lib/opcode/YIELD_FROM.c` (included into `eval.c`). Delegate close / unwind for the `KOS_DYN_YIELD_FROM` node lives in `koskari/lib/dynchain/dyn_yield_from.c` (e.g. `kos_yieldfrom_close_delegate`, `kos_dyn_yieldfrom_unwind`). See [koskari/lib/opcode/README.md](../../koskari/lib/opcode/README.md) for the opcode-fragment layout.
- **See also**: [compile/yield-from](../specials/compile-yield-from.md)
- **Covered by**: `tests/lib/opcode/test_yield_from.py` (sync/async delegates, Koskari async gen, send/throw/close, final values, type errors).

**Example:** Delegate to a sync iterator; each yielded value is emitted (or pushed in-loop); final value is pushed when the iterator exhausts. Pool: 0 = [1, 2, 3].

| index | opcode   | arg | arg view | comment |
|------:|---------|-----|----------|---------|
| 0     | CONST   | 0   | 0 ([1,2,3]) | stack: [..., iterable] |
| 1     | YIELD_FROM | 0 | 0        | first entry: consume TOS, push KOS_DYN_YIELD_FROM, rollback PC; resume: drive delegate; on exhaust stack: [..., final_value] |
| 2     | RETURN  | 1   | 1        | |

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
| 1     | PUSH_FOREACH | 0 | 0        | push region; iterator from iterable; break target from extent table |
| 2     | ITER_NEXT| 6   | 6 (-> 6)  | next from region; if exhausted → pc=6. stack: [..., val] or jump |
| 3     | POP      | 1   | 1        | consume value (body) |
| 4     | JUMP     | 2   | 2 (-> 2) | loop back |
| 5     | POP_FOREACH | 0 | 0        | |
| 6     | LABEL    | —   | —        | else (exhausted) |

---

### KOS_OP_NEXT

- **Style**: COUNT. Arg = 1 or 2: 1 = TOS is iterator or async iterator (StopIteration raises on exhaustion); 2 = TOS = default, below = iterator or async iterator (StopIteration pushes default).
- **Status**: Implemented.
- **Stack**: Stack-based next from iterator or async iterator. May suspend on async iterator.
- **Invariants**: TOS (or below-TOS for arg=2) must be a sync iterator or an async iterator. A bare iterable (e.g. list, tuple) is not acceptable; use OBJECT_ITER or equivalent to obtain an iterator first. Stack layout per arg (1 or 2 elements as described).

**Example (arg=1):** TOS = iterator or async iterator; push next or raise StopIteration.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (iter) | stack: [..., iterator or async iterator] |
| 1     | NEXT   | 1   | 1        | stack: [..., next_val] or raise StopIteration |

**Example (arg=2):** TOS = default, below = iterator or async iterator; push next or default.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 0     | CONST  | 0   | 0 (99)   | stack: [..., default] |
| 1     | CONST  | 1   | 1 (iter) | stack: [..., default, iterator or async iterator] |
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
- **Status**: Implemented.
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

### KOS_OP_CHECK_LT {#kos_op_check_lt}

- **Style**: LABEL. Arg = target pc (post-compile).
- **Status**: Implemented.
- **Stack**: `[..., left, right]` (right = TOS). Pops both; if **not** `left < right`, jump to `arg`; else fall through. Stack after: `[...]`.
- **Invariants**: Jump locality for target.
- **Compiler form:** Pre-compile arg = string label; post-compile arg = pc.

---

### KOS_OP_CHECK_LE {#kos_op_check_le}

- **Style**: LABEL. Arg = target pc (post-compile).
- **Status**: Implemented.
- **Stack**: `[..., left, right]` (right = TOS). Pops both; if **not** `left <= right`, jump to `arg`; else fall through. Stack after: `[...]`.
- **Invariants**: Jump locality for target.
- **Compiler form:** Pre-compile arg = string label; post-compile arg = pc.

---

### KOS_OP_CHECK_GT {#kos_op_check_gt}

- **Style**: LABEL. Arg = target pc (post-compile).
- **Status**: Implemented.
- **Stack**: `[..., left, right]` (right = TOS). Pops both; if **not** `left > right`, jump to `arg`; else fall through. Stack after: `[...]`.
- **Invariants**: Jump locality for target.
- **Compiler form:** Pre-compile arg = string label; post-compile arg = pc.

---

### KOS_OP_CHECK_GE {#kos_op_check_ge}

- **Style**: LABEL. Arg = target pc (post-compile).
- **Status**: Implemented.
- **Stack**: `[..., left, right]` (right = TOS). Pops both; if **not** `left >= right`, jump to `arg`; else fall through. Stack after: `[...]`.
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

See archive/switch-eq-opcodes.md for family rationale and the
surface `compile/switch` form. The full SWITCH_EQ family is implemented.

### KOS_OP_SWITCH_EQ

- **Style**: LABEL. Arg = target pc (post-compile).
- **Status**: Implemented.
- **Stack**: `[..., left, right]` (right = TOS). Compares `left == right` with
  the same equality semantics as [CHECK_EQ](#kos_op_check_eq)
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
- **Status**: Implemented.
- **Stack**: `[..., s]`. Compare `s == cpool[a1]` with `Py_EQ`.
  - **Match**: pop `s`; `pc = a0`.
  - **Mismatch**: fall through; stack unchanged (`s` still TOS).
- **Compiler form:** tuple arg `("label", const_value, 0, 0)` pre-compile;
  post-compile row stores pc in `a0` and constant index in `a1`.
- **Implementation**: `koskari/lib/opcode/SWITCH_EQ_CONST.c`

### KOS_OP_SWITCH_EQ_VAR

- **Style**: EXTRA. Schema `(LABEL, VAR, NOARG, NOARG)`.
- **Status**: Implemented.
- **Stack**: `[..., s]`. Resolve `rhs = frame_ns_deref(var_name)` from `a1`,
  then compare `s == rhs` with `Py_EQ`.
  - **Match**: pop `s`; `pc = a0`.
  - **Mismatch**: fall through; stack unchanged.
- **Compiler form:** tuple arg `("label", "name", 0, 0)` pre-compile.
- **Implementation**: `koskari/lib/opcode/SWITCH_EQ_VAR.c`

### KOS_OP_SWITCH_EQ_FAST

- **Style**: EXTRA. Schema `(LABEL, FASTVAR, NOARG, NOARG)`.
- **Status**: Implemented.
- **Stack**: `[..., s]`. Resolve `rhs = KosNamespace_FastDeref(ns, a1)`, then
  compare `s == rhs` with `Py_EQ`.
  - **Match**: pop `s`; `pc = a0`.
  - **Mismatch**: fall through; stack unchanged.
- **Compiler form:** tuple arg `("label", slot_index, 0, 0)` pre-compile.
- **Implementation**: `koskari/lib/opcode/SWITCH_EQ_FAST.c`

### KOS_OP_SWITCH_EQ_VAR_ATTR

- **Style**: EXTRA. Schema `(LABEL, VAR, CONST, NOARG)`.
- **Status**: Implemented.
- **Stack**: `[..., s]`. Resolve `obj = frame_ns_deref(var_name)` from `a1`,
  `rhs = getattr(obj, attr_name)` from `a2`, then compare `s == rhs`.
  - **Match**: pop `s`; `pc = a0`.
  - **Mismatch**: fall through; stack unchanged.
- **Compiler form:** tuple arg `("label", "obj_name", "attr_name", 0)` pre-compile.
- **Implementation**: `koskari/lib/opcode/SWITCH_EQ_VAR_ATTR.c`

### KOS_OP_SWITCH_EQ_FAST_ATTR

- **Style**: EXTRA. Schema `(LABEL, FASTVAR, CONST, NOARG)`.
- **Status**: Implemented.
- **Stack**: `[..., s]`. Resolve `obj = KosNamespace_FastDeref(ns, a1)`,
  `rhs = getattr(obj, attr_name)` from `a2`, then compare `s == rhs`.
  - **Match**: pop `s`; `pc = a0`.
  - **Mismatch**: fall through; stack unchanged.
- **Compiler form:** tuple arg `("label", slot_index, "attr_name", 0)` pre-compile.
- **Implementation**: `koskari/lib/opcode/SWITCH_EQ_FAST_ATTR.c`

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

---

### 10.1 Module import

The three import opcodes together implement Python's `import` statement at the
bytecode level. IMPORT and IMPORT_REL load a module and push it; IMPORT_FROM
extracts named attributes from the module. See `koskari/compile/specials/imports.py`
(`helper_import_from_sequence`, `special_import`, `special_import_from`) for
the compiler special forms that emit these opcodes.

### KOS_OP_IMPORT {#kos_op_import}

- **Style**: CONST. Arg = constant pool index of module name string (post-compile).
- **Status**: Implemented.
- **Stack**: `[...] → [..., module]`. Perform an absolute module import and push
  the **leaf** module. For dotted names (e.g. `"os.path"`), the leaf component
  (`os.path`) is pushed, not the root (`os`). This matches `importlib.import_module`
  semantics and differs from bare `__import__()`.
- **Invariants**: `cpool[arg]` must be a str.
- **Leaf behaviour**: Achieved by passing the module-level sentinel
  `_import_fromlist = ("*",)` as `fromlist` to `PyImport_ImportModuleLevelObject`.
  `_import_fromlist` is initialised once in `koskari_eval_init` and cleared in
  `koskari_eval_clear`.
- **Compiler form:** Pre-compile arg = module name string (e.g. `"os"` or
  `"os.path"`); post-compile arg = constant pool index.
- **Implementation**: `koskari/lib/opcode/IMPORT.c`

**Example:** `(import os.path)` — load `os.path` and bind as `path`.
Pool: 0 = `"os.path"`, 1 = `"path"`, 2 = None.

| index | opcode   | arg | arg view       | comment |
|------:|----------|-----|----------------|---------|
| 0     | IMPORT   | 0   | 0 ("os.path")  | stack: [..., os.path] |
| 1     | VAR_BIND | 1   | 1 ("path")     | bind "path" = module; stack: [...] |
| 2     | CONST    | 2   | 2 (None)       | statement value; stack: [..., None] |
| 3     | RETURN   | 1   | 1              | |

---

### KOS_OP_IMPORT_REL {#kos_op_import_rel}

- **Style**: CONST. Arg = constant pool index of a `(level:int, name:str|None)` tuple
  (post-compile).
- **Status**: Implemented.
- **Stack**: `[...] → [..., module]`. Perform a relative module import and push
  the leaf module. Semantics otherwise identical to IMPORT.
- **Invariants**: `cpool[arg]` must be a 2-tuple `(int, str|None)`.
- **`level`**: Number of leading dots in the import expression (e.g. 1 for
  `(import .foo)`, 2 for `(import ..foo)`).
- **`name`**: Module name after the dots, or `None` for a bare relative import
  (e.g. `(import .)` which imports the enclosing package itself).
- **Anchor resolution**: The module globals namespace (outermost parent of the
  current frame's namespace) is passed as the `globals` argument to
  `PyImport_ImportModuleLevelObject` so Python can read
  `__name__` and `__package__` to resolve the anchor package. This is identical to
  what Python's own relative import mechanism does.
- **Compiler form:** Pre-compile arg = `(level, name)` Python tuple; post-compile
  arg = constant pool index.
- **Implementation**: `koskari/lib/opcode/IMPORT_REL.c`

**Example:** `(import .foo)` inside package `mypkg` — load `mypkg.foo` and bind as `foo`.
Pool: 0 = `(1, "foo")`, 1 = `"foo"`, 2 = None.

| index | opcode     | arg | arg view       | comment |
|------:|------------|-----|----------------|---------|
| 0     | IMPORT_REL | 0   | 0 ((1, "foo")) | stack: [..., mypkg.foo] |
| 1     | VAR_BIND   | 1   | 1 ("foo")      | bind "foo" = module; stack: [...] |
| 2     | CONST      | 2   | 2 (None)       | statement value |
| 3     | RETURN     | 1   | 1              | |

---

### KOS_OP_IMPORT_FROM {#kos_op_import_from}

- **Style**: CONST. Arg = constant pool index of attribute name string
  (post-compile).
- **Status**: Implemented.
- **Stack**: `[..., module] → [..., module, value]`. Read named attribute from
  TOS **without consuming TOS**. The module remains on the stack.

> **PEEK-NOT-POP WARNING** — This is the **only** opcode in the Koskari
> instruction set that reads TOS without removing it. This behaviour allows
> multiple successive IMPORT_FROM instructions to extract different attributes
> from the same module. Every sequence of one or more IMPORT_FROM instructions
> **must** be followed by `POP 1` to remove the module from the stack.
> Omitting the POP leaves a dangling module reference that corrupts the stack.
>
> This invariant is **structurally enforced**: IMPORT_FROM is only ever emitted
> by `helper_import_from_sequence()` in `koskari/compile/specials/imports.py`,
> which always emits the compensating `POP 1` as its final instruction.
> Do not emit IMPORT_FROM anywhere else.

- **Invariants**: TOS must be a module or object with the named attribute.
  `cpool[arg]` must be a str. The module is **not popped**; the caller (always
  `helper_import_from_sequence`) is responsible for the compensating `POP 1`.
- **Compiler form:** Pre-compile arg = attribute name string; post-compile arg =
  constant pool index.
- **Implementation**: `koskari/lib/opcode/IMPORT_FROM.c`

**Example:** `(import-from os path sep)` — import `os`, extract `path` and `sep`.
Pool: 0 = `"os"`, 1 = `"path"`, 2 = `"sep"`, 3 = None.

| index | opcode      | arg | arg view    | comment |
|------:|-------------|-----|-------------|---------|
| 0     | IMPORT      | 0   | 0 ("os")    | stack: [..., os] |
| 1     | IMPORT_FROM | 1   | 1 ("path")  | PEEK TOS; stack: [..., os, os.path] |
| 2     | VAR_BIND    | 1   | 1 ("path")  | bind "path"; stack: [..., os] |
| 3     | IMPORT_FROM | 2   | 2 ("sep")   | PEEK TOS; stack: [..., os, os.sep] |
| 4     | VAR_BIND    | 2   | 2 ("sep")   | bind "sep"; stack: [..., os] |
| 5     | POP         | 1   | 1           | remove module; stack: [...] |
| 6     | CONST       | 3   | 3 (None)    | statement value |
| 7     | RETURN      | 1   | 1           | |

---

**Note:** There is **no** `KOS_OP_CAPTURE_FRAME` (or similar) in `koskari/lib/opcodes.incl`; a future opcode would be added there first, then documented here.

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

- **Style**: NOARG.
- **Status**: Implemented.
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
- **Status**: Implemented.
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


### 12.1 Attribute access on variables (VAR_ATTR_*)

These opcodes perform attribute get, set, clear, or reference on an object fetched from the frame namespace by name. The object is not on the stack; it is resolved via `frame_ns_deref(var_name)`. Each uses EXTRA schema: VAR (obj name), CONST (attr name).

- **Status**: Implemented.
- **Compiler form:** Emitted by attr/del-attr/set-attr specials when the object expression is a symbol (variable reference). May be lowered to FAST_ATTR_* when the name has a fixed slot.

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

### KOS_OP_VAR_ATTR_REF {#kos_op_var_attr_ref}

- **Style**: EXTRA (VAR, CONST, NOARG, NOARG).
- **Stack**: `[...] → [..., attr_ref]`. Push attribute reference. Equivalent to ATTR_REF with object from namespace.
- **Implementation**: `koskari/lib/opcode/VAR_ATTR_REF.c`

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
- **Status**: Implemented.
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


### 13.1 Attribute access on fast locals (FAST_ATTR_*)

These opcodes perform attribute get, set, clear, or reference on an object fetched from the fast local slot. The object is not on the stack; it is resolved via `KosNamespace_FastDeref(ns, slot)`. Each uses EXTRA schema: FASTVAR (slot), CONST (attr name).

- **Status**: Implemented.
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

### KOS_OP_FAST_ATTR_REF {#kos_op_fast_attr_ref}

- **Style**: EXTRA (FASTVAR, CONST, NOARG, NOARG).
- **Stack**: `[...] → [..., attr_ref]`. Push attribute reference for object in fast slot.
- **Implementation**: `koskari/lib/opcode/FAST_ATTR_REF.c`

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

## 15.1 Reference dereference

### KOS_OP_DEREF

- **Style**: NOARG.
- **Status**: Implemented.
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
- **Status**: Implemented.
- **Stack**: `[..., v_0, ..., v_{n-1}] → [..., tuple]`. Pop n elements and push a tuple of them.
- **Example:** Build empty args. Covered by `tests/lib/opcode/test_arg_opcodes.py`.

| index | opcode    | arg | arg view | comment |
|------:|-----------|-----|----------|---------|
| 0     | BUILD_ARGS| 0   | 0        | stack: [..., ()] |
| 1     | RETURN    | 1   | 1        | |

---

### KOS_OP_BUILD_FUNCTION

- **Style**: COUNT. Arg = number of keyword default values (0 when no keyword defaults).
- **Status**: Implemented.
- **Stack**: For arg = 0: `[..., fspec] → [..., function]`. For arg > 0: `[..., val_1, ..., val_N, names_tuple, fspec] → [..., function]`. The VM pops fspec, then (when arg > 0) pops the names tuple and N values via PopTailDictNames (TOS = names, below it the N default values in order); builds a defaults dict and creates the function from the spec. Build function from function spec (e.g. from constant pool).
- **Invariants**: TOS (after positionals) is function spec; when arg > 0, names_tuple must be a tuple of N names and the stack must have N values below it.

**Example:** Build function from spec in constant pool. Pool: 0 = function_spec. Covered by `tests/lib/test_ceval.py`: `test_build_function_returns_function`, `test_build_function_returned_function_is_callable`.

| index | opcode       | arg | arg view | comment |
|------:|--------------|-----|----------|---------|
| 0     | CONST        | 0   | 0 (fspec)| stack: [..., fspec] |
| 1     | BUILD_FUNCTION | 0 | 0        | stack: [..., function] |
| 2     | RETURN       | 1   | 1        | |

---

### KOS_OP_BUILD_KWDS

- **Style**: PAIR_COUNT. Arg = number of keyword pairs.
- **Status**: Implemented.
- **Stack**: `[..., k_0, v_0, ..., k_{n-1}, v_{n-1}] → [..., dict]`. Pop 2*opa elements as key,value pairs and push a dict.

### KOS_OP_BUILD_KWDS_NAMES

- **Style**: CONST. Arg = constant pool index of names tuple.
- **Status**: Implemented.
- **Stack**: `[..., v_0, ..., v_{n-1}] → [..., dict]`. Pop n values, map names[i] → value[i], push dict. Names tuple from constant pool; values from stack (val_0 deepest, val_{n-1} at TOS).

### KOS_OP_COMBINE_ARGS

- **Style**: COUNT. Arg = count of sequences to combine.
- **Status**: Implemented.
- **Stack**: `[..., t_0, ..., t_{m-1}] → [..., tuple]`. Concatenate the m sequences (each via iteration) into a single tuple. Non-tuple values are converted with PySequence_Tuple when m == 1.

### KOS_OP_COMBINE_KWDS

- **Style**: COUNT. Arg = count of dicts to merge.
- **Status**: Implemented.
- **Stack**: `[..., d_0, ..., d_{m-1}] → [..., dict]`. Merge the m dicts (later overwrites earlier on key clash) and push result.

## 17. Calls

### KOS_OP_CALL

- **Style**: COUNT. Arg = number of positional arguments.
- **Status**: Implemented.
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
- **Status**: Implemented.
- **Stack**: `[..., callable, pos_0, ..., pos_{N-1}, val_0, ..., val_{M-1}, names_tuple] → [..., result]`. TOS = names_tuple (tuple of M keyword names); below it the M keyword values in the same order as names; below that the N positional args; then callable. TailPtr for callable + args + kwnames; validate names tuple; kos_callable_vcall; DropNoErr. No tuple or dict allocation.
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
- **Status**: Implemented.
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

### KOS_OP_CALL_VAR {#kos_op_call_var}

- **Style**: EXTRA. Schema: VAR (name cpool idx), COUNT (npos), NOARG, NOARG.
- **Status**: Implemented.
- **Stack**: `[..., pos_0, ..., pos_{N-1}] → [..., result]`. Callable is loaded from frame namespace by name (`frame_ns_deref`); not on stack. KosCallable: steal args then KosContext_Call. Plain Python: TailPtr + PyObject_Vectorcall + DropNoErr.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

---

### KOS_OP_CALL_FAST {#kos_op_call_fast}

- **Style**: EXTRA. Schema: FASTVAR (slot index), COUNT (npos), NOARG, NOARG.
- **Status**: Implemented.
- **Stack**: Same as CALL_VAR. Callable is loaded from fast local slot via `KosNamespace_FastDeref`; not on stack.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

---

### KOS_OP_CALL_VAR_NAMES {#kos_op_call_var_names}

- **Style**: EXTRA. Schema: VAR (name cpool idx), CONST (kw names tuple cpool idx), COUNT (npos), NOARG.
- **Status**: Implemented.
- **Stack**: `[..., pos_0, ..., pos_{N-1}, kwd_val_0, ..., kwd_val_{M-1}] → [..., result]`. Callable from namespace by name; kw names tuple from constant pool. Same call semantics as CALL_NAMES.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

---

### KOS_OP_CALL_FAST_NAMES {#kos_op_call_fast_names}

- **Style**: EXTRA. Schema: FASTVAR (slot index), CONST (kw names tuple cpool idx), COUNT (npos), NOARG.
- **Status**: Implemented.
- **Stack**: Same as CALL_VAR_NAMES. Callable from fast slot via `KosNamespace_FastDeref`.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

---

### KOS_OP_CALL_VAR_STAR {#kos_op_call_var_star}

- **Style**: EXTRA. Schema: VAR (name cpool idx), STAR (bitflags: 1 = *args, 2 = **kwargs), NOARG, NOARG.
- **Status**: Implemented.
- **Stack**: Same as CALL_STAR minus callable. `[..., pos, kwd]` or subsets per flags. Callable from namespace by name. Pop per flags, ensure pos is tuple, call via KosContext_CallArgs.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

---

### KOS_OP_CALL_FAST_STAR {#kos_op_call_fast_star}

- **Style**: EXTRA. Schema: FASTVAR (slot index), STAR (bitflags), NOARG, NOARG.
- **Status**: Implemented.
- **Stack**: Same as CALL_VAR_STAR. Callable from fast slot via `KosNamespace_FastDeref`.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

---

### KOS_OP_CALL_ATTR {#kos_op_call_attr}

- **Style**: EXTRA. Schema: CONST (attr name), COUNT (npos), NOARG, NOARG.
- **Status**: Implemented.
- **Stack**: `[..., obj, arg_0, ..., arg_{N-1}] → [..., result]`. Attr name from constant pool (EXTRA row). Resolve `obj.name` via `kos_resolve_named_attr`; then invoke. Unbound class method (plain PyFunction/KosFunction from MRO): call `fn(obj, *args)`. Bound or instance-dict callable: call `callable(*args)`. Resolver return: -1 err, 0 = send self, 1 = omit self.
- **Invariants**: Stack has at least 1 + N elements (obj, N args). Resolved attribute must be callable or TypeError.


**Implementation notes** (koskari/lib/opcode/CALL_ATTR.c, eval.c):

- **Stack arrangement**: `ptr = KosStackBlock_TailPtr(*stack, count)` where `count = n + 1`. Index layout: `ptr[0]` = obj, `ptr[1..count-1]` = positional args. Attr name from `KosCode_GetConstant(code, row.a0)`. Stack is consumed via `KosStackSteal_Tail` (KosCallable path) or `KosStackBlock_DropNoErr` (plain Python path).

- **`kos_resolve_named_attr` return value**: -1 = error (exception set); 0 = send self (unbound class method — caller invokes `fn(obj, *args)`); 1 = omit self (bound method, instance dict, data descriptor result, etc. — caller invokes `callable(*args)`).

- **Invocation paths**:
  - **KosCallable** (KosFunction, KosBoundMethod, etc.): `KosStackSteal_Tail(&steal, count, stack)` to own args; if `send_self == 0` then `args = &steal.vals[1]`, `nargs = n + 1`; else `args = (n > 0) ? &steal.vals[2] : NULL`, `nargs = n`. Then `KosContext_Call(ctx, out_ev, resolved, args, nargs, NULL, 0)`; `KosStackSteal_Free(&steal)`.
  - **Plain Python**: `PyObject_Vectorcall(resolved, ...)` with same arg layout; then `kos_push_or_await(ctx, out_ev, result)`.

- **Implemented functions** (eval.c): `kos_resolve_named_attr` (static; full resolution, cache placeholder for Phase 3); `is_plain_instance_method` (KosFunction/PyFunction); `descr_is_data` (tp_descr_set != NULL); `instance_dict_get` (via `_PyObject_GetDictPtr`). Invocation: `KosContext_Call`, `kos_push_or_await`, `KosStackSteal_Tail` / `KosStackSteal_Free` (stack.h/stack.c).

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

- **Style**: EXTRA. Schema: CONST (attr name), CONST (kwarg names tuple), COUNT (npos), NOARG.
- **Status**: Implemented.
- **Stack**: `[..., obj, pos_0, ..., pos_{N-1}, kwd_val_0, ..., kwd_val_{M-1}] → [..., result]`. Resolve `obj.name` via `kos_resolve_named_attr`; then call like CALL_NAMES. The kwarg names tuple and attr name are in the constant pool (EXTRA row), not on the stack.
- **Compiler form:** Emitted by `compile/call-attr` when the call has keyword arguments and no `*:` or `**:`.

---

### KOS_OP_CALL_FAST_ATTR {#kos_op_call_fast_attr}

- **Style**: EXTRA. Schema: FASTVAR (slot index for `obj`), CONST (attr name), COUNT (npos), NOARG.
- **Status**: Implemented.
- **Stack**: `[..., arg_0, ..., arg_{N-1}] → [..., result]`. Same resolution and invocation rules as [`KOS_OP_CALL_ATTR`](#kos_op_call_attr), but `obj` is **not** on the stack: it is loaded from the current frame namespace via `KosNamespace_FastDeref` (slot `a0`). Requires a namespace that supports fast locals (`KosNamespace_SupportsFast`). When `send_self=0` (unbound method), uses `KosStackSteal_TailPrepend` to prepend `obj` to the stolen args; when `send_self=1`, uses `KosStackSteal_Tail` for `N` args only.
- **Implementation** (koskari/lib/opcode/CALL_FAST_ATTR.c): Borrowed `obj` from fast slot — no `Py_DECREF(obj)` on control paths (contrast `KOS_OP_CALL_VAR_ATTR`).
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

---

### KOS_OP_CALL_VAR_ATTR {#kos_op_call_var_attr}

- **Style**: EXTRA. Schema: VAR (name cpool index for `obj`), CONST (attr name), COUNT (npos), NOARG.
- **Status**: Implemented.
- **Stack**: Same as `KOS_OP_CALL_FAST_ATTR`. `obj` is obtained with `frame_ns_deref(ctx, var_name)` (owned reference); the handler balances refcounts with `Py_DECREF(obj)` on exit paths.
- **Implementation** (koskari/lib/opcode/CALL_VAR_ATTR.c): Otherwise identical control flow to `KOS_OP_CALL_FAST_ATTR` / `KOS_OP_CALL_ATTR` (resolve via `kos_resolve_named_attr`, KosCallable vs plain Python, `TailPrepend` when `send_self=0`).
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

---

### KOS_OP_CALL_FAST_ATTR_NAMES {#kos_op_call_fast_attr_names}

- **Style**: EXTRA. Schema: FASTVAR (slot index for obj), CONST (attr name), CONST (kwarg names tuple), COUNT (npos).
- **Status**: Implemented.
- **Stack**: `[..., pos_0, ..., pos_{N-1}, kwd_val_0, ..., kwd_val_{M-1}] → [..., result]`. Obj is loaded from fast slot `a0` via `KosNamespace_FastDeref`; not on stack. One fewer element than CALL_ATTR_NAMES. Resolve and call like CALL_ATTR_NAMES. When `send_self=0` (unbound method), uses `KosStackSteal_TailPrepend` to prepend obj to args; when `send_self=1`, uses `KosStackSteal_Tail`.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

---

### KOS_OP_CALL_VAR_ATTR_NAMES {#kos_op_call_var_attr_names}

- **Style**: EXTRA. Schema: VAR (name/cpool for obj), CONST (attr name), CONST (kwarg names tuple), COUNT (npos).
- **Status**: Implemented.
- **Stack**: Same as CALL_FAST_ATTR_NAMES. Obj is loaded from frame namespace by name (`frame_ns_deref`) instead of fast slot.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

---

### KOS_OP_CALL_ATTR_STAR {#kos_op_call_attr_star}

- **Style**: EXTRA. Schema: CONST (attr name), COUNT (opa bitflags: 1 = has *args, 2 = has **kwargs), NOARG, NOARG.
- **Status**: Implemented.
- **Stack**: `obj.name(*args, **kwargs)`. Stack layout per opa flags (bottom … TOS): opa=0 → `[obj]`; opa=1 → `[obj, pos]` (pos = *args); opa=2 → `[obj, kwd]` (kwd = **kwargs); opa=3 → `[obj, pos, kwd]`. Pop in reverse order (kwd then pos then obj when both star forms are present). `pos` is ensured to be a tuple (coerced via PySequence_Tuple if needed). Resolve obj.name via `kos_resolve_named_attr`; when `send_self=0`, prepend `obj` to the positional tuple before the call. Call via `KosContext_CallArgs`.
- **Invariants**: attr name in constant pool. opa bits: 1 = has *args, 2 = has **kwargs.

---

### KOS_OP_CALL_VAR_ATTR_STAR {#kos_op_call_var_attr_star}

- **Style**: EXTRA. Schema: VAR (name cpool index for `obj`), CONST (attr name), COUNT (opa bitflags: 1 = has *args, 2 = has **kwargs), NOARG.
- **Status**: Implemented.
- **Stack**: Same semantics as [`KOS_OP_CALL_ATTR_STAR`](#kos_op_call_attr_star), but `obj` is **not** on the stack; it is loaded with `frame_ns_deref(ctx, var_name)` (owned reference; handler decrements on exit paths). Stack layout per opa (bottom … TOS): opa=0 → `[]`; opa=1 → `[pos]`; opa=2 → `[kwd]`; opa=3 → `[pos, kwd]` with `kwd` at TOS (same relative order as CALL_ATTR_STAR without the receiver). Pop order and `opa` handling match [`KOS_OP_CALL_VAR_STAR`](#kos_op_call_var_star) combined with CALL_ATTR_STAR; resolve via `kos_resolve_named_attr`, prepend `obj` to tuple args when `send_self=0`, call via `KosContext_CallArgs`.
- **Invariants**: `var_name` and attr name in constant pool; same opa bit meanings as CALL_ATTR_STAR.
- **Implementation** (koskari/lib/opcode/CALL_VAR_ATTR_STAR.c): Parallel to CALL_ATTR_STAR with VAR receiver semantics (see [`KOS_OP_CALL_VAR_ATTR`](#kos_op_call_var_attr)).
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

---

### KOS_OP_CALL_FAST_ATTR_STAR {#kos_op_call_fast_attr_star}

- **Style**: EXTRA. Schema: FASTVAR (slot index for `obj`), CONST (attr name), COUNT (opa bitflags), NOARG.
- **Status**: Implemented.
- **Stack**: Same stack layout and `opa` behavior as [`KOS_OP_CALL_VAR_ATTR_STAR`](#kos_op_call_var_attr_star). `obj` comes from `KosNamespace_FastDeref(ns, slot)` (slot `a0`); requires `KosNamespace_SupportsFast`. Pop/resolve/call semantics match CALL_ATTR_STAR. Same refcount convention as [`KOS_OP_CALL_FAST_ATTR`](#kos_op_call_fast_attr): no `Py_DECREF(obj)` on control paths (contrast [`KOS_OP_CALL_VAR_ATTR_STAR`](#kos_op_call_var_attr_star)).
- **Invariants**: Same opa and tuple/`pos` rules as CALL_ATTR_STAR; fast slot must be in range and bound.
- **Implementation** (koskari/lib/opcode/CALL_FAST_ATTR_STAR.c): Parallel to CALL_FAST_ATTR / CALL_ATTR_STAR.
- **Compiler form:** Not emitted by the compiler; available for manual bytecode construction.

---

### KOS_OP_TAIL_CALL

- **Style**: COUNT. Arg = number of positional arguments.
- **Status**: Implemented.
- **Stack**: Same as CALL: `[..., callable, arg_1, ..., arg_n]`. Extracts args via PopTailPtr (small counts) or PopTail (large) before branching; opcode owns storage, DECREFs at end. For KosCallable: tail install recycles the frame. For Plain Python: `kos_dyn_pop_frame` when parent exists, then PyObject_Vectorcall, kos_push_or_await. Returns **`KOS_OP_POST_CONTROL()`** (legacy **`KOS_OP_RES_CONTROL_RETURNED`**) when continuing (triggers **`kos_dynnode_on_control_returned`** for wind thunks).
- **Execution model**: When the callable is a KosExpression or KosFunction, the VM replaces the current frame (tail install). For all other callable types, the opcode drops the current frame when parent exists, calls the callable, and pushes the result.
- **Invariants**: Stack has at least 1 + arg elements. KosExpression allows no arguments (arg must be 0); otherwise TypeError.
- **Compiler form:** Emitted by the compile/call special when the call is in tail position (`tc` true).

**Example:** Tail call with 0 args (replace frame with callee). Pool: 0 = fn.

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 0     | CONST    | 0   | 0 (fn)   | stack: [..., callable] |
| 1     | TAIL_CALL| 0   | 0        | replace frame; run callee. stack: [...,] (new frame) |

---

### KOS_OP_TAIL_CALL_NAMES {#kos_op_tail_call_names}

- **Style**: COUNT. Arg = N (number of positional arguments).
- **Status**: Implemented.
- **Stack**: Same layout as CALL_NAMES: `[..., callable, pos_0, ..., pos_{N-1}, val_0, ..., val_{M-1}, names_tuple]`. Extracts via PopTailPtr (small) or PopTail (large); opcode owns storage, DECREFs at end. KosCallable: kos_callable_tail_vcall. Plain Python: kos_dyn_pop_frame when parent exists, PyObject_Vectorcall, kos_push_or_await. Returns **`KOS_OP_POST_CONTROL()`** (legacy **`KOS_OP_RES_CONTROL_RETURNED`**) when continuing.

---

### KOS_OP_TAIL_CALL_STAR {#kos_op_tail_call_star}

- **Style**: STAR. Arg = bitflags (1 = has *args, 2 = has **kwargs).
- **Status**: Implemented.
- **Stack**: Same layout as CALL_STAR: `[..., callable, pos, kwd]`. Pop order and ensure_tuple as CALL_STAR. KosCallable: kos_callable_tail_call. Plain Python: kos_dyn_pop_frame when parent exists, PyObject_Call, kos_push_or_await. Returns **`KOS_OP_POST_CONTROL()`** (legacy **`KOS_OP_RES_CONTROL_RETURNED`**) when continuing. Same replace-frame semantics as TAIL_CALL.

---

## 18. Block control

### KOS_OP_PUSH_FOREACH

- **Style**: NOARG. Argument unused; break target is derived at runtime from the extent table (`pop_pc + 1`).
- **Status**: Implemented.
- **Stack**: Pops iterable (TOS) and loop_result (below TOS); creates iterator; pushes FOREACH node and stack block. ITER_NEXT uses the node's iterator. Break target = `extent.pop_pc + 1` (instruction after the matching POP_FOREACH).
- **Pushes:** KOS_DYN_FOREACH (static extent KOS_EXT_FOREACH; node holds iterator and loop state).
- **Invariants**: A KOS_EXT_FOREACH extent must exist at the PUSH_FOREACH PC; otherwise RuntimeError. Extent kind KOS_EXT_FOREACH.
- **Compiler form:** Pre-compile arg = 0 (unused). Break target is not stored in the instruction; it is derived at runtime via `kos_extent_find(KOS_EXT_FOREACH)` → `ext->pop_pc + 1`.

**Example:** for x in [1,2,3]: body. Stack at entry: [..., loop_result, iterable]. Pool: 0 = None, 1 = [1,2,3].

| index | opcode       | arg | arg view | comment |
|------:|--------------|-----|----------|---------|
| 0     | CONST        | 0   | 0 (None) | stack: [..., loop_result] |
| 1     | CONST        | 1   | 1 ([1,2,3]) | stack: [..., loop_result, iterable] |
| 2     | PUSH_FOREACH | 0   | 0        | pop both; push FOREACH region; break_pc = pop_pc+1 from extent table |
| 3     | ITER_NEXT    | 7   | 7 (-> 7) | next from region; else → 7. stack: [..., x] |
| 4     | POP          | 1   | 1        | body: consume x |
| 5     | JUMP         | 3   | 3 (-> 3) | loop back |
| 6     | POP_FOREACH  | 0   | 0        | |
| 7     | LABEL        | —   | —        | break / exhausted (= pop_pc+1) |

---

### 18.1 Context managers (with / dyn-with)

Context managers (`with` / `dyn-with`) at the language level: [with](../specials/with.md), [dyn-with](../specials/dyn-with.md). Summary below.

### KOS_OP_PUSH_WITH {#kos_op_push_with}

- **Style**: CONST. Arg = constant pool index of binding name (symbol/string) or sentinel for no binding.
- **Status**: Implemented (single-shot with; sync and async __enter__/__exit__; VM auto-awaits when enter returns awaitable).
- **Stack before:** `[..., mgr]`. **Stack after:** body runs in a fresh operand stack block.
- **Behaviour:** Pop `mgr`; resolve and cache enter/exit; call enter (auto-await if needed); on success bind enter result if name given; push new stack block; look up WITH extent at current PC, compute `resume_pc = ext->pop_pc + 1`; push `KOS_DYN_WITH` with `resume_pc` stored on the node. On enter failure, no node or block is installed; exception propagates.
- **Pushes:** KOS_DYN_WITH (single-shot; transfer barrier). Node stores `resume_pc` (instruction after POP_WITH) for use when exit swallows an exception.
- **Invariants:** Node owns one operand stack block; stack restoration is structural (block pop), not height-mark. A WITH extent must exist at the PUSH_WITH PC; otherwise RuntimeError.

### KOS_OP_POP_WITH {#kos_op_pop_with}

- **Style**: NOARG (argument unused in current `opcodes.incl`).
- **Status**: Implemented (sync and async exit; VM auto-awaits when exit returns awaitable).
- **Behaviour:** Call exit (no-exception form); auto-await if needed; pop owned stack block; pop dyn node; push `None`. When exit returns an awaitable: step via `kos_with_exit_start_await`; on sync completion (r==0) call `kos_complete_pending_with_exit` to consume the exit result and push `None`; on suspend (r==1) return KOS_EV_AWAIT; on resume, revive calls `kos_complete_pending_with_exit` so the with-expression value is always `None` (exit return is never left on stack). On exception unwind (via no-region path or dyn_pop_top_with): call exit(exc_info); if swallow then clear exception, pop block, pop node, push `None`, set frame PC to `KosDynWith_RESUME_PC(node)` (the instruction after POP_WITH), resume; else pop block, pop node, re-raise.
- **Invariants:** Top of chain must be KOS_DYN_WITH. With-expression value is always `None`; exit return is used only for swallow logic and is never the value seen by RETURN.
### KOS_OP_PUSH_DYN_WITH {#kos_op_push_dyn_with}

- **Style**: CONST. Arg = constant pool index of binding name or sentinel for no binding (same as PUSH_WITH).
- **Status**: Implemented (multi-shot dyn-with; sync and async enter/exit; VM auto-awaits when enter returns awaitable).
- **Stack / behaviour:** Same as PUSH_WITH except pushes KOS_DYN_DYN_WITH. Node participates in continuation transfer plans (wind/unwind on non-local transfer).
- **Pushes:** KOS_DYN_DYN_WITH.

### KOS_OP_POP_DYN_WITH {#kos_op_pop_dyn_with}

- **Style**: NOARG.
- **Status**: Implemented (sync and async exit; VM auto-awaits when exit returns awaitable).
- **Behaviour:** Same as POP_WITH for normal and exception unwind. Pops KOS_DYN_DYN_WITH.

---
### KOS_OP_PUSH_COLLECT

- **Style**: CONST. Arg = constant pool index of NamespaceSpec.
- **Status**: Implemented.
- **Stack**: Does not consume the stack. Loads NamespaceSpec from constant pool at
  arg, creates CollectorNamespace, installs as frame ns, pushes KOS_DYN_COLLECT,
  then pushes a stack block. The body runs in this block; expression results (e.g.
  None from compile/define) stay in the block and are discarded on POP_COLLECT.
- **Pushes:** KOS_DYN_COLLECT (static extent KOS_EXT_COLLECT; stores saved ns for
  restore on POP_COLLECT), then a stack block for the body.
- **Invariants**: Extent kind KOS_EXT_COLLECT. Frame namespace must be KosNamespace.

---

### KOS_OP_POP_COLLECT

- **Style**: NOARG. Argument unused.
- **Status**: Implemented.
- **Stack**: Verifies dyn_top is KOS_DYN_COLLECT. Restores frame ns, drops the stack
  block (discarding body expression results), pops the dyn node, **pushes the
  collected namespace** onto the value stack. `[..., pos] → [..., pos, ns]`.
- **Invariants**: Top of chain must be KOS_DYN_COLLECT.

---

### KOS_OP_BUILD_CLASS

- **Style**: NOARG. Argument unused.
- **Status**: Implemented.
- **Stack**: Expects `[..., pos, ns]` with TOS = ns, TOS-1 = pos. Pops ns, pos.
  pos = (name, bases) or (name, bases, metaclass). Builds the class via
  `kos_build_class` (namespace materialization, optional explicit metaclass,
  derived/composite metaclass, `__prepare__`, merge, metaclass call); pushes
  resulting class.
- **Compiler form:** Build pos with BUILD_ARGS; use with_collector_namespace for
  PUSH_COLLECT/body/POP_COLLECT; then BUILD_CLASS. See [compile/class](../specials/compile-class.md).

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
- **Status**: Implemented.
- **Stack**: Pushes LOOP node and stack block. Break/continue use `break_pc` (derived at runtime) and `continue_pc` (next instruction after PUSH_LOOP). Initializes node result to `None`.
- **Pushes:** KOS_DYN_LOOP (static extent KOS_EXT_LOOP; plus stack block for loop body).
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
- **Status**: Implemented.
- **Constant pool**: NSPEC and NAMES are loaded from the pool; they are not placed on the stack by preceding `CONST` opcodes.
- **Stack**: If `opa == 0`, no stack operands for bindings. If `opa > 0`, stack is `[..., val_0, ..., val_{opa-1}]` with TOS = last value; names come from the extra row. The VM binds `names[i]` to `val[i]`, then drops `opa` stack slots.
- **Pushes:** KOS_DYN_NS (static extent KOS_EXT_NS; stores saved namespace for restore on POP_NS).
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
- **Status**: Implemented.
- **Stack**: Pushes TRY node and a KosStackBlock (like PUSH_LOOP). Exception dispatch clears the block before jumping to the handler. POP_TRY drops the block (with PopBlock(1) to promote one value to parent); block must have exactly one value when POP_TRY runs. Node unwind (e.g. BREAK through TRY) also drops the block.
- **Pushes:** KOS_DYN_TRY (static extent KOS_EXT_TRY; handler targets in extent descriptor) and KosStackBlock.
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
- **Stack**: Pushes FINALLY node and a KosStackBlock (like PUSH_TRY). Exception dispatch clears the block before jumping to the finally handler. POP_FINALLY drops the block (with PopBlock(1) to promote one value to parent); block must have exactly one value when POP_FINALLY runs. Node unwind drops the block when control passes through.
- **Pushes:** KOS_DYN_FINALLY (static extent KOS_EXT_FINALLY) and KosStackBlock.
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
- **Status**: Implemented.
- **Stack**: No stack effect. Clears FINALLY region state on non-local exit; does not pop the region (POP_FINALLY does at target). Does not touch the stack block (only disables the region); the block is dropped by POP_FINALLY or unwind.
- **Example:** Exception or unwind path before transferring to a finally handler.

---

### KOS_OP_CLEAR_LOOP {#kos_op_clear_loop}

- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: No stack effect. Disables the top LOOP region (sets `enabled` to 0) and clears the loop stack block. Used on normal exit from a while-loop body (before POP_LOOP), or on iterator exhaustion in patterns that only use LOOP extents.
- **Invariants**: Top of chain must be KOS_DYN_LOOP. Use CLEAR_FOREACH for FOREACH extents.
- **Example:** On `break` from a while loop: CLEAR_LOOP (and possibly CLEAR_TRY) then jump to break_pc; POP_LOOP at target.
- **Implementation**: `koskari/lib/opcode/CLEAR_LOOP.c`

---

### KOS_OP_CLEAR_FOREACH {#kos_op_clear_foreach}

- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: No stack effect. Disables the top FOREACH region (sets `enabled` to 0) and clears the loop stack block. Used when a for-each iterator is exhausted naturally (before POP_FOREACH).
- **Invariants**: Top of chain must be KOS_DYN_FOREACH. Use CLEAR_LOOP for LOOP extents.
- **Implementation**: `koskari/lib/opcode/CLEAR_FOREACH.c`

---

### KOS_OP_CLEAR_TRY {#kos_op_clear_try}

- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: No stack effect. Clears TRY region state on non-local exit; does not touch the stack block; POP_TRY drops the block at the target.
- **Invariants**: Used with structured exception and non-local control flow.

---

### KOS_OP_POP_TRY {#kos_op_pop_try}

- **Style**: NOARG. Argument unused (or 0).
- **Status**: Implemented.
- **Stack**: Promote one value (TOS) from try block to parent via PopBlock(1), then drop the block and pop the region. Block must have exactly one value. Clear exception state before pop.
- **Invariants**: Top of chain must be KOS_DYN_TRY.

---

### KOS_OP_POP_FINALLY {#kos_op_pop_finally}

- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: Same structural behaviour as POP_TRY for the finally block: promote one value via PopBlock(1), drop block, pop region.
- **Invariants**: Top of chain must be KOS_DYN_FINALLY.

---

### KOS_OP_POP_LOOP {#kos_op_pop_loop}

- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: Drop stack block (no promotion); pop LOOP region; push loop result from the region (stored on the region, not from the block).
- **Invariants**: Top of chain must be KOS_DYN_LOOP (paired with PUSH_LOOP).
- **Implementation**: `koskari/lib/opcode/POP_LOOP.c`

---

### KOS_OP_POP_FOREACH {#kos_op_pop_foreach}

- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: Drop stack block (no promotion); pop FOREACH region; push loop result from the region (stored on the region, not from the block).
- **Invariants**: Top of chain must be KOS_DYN_FOREACH (paired with PUSH_FOREACH).
- **Implementation**: `koskari/lib/opcode/POP_FOREACH.c`

---

### KOS_OP_POP_NS {#kos_op_pop_ns}

- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: Restore frame namespace from saved_ns; pop NS node. No stack block.

**Example:** Pool: N/A.

| index | opcode | arg | arg view | comment |
|------:|--------|-----|----------|---------|
| 2     | POP_NS | 0   | 0        | pop KosDynNs; restore saved namespace |

- **Invariants**: Top of chain must be KOS_DYN_NS.

---

## 19. Object protocol

Object protocol opcodes: single Python operation on TOS (and optionally below); pop operands, push result. All **Implemented** unless noted.

- **Comparisons**: OBJECT_EQ, OBJECT_NE, OBJECT_LT, OBJECT_LE, OBJECT_GT, OBJECT_GE (COUNT) — stack `[..., a1, a2, ... aN] → [..., result]`, `N >= 2`.
- **Predicates**: OBJECT_IN, OBJECT_NOT_IN, OBJECT_IS, OBJECT_IS_NOT; OBJECT_IS_INSTANCE (COUNT), OBJECT_IS_SUBCLASS (COUNT).
- **Numeric**: OBJECT_ADD, OBJECT_SUBTRACT, OBJECT_MULTIPLY, OBJECT_POWER, OBJECT_FLOOR_DIVIDE, OBJECT_TRUE_DIVIDE, OBJECT_MODULO, OBJECT_* (bit ops, matrix multiply).
- **General**: OBJECT_ITER, OBJECT_AITER, OBJECT_LEN, OBJECT_NOT, OBJECT_REPR, OBJECT_STR, OBJECT_FORMAT, OBJECT_FORMAT_CONST, OBJECT_TYPE, OBJECT_DIR, OBJECT_VARS, OBJECT_TRUTH, OBJECT_DIVMOD, OBJECT_BIT_INVERT, OBJECT_BIT_LEFT, OBJECT_BIT_RIGHT.

Each `OBJECT_*` opcode in `opcodes.incl` has its own subsection below.

### KOS_OP_OBJECT_EQ {#kos_op_object_eq}

- **Style**: COUNT.
- **Status**: Implemented.
- **Semantics**: Rich-compare chain `a1 == a2 == ... == aN` over `N` stack values (`arg = N`, `N >= 2`). Raises `TypeError` when `arg < 2`.
- **Implementation**: `koskari/lib/opcode/OBJECT_EQ.c`

### KOS_OP_OBJECT_GE {#kos_op_object_ge}

- **Style**: COUNT.
- **Status**: Implemented.
- **Semantics**: Rich-compare chain `a1 >= a2 >= ... >= aN` over `N` stack values (`arg = N`, `N >= 2`). Raises `TypeError` when `arg < 2`.
- **Implementation**: `koskari/lib/opcode/OBJECT_GE.c`

### KOS_OP_OBJECT_GT {#kos_op_object_gt}

- **Style**: COUNT.
- **Status**: Implemented.
- **Semantics**: Rich-compare chain `a1 > a2 > ... > aN` over `N` stack values (`arg = N`, `N >= 2`). Raises `TypeError` when `arg < 2`.
- **Implementation**: `koskari/lib/opcode/OBJECT_GT.c`

### KOS_OP_OBJECT_LE {#kos_op_object_le}

- **Style**: COUNT.
- **Status**: Implemented.
- **Semantics**: Rich-compare chain `a1 <= a2 <= ... <= aN` over `N` stack values (`arg = N`, `N >= 2`). Raises `TypeError` when `arg < 2`.
- **Implementation**: `koskari/lib/opcode/OBJECT_LE.c`

### KOS_OP_OBJECT_LT {#kos_op_object_lt}

- **Style**: COUNT.
- **Status**: Implemented.
- **Semantics**: Rich-compare chain `a1 < a2 < ... < aN` over `N` stack values (`arg = N`, `N >= 2`). Raises `TypeError` when `arg < 2`.
- **Implementation**: `koskari/lib/opcode/OBJECT_LT.c`

### KOS_OP_OBJECT_NE {#kos_op_object_ne}

- **Style**: COUNT.
- **Status**: Implemented.
- **Semantics**: Rich-compare chain `a1 != a2 != ... != aN` over `N` stack values (`arg = N`, `N >= 2`). Raises `TypeError` when `arg < 2`.
- **Implementation**: `koskari/lib/opcode/OBJECT_NE.c`

### KOS_OP_OBJECT_IN {#kos_op_object_in}

- **Style**: NOARG.
- **Status**: Implemented.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_IN.c`

### KOS_OP_OBJECT_NOT_IN {#kos_op_object_not_in}

- **Style**: NOARG.
- **Status**: Implemented.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_NOT_IN.c`

### KOS_OP_OBJECT_IS {#kos_op_object_is}

- **Style**: NOARG.
- **Status**: Implemented.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_IS.c`

### KOS_OP_OBJECT_IS_NOT {#kos_op_object_is_not}

- **Style**: NOARG.
- **Status**: Implemented.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_IS_NOT.c`

### KOS_OP_OBJECT_IS_INSTANCE {#kos_op_object_is_instance}

- **Style**: COUNT.
- **Status**: Implemented.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_IS_INSTANCE.c`

### KOS_OP_OBJECT_IS_SUBCLASS {#kos_op_object_is_subclass}

- **Style**: COUNT.
- **Status**: Implemented.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_IS_SUBCLASS.c`

### KOS_OP_OBJECT_ADD {#kos_op_object_add}

- **Style**: COUNT.
- **Status**: Implemented.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_ADD.c`

### KOS_OP_OBJECT_BIT_AND {#kos_op_object_bit_and}

- **Style**: COUNT.
- **Status**: Implemented.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_BIT_AND.c`

### KOS_OP_OBJECT_BIT_INVERT {#kos_op_object_bit_invert}

- **Style**: NOARG.
- **Status**: Implemented.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_BIT_INVERT.c`

### KOS_OP_OBJECT_BIT_LEFT {#kos_op_object_bit_left}

- **Style**: NOARG.
- **Status**: Implemented.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_BIT_LEFT.c`

### KOS_OP_OBJECT_BIT_OR {#kos_op_object_bit_or}

- **Style**: COUNT.
- **Status**: Implemented.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_BIT_OR.c`

### KOS_OP_OBJECT_BIT_RIGHT {#kos_op_object_bit_right}

- **Style**: NOARG.
- **Status**: Implemented.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_BIT_RIGHT.c`

### KOS_OP_OBJECT_BIT_XOR {#kos_op_object_bit_xor}

- **Style**: COUNT.
- **Status**: Implemented.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_BIT_XOR.c`

### KOS_OP_OBJECT_FLOOR_DIVIDE {#kos_op_object_floor_divide}

- **Style**: COUNT.
- **Status**: Implemented.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_FLOOR_DIVIDE.c`

### KOS_OP_OBJECT_MATRIX_MULTIPLY {#kos_op_object_matrix_multiply}

- **Style**: COUNT.
- **Status**: Implemented.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_MATRIX_MULTIPLY.c`

### KOS_OP_OBJECT_MODULO {#kos_op_object_modulo}

- **Style**: NOARG.
- **Status**: Implemented.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_MODULO.c`

### KOS_OP_OBJECT_MULTIPLY {#kos_op_object_multiply}

- **Style**: COUNT.
- **Status**: Implemented.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_MULTIPLY.c`

### KOS_OP_OBJECT_POWER {#kos_op_object_power}

- **Style**: COUNT.
- **Status**: Implemented.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_POWER.c`

### KOS_OP_OBJECT_SUBTRACT {#kos_op_object_subtract}

- **Style**: COUNT.
- **Status**: Implemented.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_SUBTRACT.c`

### KOS_OP_OBJECT_TRUE_DIVIDE {#kos_op_object_true_divide}

- **Style**: NOARG.
- **Status**: Implemented.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_TRUE_DIVIDE.c`

### KOS_OP_OBJECT_AITER {#kos_op_object_aiter}

- **Style**: NOARG.
- **Status**: Implemented.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_AITER.c`

### KOS_OP_OBJECT_DIR {#kos_op_object_dir}

- **Style**: NOARG.
- **Status**: Implemented.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_DIR.c`

### KOS_OP_OBJECT_DIVMOD {#kos_op_object_divmod}

- **Style**: NOARG.
- **Status**: Implemented.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_DIVMOD.c`

### KOS_OP_OBJECT_ITER {#kos_op_object_iter}

- **Style**: NOARG.
- **Status**: Implemented.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_ITER.c`

### KOS_OP_OBJECT_LEN {#kos_op_object_len}

- **Style**: NOARG.
- **Status**: Implemented.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_LEN.c`

### KOS_OP_OBJECT_NOT {#kos_op_object_not}

- **Style**: NOARG.
- **Status**: Implemented.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_NOT.c`

### KOS_OP_OBJECT_REPR {#kos_op_object_repr}

- **Style**: NOARG.
- **Status**: Implemented.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_REPR.c`

### KOS_OP_OBJECT_STR {#kos_op_object_str}

- **Style**: COUNT.
- **Status**: Implemented.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_STR.c`

### KOS_OP_OBJECT_FORMAT {#kos_op_object_format}

- **Style**: COUNT.
- **Status**: Implemented.
- **Semantics**: format operation. Arg must be 1 or 2.
  - arg=1: pop object, call `PyObject_Format(obj, NULL)`.
  - arg=2: pop rules then object, call `PyObject_Format(obj, rules)`.
  Stack effect: `[..., obj] -> [..., str]` or `[..., obj, rules] -> [..., str]`.
- **Implementation**: `koskari/lib/opcode/OBJECT_FORMAT.c`

### KOS_OP_OBJECT_FORMAT_CONST {#kos_op_object_format_const}

- **Style**: CONST.
- **Status**: Implemented.
- **Semantics**: format operation with constant rules from cpool.
  Pop object, fetch format rules string from constant pool arg, call
  `PyObject_Format(obj, rules_const)`, push resulting string.
  Stack effect: `[..., obj] -> [..., str]`.
- **Implementation**: `koskari/lib/opcode/OBJECT_FORMAT_CONST.c`

### KOS_OP_OBJECT_TRUTH {#kos_op_object_truth}

- **Style**: NOARG.
- **Status**: Implemented.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_TRUTH.c`

### KOS_OP_OBJECT_TYPE {#kos_op_object_type}

- **Style**: NOARG.
- **Status**: Implemented.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_TYPE.c`

### KOS_OP_OBJECT_VARS {#kos_op_object_vars}

- **Style**: NOARG.
- **Status**: Implemented.
- **Semantics**: See [§19 Object protocol](#19-object-protocol) overview.
- **Implementation**: `koskari/lib/opcode/OBJECT_VARS.c`

### 19.1 In-place variable update (VAR_OBJECT_* and FAST_OBJECT_*)

In-place opcodes perform an augmented assignment: fetch the current value from a name (VAR) or slot (FAST), pop the rhs from the stack, apply the corresponding `PyNumber_InPlace*` operation, then store the result back. Stack effect: `[..., rhs] → [...]` (no value pushed; form evaluates to None).

**VAR_OBJECT_* (style VAR):** Arg = constant pool index (name). Fetch obj from frame namespace, pop rhs, `PyNumber_InPlace*(obj, rhs)` (or `PyNumber_InPlacePower(obj, rhs, Py_None)` for POWER), store result back into name.

**FAST_OBJECT_* (style FASTVAR):** Arg = slot index. Fetch obj from fast slot, pop rhs, same inplace operation, store result back into slot.

These opcodes are lowered at compile time: VAR_OBJECT_* may be converted to FAST_OBJECT_* when the name has a fixed slot.

| Opcode | PyNumber call |
|--------|---------------|
| VAR_OBJECT_ADD / FAST_OBJECT_ADD | PyNumber_InPlaceAdd |
| VAR_OBJECT_SUBTRACT / FAST_OBJECT_SUBTRACT | PyNumber_InPlaceSubtract |
| VAR_OBJECT_MULTIPLY / FAST_OBJECT_MULTIPLY | PyNumber_InPlaceMultiply |
| VAR_OBJECT_POWER / FAST_OBJECT_POWER | PyNumber_InPlacePower(obj, rhs, Py_None) |
| VAR_OBJECT_FLOOR_DIVIDE / FAST_OBJECT_FLOOR_DIVIDE | PyNumber_InPlaceFloorDivide |
| VAR_OBJECT_TRUE_DIVIDE / FAST_OBJECT_TRUE_DIVIDE | PyNumber_InPlaceTrueDivide |
| VAR_OBJECT_MODULO / FAST_OBJECT_MODULO | PyNumber_InPlaceRemainder |
| VAR_OBJECT_MATRIX_MULTIPLY / FAST_OBJECT_MATRIX_MULTIPLY | PyNumber_InPlaceMatrixMultiply |
| VAR_OBJECT_BIT_LEFT / FAST_OBJECT_BIT_LEFT | PyNumber_InPlaceLshift |
| VAR_OBJECT_BIT_RIGHT / FAST_OBJECT_BIT_RIGHT | PyNumber_InPlaceRshift |
| VAR_OBJECT_BIT_AND / FAST_OBJECT_BIT_AND | PyNumber_InPlaceAnd |
| VAR_OBJECT_BIT_XOR / FAST_OBJECT_BIT_XOR | PyNumber_InPlaceXor |
| VAR_OBJECT_BIT_OR / FAST_OBJECT_BIT_OR | PyNumber_InPlaceOr |


### KOS_OP_VAR_OBJECT_ADD {#kos_op_var_object_add}

- **Style**: VAR.
- **Status**: Implemented.
- **Semantics**: See §19.1 above (table maps each opcode to `PyNumber_InPlace*`).
- **Implementation**: `koskari/lib/opcode/VAR_OBJECT_ADD.c`

### KOS_OP_VAR_OBJECT_BIT_AND {#kos_op_var_object_bit_and}

- **Style**: VAR.
- **Status**: Implemented.
- **Semantics**: See §19.1 above (table maps each opcode to `PyNumber_InPlace*`).
- **Implementation**: `koskari/lib/opcode/VAR_OBJECT_BIT_AND.c`

### KOS_OP_VAR_OBJECT_BIT_LEFT {#kos_op_var_object_bit_left}

- **Style**: VAR.
- **Status**: Implemented.
- **Semantics**: See §19.1 above (table maps each opcode to `PyNumber_InPlace*`).
- **Implementation**: `koskari/lib/opcode/VAR_OBJECT_BIT_LEFT.c`

### KOS_OP_VAR_OBJECT_BIT_OR {#kos_op_var_object_bit_or}

- **Style**: VAR.
- **Status**: Implemented.
- **Semantics**: See §19.1 above (table maps each opcode to `PyNumber_InPlace*`).
- **Implementation**: `koskari/lib/opcode/VAR_OBJECT_BIT_OR.c`

### KOS_OP_VAR_OBJECT_BIT_RIGHT {#kos_op_var_object_bit_right}

- **Style**: VAR.
- **Status**: Implemented.
- **Semantics**: See §19.1 above (table maps each opcode to `PyNumber_InPlace*`).
- **Implementation**: `koskari/lib/opcode/VAR_OBJECT_BIT_RIGHT.c`

### KOS_OP_VAR_OBJECT_BIT_XOR {#kos_op_var_object_bit_xor}

- **Style**: VAR.
- **Status**: Implemented.
- **Semantics**: See §19.1 above (table maps each opcode to `PyNumber_InPlace*`).
- **Implementation**: `koskari/lib/opcode/VAR_OBJECT_BIT_XOR.c`

### KOS_OP_VAR_OBJECT_FLOOR_DIVIDE {#kos_op_var_object_floor_divide}

- **Style**: VAR.
- **Status**: Implemented.
- **Semantics**: See §19.1 above (table maps each opcode to `PyNumber_InPlace*`).
- **Implementation**: `koskari/lib/opcode/VAR_OBJECT_FLOOR_DIVIDE.c`

### KOS_OP_VAR_OBJECT_MATRIX_MULTIPLY {#kos_op_var_object_matrix_multiply}

- **Style**: VAR.
- **Status**: Implemented.
- **Semantics**: See §19.1 above (table maps each opcode to `PyNumber_InPlace*`).
- **Implementation**: `koskari/lib/opcode/VAR_OBJECT_MATRIX_MULTIPLY.c`

### KOS_OP_VAR_OBJECT_MODULO {#kos_op_var_object_modulo}

- **Style**: VAR.
- **Status**: Implemented.
- **Semantics**: See §19.1 above (table maps each opcode to `PyNumber_InPlace*`).
- **Implementation**: `koskari/lib/opcode/VAR_OBJECT_MODULO.c`

### KOS_OP_VAR_OBJECT_MULTIPLY {#kos_op_var_object_multiply}

- **Style**: VAR.
- **Status**: Implemented.
- **Semantics**: See §19.1 above (table maps each opcode to `PyNumber_InPlace*`).
- **Implementation**: `koskari/lib/opcode/VAR_OBJECT_MULTIPLY.c`

### KOS_OP_VAR_OBJECT_POWER {#kos_op_var_object_power}

- **Style**: VAR.
- **Status**: Implemented.
- **Semantics**: See §19.1 above (table maps each opcode to `PyNumber_InPlace*`).
- **Implementation**: `koskari/lib/opcode/VAR_OBJECT_POWER.c`

### KOS_OP_VAR_OBJECT_SUBTRACT {#kos_op_var_object_subtract}

- **Style**: VAR.
- **Status**: Implemented.
- **Semantics**: See §19.1 above (table maps each opcode to `PyNumber_InPlace*`).
- **Implementation**: `koskari/lib/opcode/VAR_OBJECT_SUBTRACT.c`

### KOS_OP_VAR_OBJECT_TRUE_DIVIDE {#kos_op_var_object_true_divide}

- **Style**: VAR.
- **Status**: Implemented.
- **Semantics**: See §19.1 above (table maps each opcode to `PyNumber_InPlace*`).
- **Implementation**: `koskari/lib/opcode/VAR_OBJECT_TRUE_DIVIDE.c`

### KOS_OP_FAST_OBJECT_ADD {#kos_op_fast_object_add}

- **Style**: FASTVAR.
- **Status**: Implemented.
- **Semantics**: See §19.1 above (table maps each opcode to `PyNumber_InPlace*`).
- **Implementation**: `koskari/lib/opcode/FAST_OBJECT_ADD.c`

### KOS_OP_FAST_OBJECT_BIT_AND {#kos_op_fast_object_bit_and}

- **Style**: FASTVAR.
- **Status**: Implemented.
- **Semantics**: See §19.1 above (table maps each opcode to `PyNumber_InPlace*`).
- **Implementation**: `koskari/lib/opcode/FAST_OBJECT_BIT_AND.c`

### KOS_OP_FAST_OBJECT_BIT_LEFT {#kos_op_fast_object_bit_left}

- **Style**: FASTVAR.
- **Status**: Implemented.
- **Semantics**: See §19.1 above (table maps each opcode to `PyNumber_InPlace*`).
- **Implementation**: `koskari/lib/opcode/FAST_OBJECT_BIT_LEFT.c`

### KOS_OP_FAST_OBJECT_BIT_OR {#kos_op_fast_object_bit_or}

- **Style**: FASTVAR.
- **Status**: Implemented.
- **Semantics**: See §19.1 above (table maps each opcode to `PyNumber_InPlace*`).
- **Implementation**: `koskari/lib/opcode/FAST_OBJECT_BIT_OR.c`

### KOS_OP_FAST_OBJECT_BIT_RIGHT {#kos_op_fast_object_bit_right}

- **Style**: FASTVAR.
- **Status**: Implemented.
- **Semantics**: See §19.1 above (table maps each opcode to `PyNumber_InPlace*`).
- **Implementation**: `koskari/lib/opcode/FAST_OBJECT_BIT_RIGHT.c`

### KOS_OP_FAST_OBJECT_BIT_XOR {#kos_op_fast_object_bit_xor}

- **Style**: FASTVAR.
- **Status**: Implemented.
- **Semantics**: See §19.1 above (table maps each opcode to `PyNumber_InPlace*`).
- **Implementation**: `koskari/lib/opcode/FAST_OBJECT_BIT_XOR.c`

### KOS_OP_FAST_OBJECT_FLOOR_DIVIDE {#kos_op_fast_object_floor_divide}

- **Style**: FASTVAR.
- **Status**: Implemented.
- **Semantics**: See §19.1 above (table maps each opcode to `PyNumber_InPlace*`).
- **Implementation**: `koskari/lib/opcode/FAST_OBJECT_FLOOR_DIVIDE.c`

### KOS_OP_FAST_OBJECT_MATRIX_MULTIPLY {#kos_op_fast_object_matrix_multiply}

- **Style**: FASTVAR.
- **Status**: Implemented.
- **Semantics**: See §19.1 above (table maps each opcode to `PyNumber_InPlace*`).
- **Implementation**: `koskari/lib/opcode/FAST_OBJECT_MATRIX_MULTIPLY.c`

### KOS_OP_FAST_OBJECT_MODULO {#kos_op_fast_object_modulo}

- **Style**: FASTVAR.
- **Status**: Implemented.
- **Semantics**: See §19.1 above (table maps each opcode to `PyNumber_InPlace*`).
- **Implementation**: `koskari/lib/opcode/FAST_OBJECT_MODULO.c`

### KOS_OP_FAST_OBJECT_MULTIPLY {#kos_op_fast_object_multiply}

- **Style**: FASTVAR.
- **Status**: Implemented.
- **Semantics**: See §19.1 above (table maps each opcode to `PyNumber_InPlace*`).
- **Implementation**: `koskari/lib/opcode/FAST_OBJECT_MULTIPLY.c`

### KOS_OP_FAST_OBJECT_POWER {#kos_op_fast_object_power}

- **Style**: FASTVAR.
- **Status**: Implemented.
- **Semantics**: See §19.1 above (table maps each opcode to `PyNumber_InPlace*`).
- **Implementation**: `koskari/lib/opcode/FAST_OBJECT_POWER.c`

### KOS_OP_FAST_OBJECT_SUBTRACT {#kos_op_fast_object_subtract}

- **Style**: FASTVAR.
- **Status**: Implemented.
- **Semantics**: See §19.1 above (table maps each opcode to `PyNumber_InPlace*`).
- **Implementation**: `koskari/lib/opcode/FAST_OBJECT_SUBTRACT.c`

### KOS_OP_FAST_OBJECT_TRUE_DIVIDE {#kos_op_fast_object_true_divide}

- **Style**: FASTVAR.
- **Status**: Implemented.
- **Semantics**: See §19.1 above (table maps each opcode to `PyNumber_InPlace*`).
- **Implementation**: `koskari/lib/opcode/FAST_OBJECT_TRUE_DIVIDE.c`

**Example (VAR_OBJECT_ADD → FAST_OBJECT_ADD):** `(set! x (+ x 1))` compiles to value expr, VAR_OBJECT_ADD name, CONST None; VAR_OBJECT_ADD may lower to FAST_OBJECT_ADD if `x` has a slot.

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

Delimited continuations use a block-based layout: PUSH_PROMPT establishes the
reset and owns the COW stack block for the delimited region; PUSH_SHIFT and
POP_SHIFT delimit the shift block (handler); the continuation is the code
between POP_SHIFT and POP_PROMPT.

### KOS_OP_PUSH_PROMPT

- **Style**: CONST. Arg = prompt_id (constant pool index, post-compile).
- **Status**: Implemented.
- **Stack**: Pushes a new COW stack block (so the reset body has an isolated stack), then pushes the prompt node onto the dynamic chain.
- **Pushes:** KOS_DYN_PROMPT (static extent KOS_EXT_PROMPT; PUSH_SHIFT captures up to this prompt). PROMPT owns the stack block for the delimited region; SHIFT does not create one.
- **Invariants**: PUSH_SHIFT captures up to this prompt. Exception unwinding past a PROMPT drops the stack block and unlinks the node via the HANDLES_EXCEPTIONS capability (no kind-specific check in the unwind loop).
- **Compiler form:** Pre-compile arg may be prompt id value or symbol; post-compile arg = pool index.

**Example:** reset (push prompt). Pool: 0 = prompt_id.

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 0     | PUSH_PROMPT| 0   | 0 (pid)  | push stack block; push prompt onto chain |
| 1     | …          | …   | …        | body before shift (continuation prefix) |
| 2     | PUSH_SHIFT | 0   | 0 (pid)  | capture k; enter shift block with k on stack |
| 3     | …          | …   | …        | shift body (k available) |
| 4     | POP_SHIFT  | 0   | 0        | return from shift without invoking |
| 5     | …          | …   | …        | continuation (body after shift) |
| 6     | POP_PROMPT | 0   | 0        | drop stack block; pop prompt |

---

### KOS_OP_PUSH_SHIFT

- **Style**: CONST. Arg = prompt_id (constant pool index, post-compile).
- **Status**: Implemented.
- **Stack**: Captures continuation from POP_SHIFT+1 to POP_PROMPT; builds an unwind transfer plan to unwind from the current position to the matching prompt (exclusive); runs the plan; pushes the captured continuation onto the prompt's stack (which is now the active stack after PUSH_PROMPT's block push); enters the shift block. Shift body runs with k on stack. SHIFT does not push its own stack block; the PROMPT's block is the active block throughout.
- **Pushes:** KOS_DYN_SHIFT (simple marker; no stack block, no handlers).
- **Invariants**: Capture validated by kos_validate_capture (prompt reachability in chain). Extent KOS_EXT_SHIFT pairs with POP_SHIFT. Stack block is owned by PROMPT, not SHIFT.
- **Compiler form:** Pre-compile arg may be prompt id value/symbol; post-compile arg = pool index.

**Example:** shift k: body. Capture k, run shift body with k on stack. Pool: 0 = prompt_id.

| index | opcode    | arg | arg view | comment |
|------:|-----------|-----|----------|---------|
| 2     | PUSH_SHIFT| 0   | 0 (pid)  | capture k; unwind to prompt; push k (onto prompt's stack); enter shift block |

---

### KOS_OP_POP_SHIFT

- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: Pops TOS as result; builds a transfer plan that unwinds from the current position through the matching SHIFT and PROMPT nodes (TRUNCATE_AND_INJECT step drops both and their stack block), then injects the single result value onto the outer frame's stack (below the PROMPT's block boundary). Handler returns without invoking k.
- **Invariants**: Must be inside a PUSH_SHIFT extent (parent is PROMPT extent). Stack block owned by PROMPT is dropped as part of the TRUNCATE_AND_INJECT step; the injected value lands on the frame's stack below the former block boundary.
- **Compiler form:** No argument.

**Example:** Shift handler returns value without invoking k.

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 4     | POP_SHIFT| 0   | 0        | pop result; unwind through SHIFT+PROMPT; drop stack block; inject result onto outer stack |

---

### KOS_OP_POP_PROMPT

- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: Normal case (no active continuation return): pops TOS as the reset result, drops the PROMPT's COW stack block, unlinks the prompt from the chain, and pushes the result onto the parent stack. Invoked-continuation return case: when a nearest **`KOS_DYN_DELIM`** with mode **`KOS_DYN_DELIM_MODE_POP_PROMPT`** is present (`kos_find_nearest_delim`), pops TOS as result, unwinds continuation frames back to the invocation site per that node’s return metadata, drops the PROMPT's stack block, pushes result, sets PC to post-INVOKE_K, and resumes.
- **Invariants**: Top of chain must be the matching prompt when POP_PROMPT executes. When returning from an invoked delimited continuation (return path recorded on **`KOS_DYN_DELIM`**), POP_PROMPT transfers control back to the call site with the result value instead of exiting normally. Exactly one value is consumed from the PROMPT's stack block and one is injected to the outer stack.
- **Push-return model**: Delimited invoke uses PUSH (graft frames onto current chain). When the continuation runs and hits POP_PROMPT, it consults the **`KOS_DYN_DELIM`** marker installed for that invoke and resumes at the call site with the value on stack.
- **Exception propagation**: If an exception escapes past the PROMPT during unwinding, the `unwind_on_exception` handler (dispatched via `KOS_DYN_CAP_HANDLES_EXCEPTIONS`) drops the stack block and unlinks the PROMPT, then continues unwinding. No POP_PROMPT opcode executes in this path.

**Example:** End of reset body.

| index | opcode    | arg | arg view | comment |
|------:|-----------|-----|----------|---------|
| 6     | POP_PROMPT| 0   | 0        | pop result; drop stack block; pop prompt; push result to outer stack |

---

### KOS_OP_INVOKE_K

- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: Pops value, then continuation (TOS); invokes continuation with value. Uses push model: grafts continuation frames onto current chain (caller stays below); winds PARAM nodes in cont if any; injects value. When continuation hits POP_PROMPT, control returns to the call site with the result. Prompt need not be in the dynamic chain.
- **Invariants**: Continuation must be valid (same evaluator token, usable state). Multi-shot: invocation clones state.

**Example:** Invoke continuation k with value 100. Stack before: [..., 100, k].

| index | opcode  | arg | arg view | comment |
|------:|---------|-----|----------|---------|
| 0     | …       | …   | …        | stack: [..., value, continuation] |
| 1     | INVOKE_K| 0   | 0        | pop value, pop k; invoke k(value) |

---

### KOS_OP_INVOKE_K_VAR

- **Style**: CONST. Arg = constant pool index of name (post-compile).
- **Status**: Implemented.
- **Stack**: Pops value; looks up continuation by name; invokes it with value. Same push model as INVOKE_K; prompt need not be in chain.
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

Unified extent wind/unwind uses `kos_extent_run_wind` and `kos_extent_run_unwind`,
which dispatch by dynamic-chain node kind. See [compile/capture-cc](../specials/compile-capture-cc.md)
and [compile/dyn-wind](../specials/compile-dyn-wind.md).

### KOS_OP_PUSH_WIND

- **Style**: COUNT. Arg = 1 (before only) or 2 (both before and after).
- **Status**: Implemented.
- **Stack**: For arg=2: `[..., before, after]` (before below, after above). Pops thunks; each must be callable or nil. For arg=1: `[..., before]`. Pushes KOS_DYN_WIND extent; runs before thunk on entry (via normal call path; may return KOS_DYN_UNWIND_ASYNC_STARTED).
- **Pushes:** KOS_DYN_WIND (static extent KOS_EXT_WIND).
- **Example:** See extent-wind-unwind.md; thunks are invoked via kos_callable_call.

---

### KOS_OP_POP_WIND

- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: Pops wind from dynamic chain; runs after thunk (if entered and non-null) via kos_dyn_wind_unwind; may return KOS_DYN_UNWIND_ASYNC_STARTED when thunk pushes a frame.
- **Invariants**: Dyn top must be KOS_DYN_WIND; otherwise RuntimeError.

## 23. Parameterize extent

### KOS_OP_PUSH_PARAM

- **Style**: CONST. Arg = constant pool index of names tuple `(name_0, name_1, ...)`.
- **Status**: Implemented.
- **Stack**: `[..., val_0, val_1, ..., val_{n-1}]` → `[]`. Values consumed; names come from constant.
- **Pushes:** KOS_DYN_PARAM (extent kind KOS_EXT_PARAM). Builds dict from names and values, enters param extent.
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

### KOS_OP_PARAM

- **Style**: CONST. Arg = constant pool index of name (string) for ParamRef key.
- **Status**: Implemented.
- **Stack**: `[]` → `[ParamRef]`. Pushes ParamRef for the given key (e.g. for `(use-param X)`).
- **Invariants**: Constant at arg must be hashable (typically PyUnicode).
- **Example:** `(use-param x)` compiles to PARAM with constant `"x"`, then binds the result.

---

### KOS_OP_POP_PARAM

- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: No change.
- **Pops:** KOS_DYN_PARAM from dynamic chain. Restores domain param bindings.
- **Invariants**: Dyn top must be KOS_DYN_PARAM; otherwise RuntimeError.
- **Example:** See PUSH_PARAM table.

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


