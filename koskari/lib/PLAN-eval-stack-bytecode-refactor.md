# Plan: trace_stack, frame caching, CopyForWrite macro, extent tables

Implementation plan for four refactors in koskari native code: conditional
trace_stack, current-frame caching in the dispatch loop, macro-based
CopyForWrite refcount fast path, and O(1) bytecode extent lookup tables.


## 1. trace_stack: compile out when disabled

**Goal:** Remove trace_stack and its call sites from the hot path when
`KOS_STACK_TRACE` is 0 so the dispatch loop has no debug overhead.

**Files:** `koskari/lib/eval.c`, `koskari/lib/debug.h` (optional).

**Details:**

- In `eval.c`, the function `trace_stack` (lines ~239–251) and its two
  call sites (lines ~1755 and ~1790) are currently always compiled.
- `KOS_STACK_TRACE` is defined in `debug.h` (default 0). `SAY`/`SAYF` are
  already no-ops when `VERBOSE_DEBUG` is 0, but `trace_stack` is always
  present and called.

**Changes:**

1. **eval.c**
   - Wrap the entire `trace_stack` function in:
     ```c
     #if KOS_STACK_TRACE
     static void trace_stack(const char *prefix, KosStackBlock *stack) {
       ...
     }
     #endif
     ```
   - At the two call sites (before and after the opcode dispatch), wrap
     the calls:
     ```c
     #if KOS_STACK_TRACE
         trace_stack("  before", CURRENT_STACK(ctx));
     #endif
     ```
     and similarly for `"  after "`.
   - Optionally wrap the `SAYF` that logs `pc`/`op`/`arg` in the loop in
     `#if VERBOSE_DEBUG` so that when both are 0 the loop body has no
     debug calls at all.

**Result:** With `KOS_STACK_TRACE` 0 (default), no trace_stack code in the
loop and no function call; debug builds can set `KOS_STACK_TRACE` to 1
(e.g. via CFLAGS or a debug-only block in the build).

**Testing:** Build with and without `-DKOS_STACK_TRACE=1`; run tests; confirm
trace output only when enabled.


---


## 2. Current-frame caching in the dispatch loop

**Goal:** Make `CURRENT_FRAME(ctx)` (and thus `CURRENT_STACK`, `CURRENT_CODE`,
`CURRENT_NS`, `CURRENT_PC`) a single load from the context by maintaining a
cached frame pointer that is updated whenever the active frame changes.
Handlers stay unchanged and keep using the same macros.

**Invariant:** `ctx->cached_frame == ctx->frame_top` whenever the VM is
about to dispatch an opcode (and after any operation that changes the
active frame).

**Files:**
- `koskari/lib/eval.h` – add cache field and update `CURRENT_*` macros.
- `koskari/lib/eval.c` – refresh cache at loop start; ensure all
  frame-top updates also update the cache (or are followed by a refresh).
- `koskari/lib/dynchain.c` – wherever `KosContext_FRAME_TOP(ctx)` is
  set, also set the cached frame.

**Details:**

- **Context structure (eval.h):**
  - Add a field, e.g. `KosDynFrame *cached_frame`, to the `KosContext`
    struct (or the equivalent internal struct used in eval.h). Document
    that it must equal `frame_top` at dispatch.
  - Add an accessor macro, e.g. `KosContext_CACHED_FRAME(c)`.

- **Macro change (eval.c):**
  - Change:
    - `#define CURRENT_FRAME(ctx)    KosContext_FRAME_TOP(ctx)`
    to:
    - `#define CURRENT_FRAME(ctx)    KosContext_CACHED_FRAME(ctx)`
  - Keep `CURRENT_STACK`, `CURRENT_CODE`, `CURRENT_NS`, `CURRENT_PC` as
    they are (they already derive from `CURRENT_FRAME(ctx)`).

- **Where the frame top is set (must also set cache):**
  - **eval.c:**
    - `kos_context_new`: after setting `KosContext_DYN_TOP(ctx)` and
      `CURRENT_FRAME(ctx) = top`, set
      `KosContext_CACHED_FRAME(ctx) = KosContext_FRAME_TOP(ctx)` (or
      equivalently set both to `top`).
    - `kos_tail_koscall`: after `CURRENT_FRAME(ctx) = frame`, set
      `KosContext_CACHED_FRAME(ctx) = frame`.
  - **dynchain.c:** All assignments to `KosContext_FRAME_TOP(ctx)` must
    be paired with `KosContext_CACHED_FRAME(ctx) = <same value>`. Grep
    for `KosContext_FRAME_TOP(ctx) =` and add the cache update next to
    each (lines ~613, 618, 632, 680, and any in transfer-plan or other
    helpers that set frame_top).

