# Tail-call promotion as a compiler bytecode pass

**Status:** Design proposal — not yet implemented.

**Goal:** Remove tail-position awareness from special-form handlers. Specials
always emit plain call opcodes (`CALL`, `CALL_NAMES`, `CALL_STAR`, and their
`CALL_VAR` / `CALL_FAST` / `CALL_ATTR` lowered forms). A dedicated promotion
pass in `Compiler.complete()` rewrites eligible call sites to the existing
`TAIL_CALL*` opcodes using bytecode heuristics.

**Phasing:**

1. **Phase 1 (this document):** Implement the promotion pass; delete the `tc`
   parameter from specials; gate the pass with `Compiler.tco_enabled` (default
   true).
2. **Phase 2 (deferred):** Gate the pass via module pragma instead of
   `tco_enabled`. See [PRAGMA-MODEL.md](PRAGMA-MODEL.md) (`compiler_pass_*`
   flags). No change to the pass algorithm itself.

---

## 1. Problem today

### 1.1 The `tc` flag threads through everything

`Compiler.compile_expr` and `compile_special` pass a boolean `tc` (tail-call
context) into every special handler. Most handlers accept `tc=False` and never
use it. The flag is propagated only so the *last* expression in certain bodies
can reach `compile/call` with `tc=True`:

- `helper_begin` — last form in a body list
- `compile/cond`, `compile/switch` — each branch body
- `compile/and`, `compile/or` — last subexpression
- `compile/let`, `compile/begin`, `compile/class` — body
- `compile/lambda` / `compile/function` — always `True` for the lambda body
- `compile/dyn-wind` thunks — `True` for inner thunk bodies

At compile time, `tc` is ANDed with `Compiler.tco_enabled`:

```python
tc = self.tco_enabled and tc
```

### 1.2 Only `compile/call` acts on `tc`

`koskari/compile/specials/call.py` is the sole emitter of `TAIL_CALL*`
opcodes. When `tc` is true it also changes emission strategy for symbol
callees:

```python
use_var_path = is_symbol(callable) and not tc
```

Non-tail symbol calls use `CALL_VAR` (callable not on stack). Tail symbol calls
load the callable with `VAR` and use `TAIL_CALL` (callable on stack).

`compile/call-attr` accepts `tc` but **never** emits tail opcodes — method
calls in lambda tail position are not tail-optimized today.

### 1.3 Existing `complete()` pipeline

`Compiler.complete()` already runs optimization passes on the instruction
stream before building `Code`:

| Step | Function | Purpose |
|------|----------|---------|
| 1 | `compress()` | Peephole squish; appends `RETURN 1` when missing |
| 2 | `convert_fast()` | Lower `VAR` ops to `FAST` where possible |
| 3 | `demote_tail_calls_if_yields()` | `TAIL_CALL*` → `CALL*` if code contains `YIELD` |

Tail-call *selection* happens too early (at special compile time), not in this
pipeline.

---

## 2. Proposed architecture

### 2.1 Separation of concerns

| Layer | Responsibility |
|-------|----------------|
| **Special forms** | Emit correct *semantics*: argument order, extent
  structure (`PUSH_TRY`, `POP_LOOP`, `JUMP` merge labels), stack cleanup
  (`POP` between `begin` forms). Always use non-tail call opcodes. |
| **Promotion pass** | Decide which call sites are in tail position and rewrite
  to `TAIL_CALL*`. |
| **VM** | Unchanged: `TAIL_CALL*` uses `tp_kos_tail_install` / frame reset per
  [special-call-protocol.md](../api/special-call-protocol.md). |

Specials no longer need to know whether they are compiling the last expression
in a function body.

### 2.2 New pipeline order

Insert promotion after `compress` (so implicit `RETURN 1` is present) and
before `convert_fast`:

```
compress(instr)
→ promote_tail_calls(instr)     # new; skipped when not tco_enabled
→ convert_fast(instr, ...)
→ demote_tail_calls_if_yields(instr)
```

`demote_tail_calls_if_yields` remains mandatory — generators must not use
frame-reuse tail calls.

### 2.3 Enable switch (phase 1)

```python
if self.tco_enabled:
    instr = self.promote_tail_calls(instr)
```

`Compiler.tco_enabled` stays on `__init__` until pragma wiring lands. Tests
that need non-tail behavior keep passing `tco_enabled=False`.

---

## 3. Special-form cleanup

### 3.1 Remove `tc` from all signatures

Delete the `tc` parameter from:

- Every `@special` handler in `koskari/compile/specials/`
- `Compiler.compile_expr`, `compile_special`, `compile_pair`, etc.
- `helper_begin(fragment, body)` — drop the third argument
- `syntax.py` / `CompiledSpecial.compile_impl` protocol

Propagation sites to simplify:

- `helper_begin` — always `full_compile(expr, False)` for non-final forms;
  last form also `False` (pass handles tail)
