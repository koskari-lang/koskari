# Koskari DynNode Event Model (Working Draft)

This document summarizes a proposed event-oriented model for dynamic nodes in the Koskari evaluator. The goal is to reduce evaluator complexity by making the context responsible only for boundary routing, while individual dynamic nodes handle structured control behavior through a small set of lifecycle events.

---

# Node Activity Definitions

## push(node)

Structurally link a node onto the dynchain and make it the current active node.

* Minimal, local environment setup: e.g. push stack block, push namespace, allocate frame resources.
* Purely structural; no user code invocation, no suspension, no transfer.
* Cheap and predictable (O(1) in the common case).
* Does not imply semantic entry beyond making the node active.

## wind(node)

Notify a node that control is entering its active extent.

* Side-effect driven: invokes user code (before-thunk, `__enter__`, parameterize bindings).
* May suspend (e.g. async context manager enter) or initiate a new transfer.
* Paired with `push` on construct entry; also occurs during control transfer re-entry.

Typical uses:

* `dynamic-wind` before-thunks
* installing ambient state
* resource acquisition

## resume(node)

Notify an already-active node that normal computation has resumed under it.

Key properties:

* Used when returning to an active node after a subordinate operation finishes
* Node remains installed on the dynchain
* Execution should continue under this node
* Resumption payload (send value, await result) is delivered via stack convention: the evaluator pushes the value to TOS before redispatches; the node consumes from TOS

Examples:

* returning from a delegated iteration step
* resuming a `yield-from` driver node

## handle(node, event)

Offer a control transit to the node.

The node may:

* consume the event
* transform it
* defer it
* pass it upward unchanged

Typical events handled:

* `RETURN`
* `RAISE`
* `YIELD`
* `BREAK`
* `CONTINUE`
* `SHIFT`

Examples:

* `try/except` catching exceptions
* `finally` altering return values
* generator boundary intercepting yield

Important invariant:

`handle()` does **not** perform structural unlinking. It only decides what happens to the event.

## unwind(node, event?)

Notify a node that control is leaving its dynamic extent.

The optional event indicates *why* the unwind is occurring (RETURN, RAISE, YIELD, BREAK, CONTINUE, SHIFT), so nodes can react appropriately. Transfer-aware nodes (e.g. `dynamic-wind`) rely on this to run after-thunks; interceptor nodes may treat different events differently.

Typical uses:

* `dynamic-wind` after-thunks
* resource teardown
* state restoration

Usually triggered during control transfer.

## pop(node)

Structurally unlink a node from the dynchain.

Normally preceded by `unwind`, but logically distinct.

* `unwind` = semantic exit
* `pop` = structural removal

## free(node)

Destroy node storage.

This should only occur when no external ownership remains.

Generator nodes are expected to defer freeing until the owning generator object is destroyed.

---

# Evaluator Event Composition

## Normal Construct Entry

Evaluator performs:

push → wind

Push does minimal local setup; wind runs side-effectful entry (and may suspend). Keeping them separate allows push to complete cheaply before any user code runs.

Used when entering constructs such as:

* `with`
* `dynamic-wind`
* handler scopes

---

## Ordinary Completion (No Control Transfer)

Evaluator pushes payload to TOS (if any), then redispatches so control returns to the node. The node consumes from TOS.

Semantically:

resume(node)

Meaning:

* control returned normally
* node remains active
* payload at TOS (node pops as needed)

Examples:

* returning from a delegated step
* resuming a `yield-from` driver

---

## RETURN Transit

Evaluator walks upward along the dynchain.

For each node:

handle(RETURN)

If not consumed:

unwind → pop

If the return reaches a parent operation expecting a result:

push value to TOS, resume(parent_node)

Note: Close (caller shutting down a generator) uses the exception mechanism: GeneratorExit is raised and propagates via RAISE transit.

---

## RAISE Transit

Evaluator walks upward along the dynchain.

For each node:

handle(RAISE)

If not handled:

unwind → pop

If caught:

stop traversal and resume execution under the catching node.

---

## YIELD Transit

Evaluator walks upward until reaching a generator boundary node.

Each node (including the one that emitted the yield) receives:

handle(YIELD)

The emitting node (e.g. yield-from) typically passes through; it has already done its work. Intermediate nodes may inspect or pass. The generator boundary node intercepts.

Generator boundary node behavior:

* captures current dynchain suffix
* stores resumable state
* detaches generator node from chain

Two outcomes:

### Top-level generator

Suspend evaluator and return YIELD value.

### Flattened generator

Push yielded value into awaiting parent frame and continue execution.

Nodes that care about suspension may treat this as a leave/re-enter pair:

