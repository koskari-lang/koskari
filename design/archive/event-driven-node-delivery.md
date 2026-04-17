# Event-driven yield, return, and raise delivery to dynamic nodes

This document specifies a **target architecture** for Koskari iteration and generator boundaries: **boundary outcomes** (yield, return, exhaustion, raise) are described by a **canonical `KosEvent` on the evaluation context**, and **consumption** (stack effects, PC updates, branch to `for`/`else`, default handling) moves into **dynamic nodes** and **generic dynchain dispatch**. Opcodes and `eval.c` stop encoding feature-specific state in **`KosContext` ad-hoc fields** (notably the pending async-generator slots documented below).

The document is written for implementers: it states goals, proposes concrete mechanisms, details **flow control** (when which node runs), lists **pitfalls**, and requires **backward compatibility** with existing behavior.

**Related documentation**

- [event-driven-eval-event-handlers.md](../interpreter/event-driven-eval-event-handlers.md) — **next** milestone: escape overloaded `RETURN`, `eval_event_*` helpers, RAISE/RETURN/YIELD handler routing
- [iteration-yield-boundaries.md](../interpreter/iteration-yield-boundaries.md) — current opcode map and file locations
- [generic-dynnode-protocol.md](../interpreter/generic-dynnode-protocol.md) — generic vs type-specific bypass; `kos_dynnode_on_control_returned` today
- [dynamic-node-unified-api.md](../interpreter/dynamic-node-unified-api.md) — capability model (partially adopted)
- [evaluator.md](../interpreter/evaluator.md) — eval loop, `eval_phase_post_opcode`, suspension

**Primary code references (current behavior)**

- `koskari/lib/eval.h` — `KosEvent`, `KosEventKind`, `KosContext`
- `koskari/lib/eval.c` — `eval_phase_post_opcode`, `dispatch_boundary_event` (pending async-gen fields removed; state on `KOS_DYN_NEXT` / `KOS_DYN_FOREACH`)
- `koskari/lib/opcode/{NEXT,ITER_NEXT,RETURN,YIELD,YIELD_FROM}.c`
- `koskari/lib/dynchain.c` — `kos_dynnode_on_control_returned`, yield walk / GEN freeze

---

## Agreed decisions (post-draft)

The following choices **narrow** earlier ambiguous wording in this document and should be treated as **authoritative** for implementation:

1. **`KOS_DYN_NEXT` is only for the standalone `NEXT` opcode**  
   It exists solely to represent the consumer-side boundary when **`KOS_OP_NEXT`** attaches a **Koskari async generator** and flattens it into the same eval loop. Payload on that node is **NEXT-shaped** (e.g. arity 1 vs 2, default value for two-arg `NEXT`).

2. **`ITER_NEXT` does not use `KOS_DYN_NEXT`**  
   **`ITER_NEXT`** always runs inside an existing **`PUSH_FOREACH`** / **`KOS_DYN_FOREACH`** region. Iterator-step boundary state, **phase gating** (see §5), and completion policy for that construct live on **`FOREACH`** only. **`compile/for-each`** stays **`PUSH_FOREACH` → `ITER_NEXT`** with **no** extra dyn-node opcodes inserted.

3. **No `PUSH_NEXT` / `POP_NEXT` (or any new opcode for this feature)**  
   The **`KOS_DYN_NEXT`** node is **pushed conditionally at runtime** from the **`NEXT`** opcode handler when a boundary node is needed, and it **self-pops** after it has **accepted** the boundary event (yield/return) and applied stack/PC semantics—there is **no** separate POP opcode for this extent.

4. **Remove `KosContext.pending_agen*` in the same effort**  
   Equivalent state moves to **`KOS_DYN_NEXT`** (NEXT path) and **`KOS_DYN_FOREACH`** (ITER_NEXT path), not to ad-hoc **`KosContext`** fields.

---

## 1. Ultimate goal

