# Proposal: Node-owned operand stack (context-current block, all-at-once)

**Status:** Implemented (archived 2026-06-22). Canonical summary:
[interpreter/evaluator.md](../interpreter/evaluator.md) (operand stack authority).

---

## 1. Motivation

 Operand stack state and dynamic chain state have drifted apart. Today,

- `KosStackBlock` chains are threaded with a `back` pointer, and many call sites
  reason about “the parent block” (`KosStackBlock_PARENT`) to cross **logical**
  stack regions.
- Live operand stack ownership is primarily anchored on **`FRAME`**
  (`KosDynNode.u.frame.stack`), while important control boundaries (prompt,
  shift, delim, etc.) are **not** frames.

 Delimited continuations and transfer plans clone or reinstall **nodes** without
 always cloning or restoring a **matching** block image. That produces bugs
 such as unbalanced teardown (“cannot drop root block”) and incorrect TOS when
 the chain and block chain disagree.

 This proposal makes **one** rule: **the active operand stack segment for
 execution is whatever block is referenced from the `KosContext` (`ctx->block`),
 and that pointer is updated only by dynamic node install/uninstall (and tightly
 controlled primitives).** That pointer is **not** the same as the innermost
 dynamic node: many nodes do not introduce a stack extent. Nodes that do
 optionally **own** a `KosStackBlock *` that becomes `ctx->block` while the
 node is installed. There is no separate “frame stack” as the authority for
 “where do I push.”

## 2. Goals

1. **Single source of truth** for “current stack block” on `KosContext`.
2. **Symmetric lifecycle**: installing a node may establish a stack extent;
   uninstalling it restores the previous extent exactly.
3. **No opcode-level block parenting on the evaluator path**: opcodes **push/pop
   `PyObject *` references** via **`ctx->block`**; they do **not** call
   `KosStackBlock_PushBlock` or use `back` to infer a prior region boundary (§5.2).
4. **Node metadata owns stack snapshots** for capture/clone: **clone** copies
   owned blocks; **traverse** visits owned blocks; **free** releases owned
   blocks (with clear ownership rules).
5. **All-at-once** cutover: no long-lived dual paths (frame stack vs context
   stack).

## 3. Non-goals

- Redesigning Python semantics or the bytecode instruction set (except where
  stack access is implemented).
- Micro-optimizing stack storage layout in the first landing (see §10).

## 4. Current state (summary)

- `KosStackBlock` is documented as a **chain** of segments; `back` points toward
  the bottom of the **physical** stack (`stack.h`).
- **`KosDynNode.stack`** (base field, above `u`) owns the operand extent for
  nodes that introduce one; **`KosContext.block`** points at the innermost
  active `KosStackBlock *` and is recomputed after chain changes by
  **`KosEvalState_RefreshDynCaches(es)`** (then **`KosEvalState_SyncFromContext`**
  when the live eval slice must see updated **`frame_top`** / **`code`**).
  Opcodes use **`KosEvalState`** stack helpers bound to that slot.
- Dynamic install/uninstall already runs for many node kinds; teardown must stay
  symmetric so `ctx->block` and the node chain never disagree.
- Continuations / transfer plans may carry `resume_stack` in parallel with chain
  clones.

## 5. Target architecture

### 5.1 Invariants

1. **`ctx->block`** is the **only** pointer used by the evaluator for “current
   operand stack block” during normal interpretation (the **top** of the logical
   stack for the active extent: pushes/peeks use this block, grown in place).
2. **`KosDynNode.stack` (name TBD)** — a new field **above the `union`** on
   `KosDynNode` — is either `NULL` (“this node does not change the current
   stack extent”) or points to a block **owned by this node** for the purpose of
   install/uninstall.
3. **`kos_dyn_install` / `kos_dyn_uninstall` (and `_core`)** maintain a small
   **stack discipline stack** *or* paired save/restore: on install, if
   `node->stack != NULL`, set `ctx->block` to that block and remember the
   previous pointer for uninstall; on uninstall, restore the saved pointer.
4. **Region boundaries are not discovered via `block->back` on the evaluator /
   dynchain path.** Crossing a **logical** extent is always **uninstall/install
   of dyn nodes**, not “drop block until parent exists.”

