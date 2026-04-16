# Event-driven yield, return, and raise delivery to dynamic nodes

This document specifies a **target architecture** for Koskari iteration and generator boundaries: **boundary outcomes** (yield, return, exhaustion, raise) are described by a **canonical `KosEvent` on the evaluation context**, and **consumption** (stack effects, PC updates, branch to `for`/`else`, default handling) moves into **dynamic nodes** and **generic dynchain dispatch**. Opcodes and `eval.c` stop encoding feature-specific state in **`KosContext` ad-hoc fields** (notably the pending async-generator slots documented below).

The document is written for implementers: it states goals, proposes concrete mechanisms, details **flow control** (when which node runs), lists **pitfalls**, and requires **backward compatibility** with existing behavior.

**Related documentation**

- [iteration-yield-boundaries.md](iteration-yield-boundaries.md) — current opcode map and file locations
- [generic-dynnode-protocol.md](generic-dynnode-protocol.md) — generic vs type-specific bypass; `kos_dynnode_on_control_returned` today
- [dynamic-node-unified-api.md](dynamic-node-unified-api.md) — capability model (partially adopted)
- [evaluator.md](evaluator.md) — eval loop, `eval_phase_post_opcode`, suspension

**Primary code references (current behavior)**

- `koskari/lib/eval.h` — `KosEvent`, `KosEventKind`, `KosContext` pending async-gen fields (`pending_agen`, `pending_agen_default`, `pending_agen_n`)
- `koskari/lib/eval.c` — `eval_phase_post_opcode`, `eval_post_opcode_pending_agen`
- `koskari/lib/opcode/{NEXT,ITER_NEXT,RETURN,YIELD,YIELD_FROM}.c`
- `koskari/lib/dynchain.c` — `kos_dynnode_on_control_returned`, yield walk / GEN freeze

---

## 1. Ultimate goal

1. **Single source of truth for “what happened at the boundary”**  
   The active boundary outcome is always reflected in **`out_ev`** (`KosEventKind` + payload + any parallel exception state required by `KOS_EV_RAISE`). Callers outside the inner loop see the same event model as today for top-level suspension; **in-loop** consumers use the **same** event object rather than inferring meaning from `KosContext.pending_*`.

2. **Node-owned semantics**  
   Rules such as “two-arg NEXT pushes default on exhaustion”, “ITER_NEXT jumps to else PC when the Koskari async gen returns”, “YIELD_FROM advances phase” live in **the responsible `KosDynNode` implementation** (or a dedicated boundary node), not in scattered **opcode** branches that also read **`KosContext`** fields.

3. **Thin opcodes**  
   Opcodes **install** extents, **advance** the abstract machine (stack, PC), and **emit** events. They **do not** implement the full consumer policy for async gen flattening beyond what is strictly the opcode’s syntactic job (e.g. ITER_NEXT still **starts** the step; the **completion** policy moves to nodes).

4. **Thin context**  
   Remove **`pending_agen` / `pending_agen_default` / `pending_agen_n`** from `KosContext` once equivalent state lives on **frames** and/or **nodes** (some of this already exists on `KosDynFrame` as `PENDING_AGEN*` — the goal is **one** coherent place, not both context and frame).

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

Parallel state also exists on **`KosDynFrame`** (`KosDynFrame_PENDING_AGEN`, etc.). **RETURN.c** and **eval_post_opcode_pending_agen** branch on **both** context and frame depending on path. That splits **one logical protocol** across three layers (context, frame, opcode).

### 2.2 eval special cases

`eval_phase_post_opcode` treats **`KOS_EV_YIELD`** and **`KOS_EV_RETURN`** with **dedicated** paths that call **`eval_post_opcode_pending_agen`**, which encodes NEXT vs ITER_NEXT vs default vs StopIteration **inline**. That is **node policy** expressed in the **evaluator**, which violates the ideology in [generic-dynnode-protocol.md](generic-dynnode-protocol.md).

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

Eval **only** passes **`KosContext_DYN_TOP(ctx)`** unless a separate design requires walking **down** from GEN to the **NEXT** boundary node (see §4.2).

### 3.3 Capability bits

Add capability flags (exact names TBD; align with `KOS_DYN_CAP_*` in `dynchain.h`):

| Cap | Intended meaning |
|-----|------------------|
| **Receives iterator boundary** | Node participates in **generic** boundary delivery for **yield/return** from an attached Koskari async gen (or delegated iterator) when installed as the **consumer** extent. |
| Optional sub-cap | **FOREACH only when ITER_NEXT step is active** — node **ignores** delivery if not in the **awaiting step** phase (§5). |

**Candidate node kinds:** new **NEXT** dyn node (§4.1), **FOREACH** (extended), **YIELD_FROM** (already has internal phase; may align caps with explicit delivery).

Nodes **without** the cap must **not** intercept events intended for an inner boundary node.

---