1. **Single source of truth for “what happened at the boundary”**  
   The active boundary outcome is always reflected in **`out_ev`** (`KosEventKind` + payload + any parallel exception state required by `KOS_EV_RAISE`). Callers outside the inner loop see the same event model as today for top-level suspension; **in-loop** consumers use the **same** event object rather than inferring meaning from `KosContext.pending_*`.

2. **Node-owned semantics**  
   Rules such as “two-arg NEXT pushes default on exhaustion”, “ITER_NEXT jumps to else PC when the Koskari async gen returns”, “YIELD_FROM advances phase” live in **the responsible `KosDynNode` implementation** (or a dedicated boundary node), not in scattered **opcode** branches that also read **`KosContext`** fields.

3. **Thin opcodes**  
   Opcodes **install** extents, **advance** the abstract machine (stack, PC), and **emit** events. They **do not** implement the full consumer policy for async gen flattening beyond what is strictly the opcode’s syntactic job (e.g. ITER_NEXT still **starts** the step; the **completion** policy moves to nodes).

4. **Thin context**  
   Remove **`pending_agen` / `pending_agen_default` / `pending_agen_n`** from `KosContext` once equivalent state lives on **`KOS_DYN_NEXT`** (standalone **`NEXT`** path) and **`KOS_DYN_FOREACH`** (**`ITER_NEXT`** path), with **one** coherent place per construct (no duplicate mirrors on context).

5. **Reuse the control-returned dispatch pattern**  
   Extend **`kos_dynnode_on_control_returned`** (or split/rename into a clearly named **“boundary delivery”** entry point) so it is invoked for **yield and return** completions from nested Koskari async generators, not only for WIND/WITH **after** `KOS_OP_RES_CONTROL_RETURNED` from RETURN. Eval remains **type-agnostic**: it calls **one** generic function with **`KosContext_DYN_TOP(ctx)`** and **`out_ev`**.

6. **No regressions**  
   Observable behavior for **YIELD**, **YIELD_FROM**, **NEXT**, **ITER_NEXT**, **PUSH_FOREACH**, **RETURN**, transfer plans, continuation capture, and exception propagation must match **pre-refactor** semantics unless a deliberate, separately documented language change is approved.

---

## 2. Problem statement (current technical debt)

### 2.1 Context fields

`KosContext` currently carries **pending async gen** iteration state:

```text
pending_agen
pending_agen_default
pending_agen_n   /* 0 = ITER_NEXT, 1/2 = NEXT */
```

**RETURN.c** and **eval_post_opcode_pending_agen** may consult **context** pending fields and opcode-local state today. That splits **one logical protocol** across **context** and **opcode** instead of **dyn nodes**. The refactor consolidates onto **`KOS_DYN_NEXT`** / **`FOREACH`** as above.

### 2.2 eval special cases

`eval_phase_post_opcode` treats **`KOS_EV_YIELD`** and **`KOS_EV_RETURN`** with **dedicated** paths that call **`eval_post_opcode_pending_agen`**, which encodes NEXT vs ITER_NEXT vs default vs StopIteration **inline**. That is **node policy** expressed in the **evaluator**, which violates the ideology in [generic-dynnode-protocol.md](../interpreter/generic-dynnode-protocol.md).

### 2.3 `kos_dynnode_on_control_returned` is narrow

Today **`kos_dynnode_on_control_returned`**:

- Runs only when **`op_res == KOS_OP_RES_CONTROL_RETURNED`** (post-RETURN delivery to parent, etc.).
- **Ignores** `out_ev` (`(void) out_ev` in the implementation).
- Dispatches only to **WIND** and **WITH** pending phases.

Async-gen **yield/return** from a **flattened** gen does **not** go through this function; it goes through **`eval_post_opcode_pending_agen`** instead.

---

## 3. Target model

### 3.1 Canonical event

