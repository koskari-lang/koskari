# compile/str
**Symbol:** `compile/str`

**Syntax:** `(str OBJ ...)`

**Use-case:** String conversion via `__str__`, variadic like Python when
several values are supplied.

**Behaviour:** Emits the corresponding `OBJECT_*` opcode; see
[Object operator overview](object-operators/index.md) for folding rules shared
with other variadic operators.

**Examples:**

```koskari
(str 42)
```