## 4. Structural options: NEXT as a dynamic node

### 4.1 Motivation

Flattening a **nested Koskari async generator** into the same eval loop places a **GEN** frame under a **consumer** frame. The compiler may insert a **NEXT** dynamic node **below** the GEN (above the consumer’s loop body) so that:

- There is a **stable chain position** for “the boundary that interprets one yield/return”.
- **FOREACH** does not need to guess from **PC alone** whether a yield belongs to **ITER_NEXT** vs arbitrary code in the loop body.

### 4.2 Routing: who is `dyn_top`?

When the nested gen yields:

- **Typical case:** **`walk_handle_yield`** runs; GEN may **freeze** and hand off to the **consumer** chain. The **top** node after transfer should be the **NEXT** boundary node **if** the flattening strategy inserts it.
- **Ordering:** If **FOREACH** and **NEXT** both have “receive” caps, **innermost** (closest to GEN) should run **first**: **NEXT** consumes and materializes stack/PC; **FOREACH** may only see **derived** state if needed.

If **NEXT** is **not** materialized for some compilation modes, **FOREACH** (with cap + **phase**) must still enforce **ITER_NEXT-only** behavior (§5).

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

### 5.2 NEXT-only flattening

When a **NEXT** dyn node exists:

- It may own **`pending_agen_n`**, **default**, and **link to `KosPyAsyncGen`**, **migrated off context**.
- **FOREACH** may **only** store **loop-wide** state (`iter`, `exhausted`, `iter_next_else_pc`, …) without **pending_agen** duplication.

### 5.3 YIELD_FROM

**YIELD_FROM** already maintains **phase** in **`KosDynYieldFrom`**. The refactor should:

- **Align** boundary delivery with the same **event** path (§3.2).
- Preserve **delegate close** semantics and **error preservation** (`KOS_DYN_CAP_PRESERVE_ERR_ACROSS_POP`).

---

## 6. Control flow diagrams (conceptual)

### 6.1 Yield from nested Koskari async gen (flattened)

```text
[Consumer frame: foreach body schedule]
       |
       v
ITER_NEXT / NEXT attaches gen frame, runs gen
       |
       v
YIELD in gen -> KOS_EV_YIELD in out_ev
       |
       v
walk_handle_yield (+ freeze if applicable)
       |
       v
kos_dynnode_on_boundary_event(dyn_top, out_ev)   # NEXT or FOREACH (gated)
       |
       +--> push value / set PC / suspend to host  # per node policy
```

### 6.2 Return / exhaustion from nested gen

```text
RETURN in gen -> KOS_EV_RETURN, payload or exhaustion contract
       |
       v
Same boundary delivery entry point with kind RETURN
       |
       v
NEXT or FOREACH: else-branch PC, default push, or StopIteration
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
   **Freeze / install / chain version** updates must stay **consistent** with [freeze-thaw-context-segments.md](freeze-thaw-context-segments.md). If **boundary** runs **before** freeze completes, nodes may see **stale** `dyn_top`.

5. **Ownership of payload**  
   `KosEvent` payload **reference** ownership must match **exactly** one of: **transferred to stack**, **stolen by node**, or **decref’d by generic** path. Document per branch.

6. **NULL payload ambiguity**  
   If **NULL** means both “exhaustion” and “error”, **FOREACH** and **NEXT** will mis-branch. Prefer **explicit** kind or **PyErr** check.

7. **Exception double handling**  
   If **raise** is delivered through both **exception unwind** and **boundary event**, ensure **one** path **owns** the error state.

8. **Continuation capture**  
   **Clone** paths for new node kinds must copy **phase** and **pending** references correctly; **KOS_DYN_FOREACH** and any **NEXT** clone must be tested with **call/cc**-style capture if supported.

9. **Native handler / `KosDynFrame_NATIVE_HANDLER`**  
   **RETURN** to native parent must **still** receive values; boundary refactor must **not** break the **native handler** short-circuit in **RETURN.c**.

10. **Regression in non-Koskari iterators**  
    **Python** async iterators, sync **next**, and **YIELD_FROM** drivers must keep **identical** observable behavior.

---

## 9. Migration strategy

1. **Inventory** every read/write of **`KosContext_PENDING_AGEN*`** and **`KosDynFrame_PENDING_AGEN*`** (grep-driven).
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

- Exact **cap** names and whether **NEXT** is always a **separate** `KosDynKind` or an **overlay** on an existing node.
- **NULL** vs **`KOS_EV_*`** for exhaustion.
- Whether **`kos_dynnode_on_control_returned`** is **renamed** for clarity or kept with **widened** contract.
- Interaction with a future **`KosDynTypeOps`** table if [dynamic-node-unified-api.md](dynamic-node-unified-api.md) is fully adopted.

---

## Document history

- **2026-04-16** — Initial design draft for event-driven boundary delivery and node-centric consumption.