### 5.2 Option C: `back` reserved for non-evaluator stacks; dynchain blocks are always root segments

 This is the chosen physical model:

- **Keep the `back` field** on `struct KosStackBlock` for **APIs and users that
  still form a linked list of segments** (notably the **`KosStack` native type**
  and tests that treat the value stack as a Python object with multi-segment
  growth via `KosStackBlock_PushBlock` / shrink via `KosStackBlock_DropBlock` /
  `KosStackBlock_PopBlock`).

- **For every block owned by or reachable as `ctx->block` or as `node->stack`
  on the dynamic chain:** maintain the invariant **`back == NULL`**. Logical
  growth uses the **existing in-place path** (`KosStackBlock_PUSH` already grows
  via `KosStackBlock_Grow` / realloc-style expansion). **Do not** call
  `KosStackBlock_PushBlock` to add a linked “overlay” segment for evaluator
  stack regions.

- **Freelist / allocation** for `KosStackBlock` stays as today (`KosStackBlock_New`,
  release paths); Option C does not abandon block pooling.

- **Two worlds, one struct:** the same `KosStackBlock` type serves (1) dynchain /
  `ctx->block` **single-segment chains** (`back` always NULL) and (2) **`KosStack`**
  (and similar) where `back` may be non-`NULL`**. Code must not mix those
  invariants—e.g. deep copy or traverse must handle both, but **evaluator code
  assumes `back == NULL`** for blocks it owns.

### 5.3 Avoiding `DropBlock` / `PopBlock` automatic promotion on the evaluator path

 `KosStackBlock_DropBlock` discards the top segment and moves `*block` to
 `top->back`, which fails with “cannot drop root block” when `back` is `NULL`.
 `KosStackBlock_PopBlock` **moves the top N values into the parent block** before
 dropping the top segment—again requiring a non-`NULL` `back`.

 On the **dynchain / `ctx->block` path**, those operations are **wrong for
 region changes**: region change is **restore `ctx->block` from install state**,
 not drop a segment. For the rare opcode behavior that today relies on “drop
 top segment and promote N refs to the block below,” **do not** use
 `DropBlock`/`PopBlock` with a synthetic parent link. Instead:

- After uninstall/install has pointed `ctx->block` at the correct target block,
  **explicitly** move or copy the needed `PyObject *` slots (with correct
  `INCREF`/`DECREF` discipline) between the source block and the block that is
  now current—**or** add a dedicated helper that transfers N values without
  fabricating a `back` pointer on dynchain-owned blocks.

 This keeps **promotion semantics explicit** and preserves **`back == NULL`**
 invariants for the evaluator.

### 5.4 Frames

- **`u.frame.stack` is removed or made non-authoritative** (dead after cutover).
- Frames keep **PC, namespace, native handler**, etc.
- If a **suspended** frame needs a stack image for introspection or future
  resume, that image is either:
  - the **`stack` field on the frame’s `KosDynNode`** (same as other nodes), or
  - **derived from `ctx->block` at suspend time** stored only on
    structures that own the snapshot (continuation, plan). Prefer **one**
    representation to avoid dual stacks.

## 6. API and opcode surface

### 6.1 Context accessors

- Add **`KosContext_BLOCK(ctx)`** (or equivalent) for **`ctx->block`**, used
  everywhere eval currently uses `KosDynFrame_STACK` / frame’s stack.
- All **`KosStackBlock_PushRef` / `PopRef` / `Peek` / …** call sites in the
  evaluator take **`KosContext *`** (or `KosStackBlock **` passed from ctx**)
  so they never need a frame pointer for stack location.

### 6.2 Opcodes

- Opcodes continue to **push and pop Python references**; implementation changes
  from “get stack from current frame” to **“get stack from context.”**
- Any opcode that today **pushes a new stack region** does so by **installing**
  or constructing a **node** (or uses a shared helper that allocates a block
  and attaches it to the node that is being pushed). It does **not** call
  low-level “push block” on the operand stack independently of the dyn chain.

### 6.3 Install / uninstall

- **`kos_dyn_install`**: after existing kind-specific logic, apply **stack
  binding**: if `node->stack`, bind `ctx->block`.
