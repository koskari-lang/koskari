# Switch / equality dispatch opcodes

**Status:** Implemented (archived). The full **`SWITCH_EQ`** family and
**`compile/switch`** lowering are shipped.

**Canonical summary:** [OPCODES.md](../OPCODES.md) §9.1; [SPECIALS.md](../SPECIALS.md)
§ compile/switch.

**User documentation:** [docs/reference/specials/compile-switch.md](../../docs/reference/specials/compile-switch.md);
[docs/reference/vm/families.md](../../docs/reference/vm/families.md) §9.1.

**Tests:** `tests/lib/opcode/test_switch.py`; `tests/compile/specials/test_switch.py`

---

## Implementation ledger

| Piece | Location |
| ----- | -------- |
| Opcodes | `koskari/lib/opcodes.incl` — `SWITCH_EQ`, `SWITCH_EQ_CONST`, `SWITCH_EQ_VAR`, `SWITCH_EQ_FAST`, `SWITCH_EQ_VAR_ATTR`, `SWITCH_EQ_FAST_ATTR` |
| Handlers | `koskari/lib/opcode/SWITCH_EQ*.c` |
| Compiler special | `koskari/compile/specials/conditionals.py` — **`compile/switch`** |
| Peephole fusion | `koskari/compile/compiler.py` — **`_SWITCH_SQUASHES`** in **`compress`** |

---

## Motivation

Multi-way branching on **value equality** against a single discriminant is
awkward with existing opcodes:

- [CHECK_EQ](OPCODES.md#kos_op_check_eq) compares two stacked values and **pops
  both**. If the discriminant must be reused for each arm, the compiler would
  have to re-push it after every failed test, wasting bytecode and complicating
  lowering.

A dedicated family keeps **one stack slot for the scrutinee** across the whole
chain: each arm supplies (or embeds) a key, compares, and either consumes the
scrutinee and falls into the body or discards only the key and jumps forward.

Equality should match **CHECK_EQ** (same underlying comparison semantics).

## Base opcode: SWITCH_EQ

**Style:** `LABEL` (post-compile arg = target pc). Jump locality rules match
other control-flow branches; see [OPCODES.md](OPCODES.md) §1.3 / §1.7.

**Stack convention:** Same order as `CHECK_EQ`: `[..., left, right]` with
**right = TOS** (`left` is the discriminant below the case key).

**Behaviour:**

- If `left == right` (per CHECK_EQ equality): pop **two** values; **fall through**
  to the case body that follows this instruction.
- If `left != right`: pop **one** value (the key on TOS only); **jump** to `arg`
  (the next arm’s prelude, or a shared else label).

**Net stack effect:**

- Match: depth −2.
- Mismatch: depth −1, then control transfers to the label.

**Compiler-shaped lowering (generic keys):**

1. Evaluate discriminant once → stack `[..., s]`.
2. For each non-final arm: evaluate case key → `[..., s, k]`; emit `SWITCH_EQ
   L_next`; emit body; emit `JUMP L_done` (or equivalent merge).
3. Label `L_next`: still `[..., s]` after each failed compare.
4. Else arm: optional `POP 1` if the result must not leak `s`, then body or
   `CONST None` when no else clause (mirroring empty / no-match `compile/cond`).

This is intentionally the **inverse pop pattern** of `CHECK_EQ`: on mismatch,
`CHECK_EQ` has already removed both values; `SWITCH_EQ` keeps the discriminant for
the next comparison.

## Compressed opcodes

Specialized forms avoid loading the key onto the stack when the key is
statically known or a simple load. Stack is always `[..., s]` with **scrutinee =
TOS** before the opcode.

| Opcode | Operand(s) | Match | Mismatch |
|--------|------------|-------|----------|
| SWITCH_EQ_CONST | label + const pool index | pop 1 | jump, pop 0 |
| SWITCH_EQ_VAR | label + var name (cpool) | pop 1 | jump, pop 0 |
| SWITCH_EQ_FAST | label + fast slot index | pop 1 | jump, pop 0 |
| SWITCH_EQ_VAR_ATTR | label + EXTRA row (VAR + attr name, same schema family as VAR_ATTR) | pop 1 | jump, pop 0 |
| SWITCH_EQ_FAST_ATTR | label + EXTRA row (FAST + attr name) | pop 1 | jump, pop 0 |

**Semantics:** Each reads the right-hand side at opcode execution (constant pool
entry, binding, or attribute), compares to `s` using the same equality as
`SWITCH_EQ` / `CHECK_EQ`, then applies the pop/jump contract above.

The compiler **`compress`** pass fuses **`CONST`/`VAR`/`VAR_ATTR` + `SWITCH_EQ`**
into the compressed opcodes via **`_SWITCH_SQUASHES`**.

## Surface syntax: compile/switch

Mirror **compile/cond** ([SPECIALS.md](SPECIALS.md) § compile/cond): clauses in
order, optional `else:`, no match → `None`.

**Symbol:** `compile/switch`

**Syntax:**

```scheme
(compile/switch DISC
  [KEY BODY...]
  [KEY BODY...]
  ...
  [else: BODY...])
```

- **DISC** — evaluated once; its value is the discriminant for every clause.
- **Each clause** — `KEY` is an expression whose value is compared to `DISC`
  with equality. If equal, `BODY...` runs in a sequence context (same as cond’s
  use of `helper_begin` for consequents) and that value is the result of the
  whole form.
- **else:** — optional; if present, run when no `KEY` matched. If absent, the
  form yields `None` (like `compile/cond` with no else).

**Lowering notes:**

- Literal `KEY` forms emit **SWITCH_EQ_CONST** when the compiler can pool the
  constant (via **`compress`** fusion).
- Simple **ref** / **fast** / **attr** case keys use the corresponding compressed
  opcode when safe.
- Arbitrary `KEY` expressions use **CONST** / evaluation + **SWITCH_EQ**.

**Contrast with compile/cond:** `cond` tests arbitrary truthiness per clause;
`compile/switch` compares each key to **one** precomputed discriminant by
equality only.

## References

- [OPCODES.md](../OPCODES.md) — CHECK_EQ, jump locality, finalize pipeline.
- [SPECIALS.md](../SPECIALS.md) — compile/cond and compile/switch behaviour.

# The end.