- `compile/lambda` — `helper_begin(lamb, body_src)` without `True`
- `compile/cond`, `switch`, `and`, `or`, `let`, `begin`, `class`, `exceptions`
  — remove `tc` threading into `helper_begin` / `full_compile`
- `compile/dyn-wind` thunk bodies — `full_compile(body_src)` without `True`

### 3.2 Simplified `compile/call`

Always emit the non-tail opcode family:

| Case | Emit |
|------|------|
| Symbol callee, positional | `CALL_VAR` (or `CALL` after callable on stack if not using var path) |
| General callee, positional | `CALL` |
| Keyword names | `CALL_NAMES` / `CALL_VAR_NAMES` |
| Star args | `CALL_STAR` / `CALL_VAR_STAR` |

Remove all `elif tc:` branches and `KOS_OP_TAIL_CALL*` emissions.

Restore consistent fast paths: `use_var_path = is_symbol(callable)` always
(when applicable), without the `and not tc` guard.

### 3.3 Simplified `compile/call-attr`

Same cleanup: always `CALL_ATTR`, `CALL_ATTR_NAMES`, `CALL_ATTR_STAR`, and
`CALL_VAR_ATTR*` variants. No `tc` parameter. Tail promotion for method calls
becomes a pass concern (see §5.2 — v1 may exclude these opcodes).

### 3.4 What specials still own

Control-flow shape is unchanged. Examples:

- `begin` — `POP 1` between non-final forms
- `cond` — `JUMP_FALSE`, branch body, `JUMP done`, merge label
- `while` — `PUSH_LOOP`, body, `CONTINUE 1`, `POP_LOOP`
- `try` — `PUSH_TRY`, body, `CLEAR_TRY`, `POP_TRY`

The promotion pass must respect these shapes; it does not replace them.

---

## 4. Promotion pass algorithm

### 4.1 Opcode families (phase 1 scope)

**In scope** — existing `CALL` / `TAIL_CALL` analogues only:

| Source | Target |
|--------|--------|
| `KOS_OP_CALL` | `KOS_OP_TAIL_CALL` |
| `KOS_OP_CALL_NAMES` | `KOS_OP_TAIL_CALL_NAMES` |
| `KOS_OP_CALL_STAR` | `KOS_OP_TAIL_CALL_STAR` |

**Out of scope for v1** — no `TAIL_*` opcode exists yet; leave unchanged
until a later phase adds tail variants (or defines expansion rules):

- `CALL_VAR`, `CALL_VAR_NAMES`, `CALL_VAR_STAR`
- `CALL_FAST`, `CALL_FAST_NAMES`, `CALL_FAST_STAR`
- `CALL_ATTR`, `CALL_ATTR_NAMES`, `CALL_ATTR_STAR`
- `CALL_VAR_ATTR`, `CALL_VAR_ATTR_NAMES`, `CALL_VAR_ATTR_STAR`
- `CALL_FAST_ATTR*`

A later phase may add `TAIL_CALL_VAR` / `TAIL_CALL_FAST` / `TAIL_CALL_ATTR`
families, or teach the pass to expand `CALL_VAR` → load + `CALL` before
promotion. v1 intentionally matches today's practical coverage for
stack-style `CALL*` (which is what tail `tc=True` already used for
non-symbol callees, and symbol callees after the `VAR` load path).

### 4.2 Tail position is not only `CALL, RETURN`

`compress()` appends `RETURN 1` when the fragment does not end in `RETURN`.
The easy case:

```
... CALL n, RETURN 1
→ ... TAIL_CALL n
```

(delete the `RETURN 1` when promotion succeeds — `TAIL_CALL` does not fall
through)

Other shapes the pass must handle (parity with current `tc` tests):

| Source pattern | Bytecode sketch |
|----------------|-----------------|
| Lambda body, single call | `CALL n` … `RETURN 1` |
| `begin` last form | `CALL n` … `RETURN 1` |
| `cond` / `switch` branch | `CALL n`, `JUMP merge` … `merge:` … `RETURN 1` |
| `and` / `or` last arm | `CALL n`, `LABEL done` … `RETURN 1` |

**Must not promote:**

| Pattern | Reason |
|---------|--------|
| `CALL n`, `CONTINUE 1` | Loop iteration, not function return |
| `CALL n`, `BREAK 1` | Loop exit via break protocol |
| `CALL n`, `YIELD` / `YIELD_FROM` | Generator (also caught by demote pass) |
| `CALL n`, `CLEAR_TRY`, … | Try normal-completion protocol |
| Non-final `begin` form | Followed by `POP 1` |

### 4.3 Analysis approach

Treat promotion as **backward reachability** from returning exits:

1. Identify **exit sites**: `RETURN` (arg 0 or 1), and optionally merge
   points that feed a unique function exit with stack depth 1.
2. Walk backward through the instruction stream (respecting `JUMP`,
   `JUMP_TRUE`, `JUMP_FALSE`, `JUMP_AND`, `JUMP_OR`, `SWITCH_EQ` targets).