**Invariant:** Whenever execution needs to **deliver** a yield value, a return value (including **generator exhaustion** represented as appropriate for the protocol: e.g. `StopIteration` or **NULL payload** per agreed rules), or an exception to a **consumer** that lives behind a **dyn boundary**, the **`KosEvent`** passed through the eval loop must be **fully** filled in before **generic** dispatch runs:

| Kind | Typical payload | Notes |
|------|-----------------|--------|
| `KOS_EV_YIELD` | yielded object | Ownership transfer per existing rules |
| `KOS_EV_RETURN` | returned value or NULL | NULL may mean exhaustion only if explicitly specified; otherwise use a distinct kind or paired flag |
| `KOS_EV_RAISE` | — | Exception in `PyErr`; may combine with payload conventions for wrapped causes |

The exact **NULL vs StopIteration vs separate kind** decision must be **one** documented contract shared by NEXT, FOREACH, and YIELD_FROM nodes.

### 3.2 Generic delivery entry point

Introduce or evolve a **single** generic function (name TBD; options below):

- **`kos_dynnode_on_boundary_event(ctx, node, out_ev)`** — explicit rename, or  
- **`kos_dynnode_on_control_returned(ctx, node, out_ev)`** — keep name, **stop** ignoring `out_ev`

**Signature concept:**

```c
/* Returns KosEvalStep-compatible outcome or int with agreed codes:
   0 / REPROLOGUE / async started / error — match existing wind/with conventions. */
int kos_dynnode_on_boundary_event(
    KosContext *ctx,
    KosDynNode *node,
    KosEvent *out_ev);
```

**Call sites (conceptual):**

1. **Existing:** `eval_phase_post_opcode` when **`op_res == KOS_OP_RES_CONTROL_RETURNED`** (unchanged trigger, but handler must **read** `out_ev` when relevant).
2. **New:** After **`KOS_TRANS_WALK_HANDLE_YIELD`** (and any GEN freeze) when the **consumer** must accept the yield — **before** or **instead of** duplicating logic in **`eval_post_opcode_pending_agen`**.
3. **New:** When **`KOS_EV_RETURN`** is delivered from an **exhausted** nested Koskari async gen in the flattening scenario — same entry point with **`out_ev->kind == KOS_EV_RETURN`**.

Eval **only** passes **`KosContext_DYN_TOP(ctx)`** unless routing rules require a short walk (e.g. after freeze, **NEXT** may be **dyn_top** when pushed by **`NEXT.c`**).

### 3.3 Capability bits

Add capability flags (exact names TBD; align with `KOS_DYN_CAP_*` in `dynchain.h`):

| Cap | Intended meaning |
|-----|------------------|
| **Receives iterator boundary** | Node participates in **generic** boundary delivery for **yield/return** from an attached Koskari async gen (or delegated iterator) when it is the **consumer** extent for that construct. |
| Optional sub-cap | **FOREACH only when ITER_NEXT step is active** — node **ignores** delivery if not in the **awaiting step** phase (§5). |

**Node kinds involved:** **`KOS_DYN_NEXT`** (only **`NEXT`** opcode path), **`KOS_DYN_FOREACH`** (**`ITER_NEXT`** path), **`KOS_DYN_YIELD_FROM`** (delegate already has internal phase; align with the same **event** path where applicable).

Nodes **without** the cap must **not** intercept events intended for another boundary.

---

## 4. `KOS_DYN_NEXT`: runtime push, self-pop, NEXT opcode only

### 4.1 Motivation

Flattening a **nested Koskari async generator** for the standalone **`NEXT`** opcode places a **GEN** frame under the consumer. A **`KOS_DYN_NEXT`** node provides a **stable chain position** for “the boundary that interprets one yield/return” for **`NEXT`**—without new bytecode.

### 4.2 Installation (no new opcodes)

