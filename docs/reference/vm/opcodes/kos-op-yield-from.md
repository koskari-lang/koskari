# KOS_OP_YIELD_FROM
- **Style**: NOARG.
- **Stack**: Pops TOS, the value to delegate to. Pushes nothing. Each value the delegate produces is yielded out of the enclosing generator as that generator's own yield. When the delegate is exhausted, execution continues at the next instruction; the delegate's return value is discarded. `compile/yield-from` emits `CONST None` after the opcode so the form has a value.
- **Execution model**: One instruction covers the whole delegation. The generator suspends and resumes inside it as many times as the delegate yields; the PC stays past the instruction throughout. A value sent into the suspended generator is sent to the delegate, an exception thrown into it is thrown into the delegate, and closing the generator closes the delegate. Awaits inside an async delegate are awaited in place.
- **Delegate kinds** (decided when TOS is consumed): a Koskari iterator or generator, a Python generator, a Python async iterator or async generator, or any sync iterator. A callable is called once with no arguments and its result is classified the same way. Anything else goes through `iter(obj)`.
- **Rejected**: Coroutines, Koskari or Python, directly or returned by the callable: `TypeError: coroutine object is not async iterable`. A non-iterable TOS is a `TypeError`. Yielded values are never re-awaited.
- **Implementation**: `koskari/lib/opcode/YIELD_FROM.c`. The delegate is the frame's resume state for the whole delegation; the generator's frame carries no chain node for it.
- **See also:** [compile/yield-from](../../specials/compile-yield-from.md) for the
  special form; [KOS_OP_YIELD](kos-op-yield.md) for single-value yields.
- **Covered by**: `tests/lib/opcode/test_yield_from.py`.

**Example:** Delegate to a sync iterator; each value is yielded out, and nothing is pushed when the iterator exhausts. Pool: 0 = [1, 2, 3].

| index | opcode   | arg | arg view | comment |
|------:|---------|-----|----------|---------|
| 0     | CONST   | 0   | 0 ([1,2,3]) | stack: [..., iterable] |
| 1     | YIELD_FROM | 0 | 0        | delegate; each value yields out; on exhaustion continue, nothing pushed |
| 2     | RETURN  | 1   | 1        | |