unwind before suspend
wind on resume

---

## AWAIT Transit

Evaluator suspends execution without modifying the dynchain.

Properties:

* no unwind
* no pop
* full dynamic chain preserved

Upon resume, execution continues with the same active node.

---

## Targeted Transits (BREAK, CONTINUE, SHIFT)

These transfers target a specific node. The opcode invokes the transfer machinery with the event; the machinery finds the target and orchestrates the walk. The opcode stays thin: it does not traverse the chain or call unwind directly.

### BREAK and CONTINUE

Target: loop node (LOOP or FOREACH). Must be in the same frame; cannot cross the frame boundary.

Flow:

1. Find loop node. If not found (e.g. break/continue outside loop), fail before any unwind.
2. Walk from DYN_TOP toward loop: for each node, unwind(node, event), pop(node).
3. Reach loop: handle(event). Loop consumes BREAK or CONTINUE and acts (exit loop vs. next iteration).
4. resume(loop_node) so execution continues at the loop head (CONTINUE) or after the loop (BREAK).

Nodes receive unwind(node, BREAK) or unwind(node, CONTINUE) and know why they are being torn down. The frame node is the structural boundary: if a BREAK/CONTINUE reached the frame without a valid loop target, the frame would deny it. In practice, the find phase fails first.

### SHIFT (continuation apply)

Target: prompt node, selected by the transfer plan. Cannot cross generator boundary.

The generator boundary node acts as a boundary for SHIFT: handle(SHIFT) would deny (raise) if a shift attempted to cross out of a generator. The node encapsulates this rule; the evaluator stays generic.

---

# Node Behavior Categories

## Lexical Nodes

Example: `with`

Interested in:

push / wind
unwind / pop
handle(RAISE)

Typically ignore `YIELD`.

---

## Transfer-aware Nodes

Example: `dynamic-wind`

Interested in:

wind
unwind

Triggered by control transfers such as:

RETURN
RAISE
YIELD (if suspension should count as leaving the extent)
BREAK
CONTINUE
SHIFT

---

## Interceptor Nodes

Examples:

* `try/except`
* `finally`

Primarily implement:

handle(event)

They may mutate or suppress transits.

---

## Resumable Operation Nodes

Example: `yield-from` super-op

Primarily use:

resume()

and possibly `handle()` for delegation.

These nodes maintain internal state machines across suspension.

---

## Generator Boundary Nodes

Special nodes that:

* intercept `YIELD`
* capture resumable dynchain state
* detach and store suspended execution

Ownership model:

The generator object owns the generator node. The dynchain only borrows it during execution.

Generator nodes intercept:

handle(YIELD)
handle(RETURN)
handle(RAISE)

and detach rather than being popped. They also act as boundaries: handle(SHIFT) denies the transfer (cannot shift across a generator boundary).

---

# Future Node Types (Under Consideration)

Two node types are under consideration and would need to be created or refactored to live within this event model:

**Generator node** — Indicates that the child frame is operating as a generator in the current context. Yields from the child are sent *up* to this node (the generator boundary), not *out* past it. This node would implement the Generator Boundary Node behavior described above.

**YieldFrom node** — Assistant to the YIELD_FROM super-op. Would take over the delegation features (phase, pending action, drive logic) that currently live on the context, allowing the super-op to act purely through the node's generic API (resume, handle) without context-level special cases.

---

# Nodes as Boundaries

Individual nodes are responsible for acting as boundaries. When a transfer would violate a rule, the node denies it in handle().

* **Generator boundary node**: denies handle(SHIFT) — SHIFT cannot cross out of a generator.
* **Frame node**: would deny handle(BREAK) or handle(CONTINUE) if the target was not found in the same frame. Ideally the find phase fails earlier, so this is a structural fallback.

The transfer machinery stays generic: it walks and offers handle(event). Nodes decide whether to consume, pass through, or deny.

---

# Core Invariant

resume() is used for **ordinary continuation within the current extent**.

handle() is used for **control transits crossing the node**.

Maintaining this separation keeps evaluator logic predictable and allows node types to specialize behavior cleanly.

---

# Inbound Events

Inbound signaling from the caller takes two forms:

**Value push**: Push value to TOS, then trigger resume(node). The node consumes from TOS and continues. Used for send value (yield resume) and await result. An inbound indicator may be needed to distinguish cases (e.g. resume-from-yield vs. resume-from-await) so the node knows how to interpret TOS.

**Exception raised**: Simulate the exception at the current point. The exception propagates through the normal RAISE transit machinery (handle, unwind, pop). Throw-in and close (GeneratorExit) both use this path — no separate inbound mechanism. Acting as a normal exception is the correct behavior.