3. Track **stack depth** and **extent depth** (simulate `PUSH_*` / `POP_*`
   using the same pairs as `koskari/lib/bytecode.c` `extent_pairs`).
4. A `CALL` / `CALL_NAMES` / `CALL_STAR` at PC *p* is promotable when:
   - Its single stack result reaches a returning exit without incompatible
     intervening stack ops (`POP`, second `CALL`, etc.)
   - Extent state at *p* allows tail frame reuse (see §4.4)
   - The call opcode is in the v1 family (§4.1)

Reuse or mirror `extent_pairs` from `bytecode.c` rather than duplicating the
table ad hoc.

### 4.4 Extent constraints

Extents fall into two categories for promotion:

**Dynamic unwind on `RETURN`** (wind, with, param, …): tail-calling from
inside is generally valid; `RETURN` / `TAIL_CALL` unwinds the dyn chain.
`kos_dynframe_reset_for_tail` recycles the frame object without tearing down
dyn nodes above it.

**Bytecode completion protocol** (try, finally, loop): normal exit requires
explicit opcodes (`CLEAR_TRY`, `POP_TRY`, `CONTINUE`, `POP_LOOP`, …). Do
**not** promote a `CALL` that is followed by these cleanup opcodes on the path
to the function return. This matches today's conservative choice: try bodies
compile with `full_compile(try_expr, False)` even when the surrounding
function is in tail position.

When in doubt, prefer **no promotion** (correctness over maximal TCO).

### 4.5 Implementation location

Add `promote_tail_calls(self, instr) -> list` on `Compiler`, alongside
`compress`, `convert_fast`, and `demote_tail_calls_if_yields`. Pure function
over `(op, arg)` pairs; no fragment access required. Unit tests can feed
hand-built instruction lists without compiling source.

---

## 5. Interaction with other passes

### 5.1 `compress`

Must run **before** promotion so that:

- Implicit `RETURN 1` is present for single-expression bodies
- Peephole rules (`CONST None, RETURN 1` → `RETURN 0`, etc.) are already applied

### 5.2 `convert_fast`

Runs **after** promotion. Only `CALL` / `TAIL_CALL` triplet exist at stack
style; there is no `TAIL_CALL_FAST*`. Fast lowering applies to `VAR` loads and
`CALL_VAR*` opcodes that were **not** promoted in v1.

### 5.3 `demote_tail_calls_if_yields`

Runs **after** promotion. Unchanged: any `YIELD` in the stream forces
`TAIL_CALL*` back to `CALL*`.

---

## 6. Testing

### 6.1 Regression

Existing tail-position tests in `tests/compile/specials/test_call.py` should
pass unchanged in behavior (still expect `TAIL_CALL` in lambda tail bodies).
Update only if the test inspects intermediate non-tail form before
`complete()`.

### 6.2 New tests

- **Pass unit tests** — hand-built `(op, arg)` lists: promote / do-not-promote
  cases from §4.2
- **`tco_enabled=False`** — stream stays `CALL*`
- **Generator** — promotion then demote leaves `CALL*`
- **Cond / begin** — branch tail calls still promoted

### 6.3 Optional improvement (not required for v1)

Once `CALL_ATTR*` promotion exists, add tests for tail method calls in lambda
bodies (currently missed).

---

## 7. Implementation checklist

1. Implement `promote_tail_calls` with v1 opcode scope and extent-aware analysis
2. Wire into `complete()` between `compress` and `convert_fast`
3. Gate with `tco_enabled`
4. Remove `tc` from compiler dispatch and all specials
5. Simplify `call.py` (and `call-attr`) per §3
6. Simplify `helper_begin` and all `tc` propagation call sites
7. Run `tests/compile/specials/test_call.py` and full `tox -e test` compile phase
8. Update `docs/reference/vm/opcodes/kos-op-tail-call.md` — emitted by
   promotion pass, not by `compile/call` directly

---

## 8. Future work

| Item | Notes |
|------|-------|
| `TAIL_CALL_VAR` / `TAIL_CALL_FAST` / `TAIL_CALL_ATTR` opcodes | Extend promotion to fast and attr call families |
| Pragma gate | Replace `tco_enabled` with `compiler_pass_tail_call` per [PRAGMA-MODEL.md](PRAGMA-MODEL.md) |
| `call-attr` tail coverage | Automatic once attr tail opcodes or expansion rules exist |
| Self-hosted compiler | `.ki` specials drop `tc` in parallel when bootstrap is ported |

---

## See also

- [special-call-protocol.md](../api/special-call-protocol.md) — VM tail install
- [vector-call.md](../interpreter/vector-call.md) — `CALL` / `TAIL_CALL` stack layout
- [PRAGMA-MODEL.md](PRAGMA-MODEL.md) — deferred pass gating
- [bytecode-finalization.md](../interpreter/bytecode-finalization.md) — long-term finalization pipeline (orthogonal)
- `koskari/compile/compiler.py` — `complete()`, `compress`, `convert_fast`
- `koskari/lib/bytecode.c` — `extent_pairs`, extent derivation
