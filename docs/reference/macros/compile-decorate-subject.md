# compile/decorate-subject

**Syntax:** `(decorate-subject DECORATOR SUBJECT)`, where the decorator's
shape selects the arm:

| Decorator | Expands to |
|-----------|------------|
| `(let BINDINGS)` | `(let BINDINGS SUBJECT)` |
| `(with BINDING)` | `(with BINDING SUBJECT)` |
| `(parameterize PARAMS)` | `(parameterize PARAMS SUBJECT)` |
| `(call-with BINDING)` | a function that enters `(with BINDING ...)` around each call of `SUBJECT` |
| `(call-parameterize PARAMS)` | a function that enters `(parameterize PARAMS ...)` around each call |
| anything else | `(DECORATOR SUBJECT)` |

**Behaviour:** Applies one decorator to a subject form. A body-less
extent form, `let`, `with`, or `parameterize`, is completed by grafting
the subject in as its body, so the extent spans the subject's
evaluation. The `call-` forms instead wrap the subject in a forwarding
function that establishes the extent per invocation and copies the
callable's metadata through `decorate-wraps`, which is
`functools.update_wrapper`. Any other decorator expression is evaluated
and called on the subject.

New shape arms are added with [`extend-syntax`](compile-extend-syntax.md)
and are tried before the invocation arm.

**Examples:**

```koskari
>>> (macroexpand-1 '(decorate-subject (let [[c 0]]) subject))
(let [[c 0]] subject)
>>> (macroexpand-1 '(decorate-subject d fn))
(d fn)
```

```koskari
@let [[count 0]]
define tick []:
  incr! count
  count
```

`tick` closes over a private `count`. See
[Decorators](../../guide/decorators.md) for the families and the timing
of each.