- **`NEXT.c`** **conditionally** allocates and **`kos_dyn_push_next`**s a **`KOS_DYN_NEXT`** when the opcode attaches a Koskari async gen and needs that boundary (same situations that today use **`KosContext.pending_agen*`** for **`NEXT`**).
- There is **no** **`PUSH_NEXT`** / **`POP_NEXT`** opcode and **no** compiler change to emit a NEXT extent.

### 4.3 Self-pop

- When **boundary control returns** to this node (yield or return consumed, stack/PC updated per **`NEXT`** semantics), the node **unlinks itself** from **`dyn_top`** as part of handling—**self-pop**—so the next **`NEXT`** execution can push a fresh node if needed.

### 4.4 Routing vs `ITER_NEXT`

- **`ITER_NEXT`** **never** pushes **`KOS_DYN_NEXT`**. For **`for-each`**, **`dyn_top`** for the iterator-step boundary is **`KOS_DYN_FOREACH`** (with **phase** in §5), not **`KOS_DYN_NEXT`**.
- There is **no** “**FOREACH** and **NEXT** both receive the same yield” ordering problem for **`ITER_NEXT`**, because **`KOS_DYN_NEXT`** is absent on that path.

---

## 5. Flow control: phases and gating

### 5.1 Why gating is mandatory

**FOREACH** wraps a **large** region (entire loop body). **Yields** can occur **only** during **ITER_NEXT**’s nested execution in the **flattened gen**, but **statically** the same region contains **many** PCs. Without **phase**, a delivery hook attached to **FOREACH** could run during **unrelated** suspension points (if any exist in the body) or could confuse **multiple** iterator steps.

**Rule:** A **FOREACH** node that receives boundary events must consult an explicit **phase** (or an equivalent **“step ticket”**):

| Phase (example names) | Meaning |
|------------------------|---------|
| `IDLE` / `BODY` | Normal loop execution; **ignore** iterator-boundary delivery (should not occur for Koskari gen step if compilation is correct). |
| `AWAITING_ITER_STEP` | **ITER_NEXT** has **attached** the nested gen and **suspended** into it; the **next** yield/return event **belongs** to this step. |

Transitions:

- **Enter `AWAITING_ITER_STEP`:** **ITER_NEXT** opcode (or shared helper) when attaching Koskari async gen for **this** loop construct.
- **Leave to `BODY`:** After boundary delivery **successfully** delivers a value to the loop variable path (stack + PC at head of body), or branches to **else** on exhaustion.

### 5.2 State ownership (NEXT vs FOREACH)

- **`KOS_DYN_NEXT`** ( **`NEXT`** opcode only ): owns **NEXT-shaped** per-step state migrated off **`KosContext`** (e.g. **`n`** 1 or 2, **default** for two-arg **`NEXT`**, reference to the **`KosPyAsyncGen`** for this step). It does **not** carry **`n == 0`** / **else-PC** semantics; those belong to **`FOREACH`**.
- **`KOS_DYN_FOREACH`** (**`ITER_NEXT`** ): owns **loop-wide** and **ITER_NEXT-step** state (`iter`, `exhausted`, `iter_next_else_pc`, **phase**, and any per-step fields required for **`ITER_NEXT`** completion), migrated off **`KosContext`**.

### 5.3 YIELD_FROM

**YIELD_FROM** already maintains **phase** in **`KosDynYieldFrom`**. The refactor should:

- **Align** boundary delivery with the same **event** path (§3.2).
- Preserve **delegate close** semantics and **error preservation** (`KOS_DYN_CAP_PRESERVE_ERR_ACROSS_POP`).

---

## 6. Control flow diagrams (conceptual)

### 6.1 Yield from nested Koskari async gen (flattened)

**Path A — standalone `NEXT` opcode (`KOS_DYN_NEXT` on chain):**

