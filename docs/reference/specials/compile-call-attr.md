# compile/call-attr

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/call-attr`

**Syntax:** `(compile/call-attr OBJ SYM [ARG ...])`

**Use-case:** Call a method by name: resolve `obj.sym`, then invoke with the
given arguments. Equivalent to `obj.sym(arg...)` in Python. Supports positional
args, keyword args (`KEY: VAL`), and `*:` / `**:` (emits CALL_ATTR_STAR).

**Behaviour:** Compiles OBJ, then parses ARGS with `gather_parameters`.
Positional-only emits `CALL_ATTR`; positional + keywords emits `CALL_ATTR_NAMES`;
any `*:` or `**:` emits `CALL_ATTR_STAR`. SYM must be a symbol (the attribute
name).

**Note:** `(compile/call (attr obj name) args)` and `(compile/call (compile/attr obj name) args)`
expand to this form before compilation. So does `(compile/call .name obj args...)` when `.name`
is a symbol whose text starts with `.` and at least `obj` is present (the dotted callee is not
used as a variable).

**Examples:**

```scheme
;; Positional only
(compile/call-attr s upper)
(compile/call-attr s split sep)
(compile/call-attr s replace "l" "L")

;; With keywords
(compile/call-attr obj m 99 name: 42)

;; Star args (emits CALL_ATTR_STAR)
(compile/call-attr obj fn *: args **: kwargs)
```

**Tests:** `tests/compile/specials/test_call_attr.py`

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
