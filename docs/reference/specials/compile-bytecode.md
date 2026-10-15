# compile/bytecode
**Symbol:** `compile/bytecode`

**Syntax:** `(bytecode [OP ARG] ...)`

**Use-case:** Direct bytecode injection for tests, debugging, or hand-written
code. Each element of the body is a pair (or list) `[OP ARG]` where `OP` is an
opcode name (e.g. `CONST`, `RETURN`) or integer, and `ARG` is the instruction
argument (optional; defaults to 0).

**Behaviour:** Compiles to a single fragment that emits the given instructions in
order. No constant-pool or label resolution is done inside the form; the
compiler's normal `complete` pass handles that. Opcode names are the `OpCode`
enum member names; a name or number that is not an opcode is a compile-time
error positioned at that segment.

**Examples:**

```koskari
;; Return a constant string
(bytecode
  [CONST "Hello World"]
  [RETURN 1])

;; Load two variables and call a function of two arguments
(bytecode
  [VAR add]
  [VAR value_a]
  [VAR value_b]
  [CALL 2]
  [RETURN 1])
```
