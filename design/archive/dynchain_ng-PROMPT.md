# Agent prompt: `dynchain_ng` (compatible replacement)

## Objective

Implement a new dynamic-chain subsystem (`dynchain_ng.c` / header split as appropriate) that is **behaviorally equivalent** to the current `koskari/lib/dynchain.c` + `dynchain.h` as consumed by the full Koskari VM. The old implementation may be discarded; **external symbols and observable behavior** used by eval, opcodes, coro/demand/iter, cont, callable, utils, tests, and Python extension init must remain compatible unless this document explicitly notes an intentional ABI break (default: **no breaks**).

The new implementation must be CLEAR, and COMPARTMENTALIZED. Current implementations are nearly impossible to debug.

## Inputs

1. This repository’s current `dynchain.h` (full public API, structs, macros).
2. `docs/dynchain_ng/DYNCHAIN_USAGE_INVENTORY.md` — per-file call sites and symbol usage (mandatory cross-check).
3. Existing automated tests (`tox` / native tests per project Makefile).

## Hard requirements

- **Full VM scope:** Every use of `KosDynNode`, `KosDynFrame`, `KosContext` chain-related fields, `KosTransferPlan`, `KosContinuation`, and `kos_*` entry points from `dynchain.h` in non-dynchain translation units must continue to work.
- **Symbol compatibility:** For each function, type, macro, and `KosContext` field listed in the inventory as **externally referenced**, provide the same names and compatible signatures/layouts **or** document a mechanical migration (default: preserve symbols).
- **GC:** `kos_dynnode_traverse`, `kos_dynchain_traverse`, `kos_continuation_traverse`, `kos_transferplan_traverse` must remain correct for all owned `PyObject *`.
- **Transfer plans:** `kos_run_transfer_step(es, …)`, all `kos_build_*_plan` variants (first arg **`KosEvalState *es`**) used outside `dynchain.c`, `kos_free_transfer_plan`, `kos_suspend_transfer_plan` / `kos_resume_transfer_plan` if wired, and `KosTransferPlan_*` macros used in `eval.c` / opcodes must behave equivalently.
- **Continuations:** `kos_shift_capture`, `kos_capture_cc`, `kos_invoke_continuation(es, …)`, `kos_continuation_*`, `KosContinuation_*` macros used in `cont.c` / opcodes.
- **Module lifecycle:** `koskari_dynchain_init`, `koskari_dynchain_clear` — same registration/cleanup contract.

## Process

1. Read `DYNCHAIN_USAGE_INVENTORY.md` and build a checklist of **every** external reference; tick off each when the new implementation satisfies it.
2. For each public function in `dynchain.h`, either reimplement or forward; **do not** leave unresolved symbols.
3. Run the project’s full test suite; fix until green.
4. If behavior is ambiguous, add a focused test that locks the intended semantics before changing implementation.

## Forbidden

- Partial migration leaving some call sites on old internals.
- “Simplifying” opcode or eval code in the same change set without explicit user approval.
- Dropping transfer-plan or continuation behavior because it is hard to follow in the old file.

## Deliverables

- New implementation file(s) replacing `dynchain.c` (name may stay `dynchain.c` on branch or be `dynchain_ng.c` with build switch — user decides).
- Updated headers only if necessary; prefer preserving `dynchain.h` as the stable include unless splitting is required.
- Short `README` in `docs/dynchain_ng/` describing any intentional deviations and migration notes (if none, state “none”).

## Success criteria

- All tests pass.
- `DYNCHAIN_USAGE_INVENTORY.md` checklist complete with no missing symbols.
- No new undefined behavior under ASan/Valgrind if those are part of CI.