```text
NEXT.c attaches gen, pushes KOS_DYN_NEXT when needed
       |
       v
YIELD in gen -> KOS_EV_YIELD in out_ev
       |
       v
walk_handle_yield (+ freeze if applicable)
       |
       v
kos_dynnode_on_boundary_event(dyn_top, out_ev)   # KOS_DYN_NEXT
       |
       +--> push value / self-pop NEXT node / REPROLOGUE
```

**Path B — `ITER_NEXT` / `FOREACH` (no `KOS_DYN_NEXT`):**

```text
ITER_NEXT attaches gen; FOREACH phase = AWAITING_ITER_STEP
       |
       v
YIELD in gen -> KOS_EV_YIELD in out_ev
       |
       v
walk_handle_yield (+ freeze if applicable)
       |
       v
kos_dynnode_on_boundary_event(dyn_top, out_ev)   # KOS_DYN_FOREACH (gated)
       |
       +--> push value / set PC / clear phase  # per FOREACH policy
```

### 6.2 Return / exhaustion from nested gen

```text
RETURN in gen -> KOS_EV_RETURN, payload or exhaustion contract
       |
       v
Same boundary delivery entry point with kind RETURN
       |
       v
KOS_DYN_NEXT (NEXT opcode): default / StopIteration / stack per n
KOS_DYN_FOREACH (ITER_NEXT): else-branch PC, exhausted flag, etc.
```

### 6.3 Exception (raise) from nested gen

If **raise** is in scope for the same cap:

```text
Exception set / KOS_EV_RAISE
       |
       v
Either merge with existing exception unwind, or dedicated path that still
ends at the same node’s "accept" policy (must not double-unwind).
```

**Pitfall:** **Two** unwinds — see §8.

---

## 7. Relationship to `KOS_OP_RES_CONTROL_RETURNED`

Today **`KOS_OP_RES_CONTROL_RETURNED`** means “opcode finished in a way that requires **post-dispatch** hook for **control return**” (RETURN-to-parent, native handler completion, tail-call). **After** the refactor it should remain **one** trigger for the **same** generic delivery function **if** the semantic is unified; alternatively, **map** both:

- **`op_res == KOS_OP_RES_CONTROL_RETURNED`** with filled `out_ev`, and  
- **pending-agen path** with `out_ev` kind YIELD/RETURN  

to **one** internal **`dispatch_boundary_event(ctx, out_ev)`** that:

1. Optionally handles **frame** attach/detach housekeeping **once**.
2. Calls **`kos_dynnode_on_boundary_event(ctx, dyn_top, out_ev)`**.
3. Returns **`KOS_STEP_REPROLOGUE`**, **`STOP_EVENT`**, or error consistently.

---

## 8. Pitfalls to avoid

1. **Dual routing**  
   Do not leave **both** `eval_post_opcode_pending_agen` **and** a new path that **both** push values or **both** advance PC. **One** must win; the other must be deleted or be a **thin** wrapper.

2. **Ignoring `out_ev` again**  
   The current `kos_dynnode_on_control_returned` **drops** the event. The new design **requires** reading **kind** and **payload** for iterator boundaries.

3. **FOREACH without phase**  
   Attaching boundary delivery to **FOREACH** without **AWAITING_ITER_STEP** (or equivalent) will **mis-handle** any future suspension inside the loop body.

4. **Order of handling vs `walk_handle_yield`**  
   **Freeze / install / chain version** updates must stay **consistent** with [freeze-thaw-context-segments.md](../interpreter/freeze-thaw-context-segments.md). If **boundary** runs **before** freeze completes, nodes may see **stale** `dyn_top`.

5. **Ownership of payload**  
   `KosEvent` payload **reference** ownership must match **exactly** one of: **transferred to stack**, **stolen by node**, or **decref’d by generic** path. Document per branch.

6. **NULL payload ambiguity**  
   If **NULL** means both “exhaustion” and “error”, **FOREACH** and **NEXT** will mis-branch. Prefer **explicit** kind or **PyErr** check.