- **`kos_dyn_uninstall_core`**: before releasing node-owned resources, **unbind**
  stack: restore previous `ctx->block` saved at install.
- **Order**: uninstall must **restore `ctx->block`** before freeing `node->stack`
  (or transfer ownership to another owner), so eval never points at freed memory.

## 7. Cloning, traversal, freeing

| Operation | Rule |
|-----------|------|
| **Clone node / chain segment** | Deep-copy **owned** `node->stack` the same way frames clone `u.frame.stack` today (`KosStackBlock_DeepCopy` or successor). |
| **GC traverse** | Traverse **owned** block from `node->stack` (existing `kos_stackblock_traverse` patterns move from frame-only to per-node). |
| **Free node** | `KosStackBlock_Release` (or chain release) on **owned** `node->stack` when the node is freed, unless ownership was transferred (e.g. moved into a continuation). |

 **Borrowed** `stack` pointers are forbidden in the steady state; if a temporary
 borrow is unavoidable during construction, document it and assert it does not
 escape install.

## 8. Continuations and transfer plans

- **`resume_stack` on `KosContinuation` / `KosTransferPlan`** should be
  reconciled with **node-owned** stacks:
  - Either **eliminate** `resume_stack` in favor of the cloned chain carrying
    all stack state on nodes, or
  - Define **`resume_stack` as a cache** produced from the top segment’s owned
    blocks at capture time (single writer).

- **Capture**: snapshot = **clone chain** + **clone each owned stack on nodes**
  in that segment (same as current frame stack clone, generalized).

- **Invoke**: install cloned nodes; **`ctx->block`** follows install order;
  inject resume value onto the **context** stack block.

## 9. All-at-once migration checklist

Suggested execution order (single branch, no partial “frame stack still works”):

1. **Introduce `ctx->block` and `KosDynNode.stack`**; compile-time assert
   or macro to force one path.
2. **Implement install/uninstall save/restore** for `node->stack`; unit-test with
   trivial synthetic nodes.
3. **Replace all eval stack access** to use context (grep for
   `u.frame.stack`, `KosDynFrame_STACK`, `KosStackBlock_PushBlock` from opcodes,
   etc.).
4. **Remove frame `u.frame.stack`** and update frame constructors/clones.
5. **Delete or rewrite** every path that uses **`KosStackBlock_PARENT`** or
   **`KosStackBlock_DropBlock`** for **evaluator region** teardown; replace with
   uninstall-driven **`ctx->block`** restore and explicit value transfer where
   promotion was previously implicit.
6. **Reconcile continuation / plan** stack fields with node-owned model.
7. **Full test run**: especially `phase_reproducers`, reset/shift nested suite,
   foreach, generators, await/defer, exception unwind.

## 10. Open technical choices

1. **Physical model for dynchain blocks:** **Option C is decided** (§5.2): eval
   blocks always have `back == NULL`; growth in place. **`KosStack`** may keep
   using multi-segment `back` chains.
2. **Where to store “previous `ctx->block`” during install:** thread-local /
   node scratch vs small explicit stack on `KosContext` (depth bounded by dyn
   chain depth).
3. **Native frames / C eval:** ensure they receive `KosContext *` and use
   **`ctx->block`** consistently.
4. **Opcode-by-opcode audit** for any use of `KosStackBlock_DropBlock` /
   `PopBlock`: replace with explicit transfers compatible with §5.3.

## 11. Risks

- **Missed call sites** leave `ctx->block` NULL or stale → immediate
  crashes or refcount bugs; mitigation: central accessor + aggressive grep +
  asserts in debug builds.
- **Double free** between node free and continuation ownership; mitigation:
   explicit ownership transfer rules at capture/invoke.
- **Performance:** extra indirection through context; likely negligible vs
  Python overhead.

## 12. Success criteria

- Nested reset/shift / delimited continuation tests pass without special-case
  “root block” guards.
- No production use of **`KosStackBlock_PARENT`** for prompt/delim/shift
  teardown.
- Single authoritative **`ctx->block`** in the interpreter loop; dynchain-owned
  stack blocks satisfy **`back == NULL`**.

---

*Document status: proposal for implementation planning. Option C (§5.2) is the
intended `stack.c` / evaluator contract for dynamic-chain operand storage.*
