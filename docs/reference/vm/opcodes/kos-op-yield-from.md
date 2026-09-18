# KOS_OP_YIELD_FROM
- **Style**: NOARG.
- **Stack**: On first entry: consume TOS (subexpr_result), push `KOS_DYN_YIELD_FROM`, roll back PC so the next loop iteration re-dispatches to YIELD_FROM. On each resume: drive one step of the delegate (or handle send/throw/close inject). On completion: pop the dyn node, push final value. PC does not advance until delegation completes.
- **Execution model**: Single-opcode delegation primitive (conduit); not loop sugar. Full send/throw/close propagation; async flattening for Python and Koskari async gens. Backed by dynamic chain node `KOS_DYN_YIELD_FROM`, which holds the delegate, its kind, and pending action/arg for send/throw/close and for resume-from-await.
- **Delegate kinds** (chosen at first entry by normalizing TOS): **sync iterator** (PyIter_Next); **sync generator** (send/throw/close); **Python async iterator** (__anext__); **Python async generator** (__anext__/asend/athrow/aclose); **Koskari async generator** (driven in-process via koskari_eval). Callables are called once; the return value is classified as above. Fallback: `iter(obj)` for sync iteration.
- **Rejected**: Coroutines (Koskari or Python). Normalization raises `TypeError: "coroutine object is not async iterable"`. Delegate results (yielded or final value) are never re-awaited; they are pushed or emitted as values only.
- **Invariants**: TOS on first entry must be an iterable or async-iterable (or callable returning one). The opcode is re-entered on each loop iteration until the delegate exhausts, yields (boundary **`KOS_RES_YIELD`**), or suspends on await (boundary **`KOS_RES_AWAIT`**).
- **Implementation**: Handler and all YIELD_FROM-dedicated helpers (normalize, rollback PC, fetch StopIteration value, emit yield, complete with value, drive sync iter/gen and Python/Koskari async) live in `koskari/lib/opcode/YIELD_FROM.c` (included into `eval.c`). Delegate close / unwind for the `KOS_DYN_YIELD_FROM` node lives in `koskari/lib/dynchain/dyn_yield_from.c` (e.g. `kos_yieldfrom_close_delegate`, `kos_dyn_yieldfrom_unwind`). See `koskari/lib/opcode/README.md` for the opcode-fragment layout.
- **See also:** [compile/yield-from](../../specials/compile-yield-from.md) for the
  special form; [KOS_OP_YIELD](kos-op-yield.md) for single-value yields.
- **Covered by**: `tests/lib/opcode/test_yield_from.py` (sync/async delegates, Koskari async gen, send/throw/close, final values, type errors).

**Example:** Delegate to a sync iterator; each yielded value is emitted (or pushed in-loop); final value is pushed when the iterator exhausts. Pool: 0 = [1, 2, 3].

| index | opcode   | arg | arg view | comment |
|------:|---------|-----|----------|---------|
| 0     | CONST   | 0   | 0 ([1,2,3]) | stack: [..., iterable] |
| 1     | YIELD_FROM | 0 | 0        | first entry: consume TOS, push KOS_DYN_YIELD_FROM, rollback PC; resume: drive delegate; on exhaust stack: [..., final_value] |
| 2     | RETURN  | 1   | 1        | |


## 7. Iteration