- **Dispatch loop (eval.c):**
  - At the very start of each iteration of the `for (;;)` loop (before
    transfer plan check, before `PyErr_Occurred` check), refresh the
    cache so that after any handler that might have changed the frame we
    still have a correct cache:
    ```c
    KosContext_CACHED_FRAME(ctx) = KosContext_FRAME_TOP(ctx);
    ```
  - This keeps the invariant without requiring every frame-changing path
    to remember to set the cache (defensive and simple).

**Alternative considered:** Pass a `KosDynFrame *` into every opcode
handler. Rejected to avoid touching all opcode files and signatures.

**Result:** Every use of `CURRENT_FRAME(ctx)` (and thus stack/code/ns/pc)
in the hot path becomes one load from `ctx->cached_frame` instead of
loading `ctx->frame_top` repeatedly. Cache is refreshed once per
iteration.

**Testing:** Full test suite; no change in behavior. Optionally add an
assert in debug builds that `cached_frame == frame_top` at loop entry.


---


## 3. CopyForWrite: macro / inline refcount fast path

**Goal:** In the hot path (refcount == 1), avoid calling
`KosStackBlock_CopyForWrite` at all; do a single refcount check (ideally
inlined) and only call the full copy path when needed.

**Approach:** Add a thin wrapper that checks refcount first; use it
everywhere instead of calling `CopyForWrite` directly. The wrapper can
live in the header as `static inline` so the refcount check is inlined
and the call to `CopyForWrite` only happens on the slow path.

**Files:** `koskari/lib/stack.h`, `koskari/lib/stack.c`.

**Details:**

- **stack.h**
  - Declare the existing implementation so the inline wrapper can call it:
    ```c
    int KosStackBlock_CopyForWrite(KosStackBlock **block);
    ```
  - Add an inline wrapper that callers should use:
    ```c
    static inline int KosStackBlock_EnsureExclusive(KosStackBlock **block) {
      KosStackBlock *b = *block;
      if (! b || KosStackBlock_REFCOUNT(b) == 1)
        return 0;
      return KosStackBlock_CopyForWrite(block);
    }
    ```
  - Document that new code should use `EnsureExclusive`; `CopyForWrite`
    remains for internal use and for the rare case where the caller
    already knows refcount > 1.

- **stack.c**
  - Remove `static` from `KosStackBlock_CopyForWrite` so the declaration
    in the header is valid.
  - Replace every call to `KosStackBlock_CopyForWrite(...)` with
    `KosStackBlock_EnsureExclusive(...)` (all current call sites are in
    stack.c: ClearBlock, Push, PUSH, ExtendPtr, EXTENDPTR, Pop, Drop,
    DropNoErr, Duplicate, Replace, PopTail, PopTailPtr, PopTailDict,
    PopTailDictNames, Pull; and in KosStack_PopBlock when copying to
    parent). Keep the body of `CopyForWrite` unchanged (including NULL
    handling and the refcount == 1 early return for consistency).

**Result:** Hot path (single-owner block) is one load and one comparison
inlined; no function call. Slow path still one call to `CopyForWrite`.

**Testing:** All existing stack and VM tests; no behavior change.


---


## 4. Bytecode extent table (O(1) instead of linear search)

**Goal:** Replace the O(n) scans over `extent_pairs` in `push_to_pop`,
`push_to_kind`, and `is_pop_op` with O(1) table lookups indexed by
opcode.

**Files:** `koskari/lib/bytecode.c` (and optionally `koskari/lib/bytecode.h`
if we expose the tables for testing).

**Details:**

- **Keep** `extent_pairs[]` as the single source of truth for the mapping
  (push op → pop op, kind) and for the set of pop ops. We will derive
  static tables from it or from the same list so we don’t duplicate
  logic.

