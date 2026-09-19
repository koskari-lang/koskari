# compile/return
**Symbol:** `compile/return`

**Syntax:** `(return VAL ...)` -- variadic.

| Form                | Result                                           |
|---------------------|--------------------------------------------------|
| `(return)`          | returns `None`                                   |
| `(return VAL)`      | returns `VAL`                                     |
| `(return VAL ...)`  | returns a `tuple` of the values                  |
| `(return K: V ...)` | returns a `values` object over the keyword pairs |

**Bare form:** the lone symbol `return` (as in `unless ok: return`) expands to
`(return)`, so the Python-style spelling returns `None` from the current
function. The expansion is governed by the
[`compiler/bare-control-words`](../../guide/pragma.md#compiler) pragma of the
module being compiled: `convert` (the default) expands silently, `warn` expands
with a `SyntaxWarning`, and `error` refuses at compile time. Because the bare
symbol always expands, `return` can never name a variable.

**Use-case:** Return from the current function and transfer control back to the
caller. `return` is variadic. With no argument it returns `None`; with one it
returns that value; with two or more positional arguments it returns a `tuple`;
and with keyword arguments it returns a multi-value `values` object.

**Behaviour:** Compiles each positional argument and emits `RETURN N`, where `N`
is the argument count. `RETURN 0` returns `None`, `RETURN 1` returns the single
value, and `RETURN N` (for `N > 1`) packages the values as a tuple. A keyword
argument re-heads the form as `(values ...)`, so `(return a: 1 b: 2)` returns a
`values` object; positional and keyword arguments must not be mixed.

At runtime, when the frame has a parent (e.g. normal CALL), RETURN walks the
dynamic chain with handle(RETURN), runs FINALLY if present, detaches the frame,
and delivers the value to the parent (opcode returns `EVAL_CONTROL_RETURNED()`
or `EVAL_NORMAL()`). When the frame has no parent or its parent is the generator
node (KOS_DYN_GEN), RETURN returns `EVAL_RETURN(val)`; `eval_event_return`
delivers and `koskari_eval` exits with `KOS_RES_RETURN`.

**Consuming multiple values:** index the returned tuple with `(item result 0)`,
or destructure it with [`define-values`](compile-define-values.md) /
[`let-values`](../macros/compile-let-values.md):

```koskari
define swap [a b]:
  return b a

define-values [x y] (swap 1 2)   ;; x = 2, y = 1
```

**Examples:**

```koskari
(return)                    ;; -> None
(return result)             ;; -> result
(return a b c)              ;; -> #(a b c)  (a tuple)
(return a: a b: b c: c)     ;; -> values(a=a, b=b, c=c)
```
