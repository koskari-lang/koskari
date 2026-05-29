# KOS_OP_CALL_ATTR

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: EXTRA. Schema: CONST (attr name), COUNT (npos), NOARG, NOARG.
- **Status**: Implemented.
- **Stack**: `[..., obj, arg_0, ..., arg_{N-1}] → [..., result]`. Attr name from constant pool (EXTRA row). Resolve `obj.name` via `kos_resolve_named_attr`; then invoke. Unbound class method (plain PyFunction/KosFunction from MRO): call `fn(obj, *args)`. Bound or instance-dict callable: call `callable(*args)`. Resolver return: -1 err, 0 = send self, 1 = omit self.
- **Invariants**: Stack has at least 1 + N elements (obj, N args). Resolved attribute must be callable or TypeError.
- **Design**: [design/interpreter/attr-lookup-cache.md](../../../design/interpreter/attr-lookup-cache.md)

**Implementation notes** (koskari/lib/opcode/CALL_ATTR.c, eval.c):

- **Stack arrangement**: `ptr = KosStackBlock_TailPtr(*stack, count)` where `count = n + 1`. Index layout: `ptr[0]` = obj, `ptr[1..count-1]` = positional args. Attr name from `KosCode_GetConstant(code, row.a0)`. Stack is consumed via `KosStackSteal_Tail` (KosCallable path) or `KosStackBlock_DropNoErr` (plain Python path).

- **`kos_resolve_named_attr` return value**: -1 = error (exception set); 0 = send self (unbound class method — caller invokes `fn(obj, *args)`); 1 = omit self (bound method, instance dict, data descriptor result, etc. — caller invokes `callable(*args)`).

- **Invocation paths**:
  - **KosCallable** (KosFunction, KosBoundMethod, etc.): `KosStackSteal_Tail(&steal, count, stack)` to own args; if `send_self == 0` then `args = &steal.vals[1]`, `nargs = n + 1`; else `args = (n > 0) ? &steal.vals[2] : NULL`, `nargs = n`. Then `KosContext_Call(ctx, out_ev, resolved, args, nargs, NULL, 0)`; `KosStackSteal_Free(&steal)`.
  - **Plain Python**: `PyObject_Vectorcall(resolved, ...)` with same arg layout; then `kos_push_or_await(ctx, out_ev, result)`.

- **Implemented functions** (eval.c): `kos_resolve_named_attr` (static; full resolution, cache placeholder for Phase 3); `is_plain_instance_method` (KosFunction/PyFunction); `descr_is_data` (tp_descr_set != NULL); `instance_dict_get` (via `_PyObject_GetDictPtr`). Invocation: `KosContext_Call`, `kos_push_or_await`, `KosStackSteal_Tail` / `KosStackSteal_Free` (stack.h/stack.c).

**Example:** `"hello".upper()` (0 args). Pool: 0 = "upper", 1 = "hello". EXTRA row 0: a0 = cpool index of "upper", a1 = 0 (npos).

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 0     | CONST    | 1   | 1 ("hello") | stack: [..., obj] |
| 1     | CALL_ATTR| 0   | 0 (extra idx) | resolve obj.upper; call; push result. stack: [..., "HELLO"] |
| 2     | RETURN   | 1   | 1        | |

**Example:** `c.add_one(41)` (1 arg, unbound method). Pool: 0 = "add_one", 1 = c, 2 = 41. EXTRA row 0: a0 = cpool index of "add_one", a1 = 1 (npos).

| index | opcode   | arg | arg view | comment |
|------:|----------|-----|----------|---------|
| 0     | CONST    | 1   | 1 (c)    | stack: [..., obj] |
| 1     | CONST    | 2   | 2 (41)   | stack: [..., obj, 41] |
| 2     | CALL_ATTR| 0   | 0 (extra idx) | resolve; call add_one(c, 41); push 42. stack: [..., 42] |
| 3     | RETURN   | 1   | 1        | |

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