- **Add three constant tables** of size `KOS_OP_THEEND + 1`:

  1. **push_to_pop_table[KOS_OP_THEEND + 1]**  
     - For each PUSH op in `extent_pairs`, set
       `push_to_pop_table[push_op] = pop_op`.  
     - All other entries are `KOS_OP_NOOP` (or 0 if the enum value is 0).

  2. **push_to_kind_table[KOS_OP_THEEND + 1]**  
     - For each PUSH op, set `push_to_kind_table[push_op] = kind`.  
     - All other entries can be `KOS_EXT_TRY` (or the first enum value)
       to match current “unknown” behavior.

  3. **is_pop_op_table[KOS_OP_THEEND + 1]**  
     - For each POP op in `extent_pairs`, set `is_pop_op_table[pop_op] = 1`.  
     - All other entries 0. Type: `static const unsigned char` or
       `static const char` to keep size small.

- **Implementation options:**

  - **Option A – Static tables from extent_pairs at runtime (one-time init):**  
    In `koskari_bytecode_init` (or a private init run once), loop over
    `extent_pairs` and fill three non-const arrays. Requires the arrays
    to be file-scope and writable at init. No need to hand-list every
    opcode; any future change to `extent_pairs` is automatically
    reflected.

  - **Option B – Static const tables with designated initializers:**  
    Define the three arrays with only the PUSH/POP entries set; the rest
    zero. No init code; tables are read-only. Slightly more verbose and
    must be updated if `extent_pairs` or opcode set changes.

  Recommendation: **Option A** so we keep one source of truth
  (`extent_pairs`) and avoid maintaining parallel lists.

- **Option A implementation sketch:**
  - Define file-scope arrays (not const):
    - `static KosOpCode push_to_pop_table[KOS_OP_THEEND + 1];`
    - `static KosExtentKind push_to_kind_table[KOS_OP_THEEND + 1];`
    - `static unsigned char is_pop_op_table[KOS_OP_THEEND + 1];`
  - In a new `static void init_extent_tables(void)` (or inline in
    `koskari_bytecode_init`):
    - Zero the three arrays (e.g. `memset`).
    - For each `i` in `extent_pairs`:
      - `push_to_pop_table[pair[i].push_op] = pair[i].pop_op;`
      - `push_to_kind_table[pair[i].push_op] = pair[i].kind;`
      - `is_pop_op_table[pair[i].pop_op] = 1;`
    - Optionally set `push_to_kind_table[op] = KOS_EXT_TRY` for
      non-push opcodes if we want “unknown” to remain KOS_EXT_TRY (current
      behavior).
  - Call `init_extent_tables()` once from `koskari_bytecode_init` before
    any code that uses the tables.
  - Replace:
    - `push_to_pop(op)` → `push_to_pop_table[op]`
    - `push_to_kind(op)` → `push_to_kind_table[op]`
    - `is_pop_op(op)` → `is_pop_op_table[op]`
  - Keep `is_push_op(op)` as `(push_to_pop_table[op] != KOS_OP_NOOP)` so
    it stays a single array read.

- **Edge cases:**
  - Opcodes outside 0..KOS_OP_THEEND: current code returns NOOP / KOS_EXT_TRY
    / 0; tables will have zero-initialized entries, so same behavior if
    we zero tables and never set entries for out-of-range opcodes.
  - Ensure `KOS_OP_NOOP` is 0 so that zeroed entries behave as “no pop”
    and “no push”.

**Result:** Extent derivation and validation use O(1) lookups per opcode
instead of O(11) linear scans; code generation and validation stay
correct.

**Testing:** All bytecode and VM tests; no change in generated code or
runtime behavior. Optionally add a small test that builds a code object
with PUSH_*/POP_* and checks extent derivation.


---


## Order of implementation

1. **trace_stack** – Isolated; no dependencies; quick win.
2. **CopyForWrite / EnsureExclusive** – Isolated; stack-only; reduces
   cost in eval and elsewhere that uses stack ops.
3. **Extent tables** – Isolated; bytecode init and derivation only.
4. **Frame caching** – Touches eval and dynchain; do last so the other
   changes are already in and the diff is easier to reason about.

Suggested branch/commits: one commit per item above, so each can be
reverted or benchmarked independently.


## Summary

| Item              | Primary file(s)   | Risk   | Effect                          |
|-------------------|-------------------|--------|----------------------------------|
| trace_stack       | eval.c            | Low    | No debug cost in hot loop       |
| Frame cache       | eval.c, eval.h, dynchain.c | Medium | One load per CURRENT_* use |
| EnsureExclusive   | stack.h, stack.c  | Low    | Inlined refcount check          |
| Extent tables     | bytecode.c        | Low    | O(1) extent lookup              |