7. **Exception double handling**  
   If **raise** is delivered through both **exception unwind** and **boundary event**, ensure **one** path **owns** the error state.

8. **Continuation capture**  
   **Clone** paths for new node kinds must copy **phase** and **pending** references correctly; **KOS_DYN_FOREACH** and **`KOS_DYN_NEXT`** clone must be tested with **call/cc**-style capture if supported.

9. **Self-pop timing (`KOS_DYN_NEXT`)**  
   Unlink the node **after** stack/PC updates and **`out_ev`** payload ownership are settled; avoid double **`DECREF`** or use-after-pop.

10. **Native handler / `KosDynFrame_NATIVE_HANDLER`**  
   **RETURN** to native parent must **still** receive values; boundary refactor must **not** break the **native handler** short-circuit in **RETURN.c**.

11. **Regression in non-Koskari iterators**  
    **Python** async iterators, sync **next**, and **YIELD_FROM** drivers must keep **identical** observable behavior.

---

## 9. Migration strategy

1. **Inventory** every read/write of **`KosContext_PENDING_AGEN*`** (grep-driven; include any frame-local mirrors if present in tree).
2. **Introduce** node/frame fields that **mirror** semantics **before** deleting context fields.
3. **Route** **`eval_post_opcode_pending_agen`** through **`kos_dynnode_on_boundary_event`** incrementally: first **delegate** to a **switch** that **copies** old behavior per kind; then **move** cases into **node** files.
4. **Add** tests (or extend existing) for: **NEXT** 1-arg vs 2-arg, **ITER_NEXT** else branch, **StopIteration** on 1-arg, **YIELD_FROM** kos async gen delegate, **revive** after yield.

---

## 10. Compatibility checklist (must pass)

- **Flattened Koskari async gen:** yield pushes to **correct** consumer stack; resume with **send** semantics unchanged.
- **ITER_NEXT** exhaustion → **else** PC; **`exhausted`** flag behavior unchanged.
- **RETURN** from callee through **finally**, **with**, **wind** — unchanged.
- **YIELD_FROM** all delegate kinds — unchanged.
- **Exception** propagation through **try/finally** and **prompt** barriers — unchanged.
- **Continuation** / **transfer plan** capture of chains containing **FOREACH** — unchanged.

---

## 11. Open decisions (implementation phase)

- Exact **`KOS_DYN_CAP_*`** names for **NEXT** vs **FOREACH** boundary receive.
- **`KOS_DYN_NEXT`** as a **separate** `KosDynKind` vs overlay (separate kind is the current direction).
- **NULL** vs **`KOS_EV_*`** for exhaustion.
- Whether **`kos_dynnode_on_control_returned`** is **renamed** for clarity or kept with **widened** contract.
- Interaction with a future **`KosDynTypeOps`** table if [dynamic-node-unified-api.md](../interpreter/dynamic-node-unified-api.md) is fully adopted.

**Settled (not open):** no **`PUSH_NEXT`** opcode; **`KOS_DYN_NEXT`** only for **`NEXT`** opcode; **`ITER_NEXT`** on **`FOREACH`** only; **self-pop** for **`KOS_DYN_NEXT`**.

---

## Document history

- **2026-04-16** — Initial design draft for event-driven boundary delivery and node-centric consumption.
- **2026-04-16** — Agreed decisions: **`KOS_DYN_NEXT`** scope (**`NEXT`** opcode only), **`ITER_NEXT`** on **`FOREACH`** only, **no** new opcodes (**runtime push** + **self-pop**), **`KosContext.pending_agen*`** removal; diagrams and §4/§5 updated.
- **2026-04-16** — Linked follow-on **`eval_event_*`** / thin-RETURN design doc (`design/interpreter/event-driven-eval-event-handlers.md`).
- **2026-04-16** — Document moved to **`design/archive/`** (superseded-as-stable snapshot of boundary ideology; active follow-up is **`event-driven-eval-event-handlers`**).
