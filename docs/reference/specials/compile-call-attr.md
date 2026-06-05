# compile/call-attr
**Symbol:** `compile/call-attr`

**Syntax:** `(call-attr OBJ SYM [ARG ...])`

**Use-case:** Call a method by name: resolve `obj.sym`, then invoke with the
given arguments. Equivalent to `obj.sym(arg...)` in Python. Supports positional
args, keyword args (`KEY: VAL`), and `*:` / `**:` (emits CALL_ATTR_STAR).

**Behaviour:** Compiles OBJ, then parses ARGS with `gather_parameters`.
Positional-only emits `CALL_ATTR`; positional + keywords emits `CALL_ATTR_NAMES`;
any `*:` or `**:` emits `CALL_ATTR_STAR`. SYM must be a symbol (the attribute
name).

**Note:** `((attr obj name) args)` and `((attr obj name) args)`
expand to this form before compilation. So does `(.name obj args...)` when `.name`
is a symbol whose text starts with `.` and at least `obj` is present (the dotted callee is not
used as a variable).

**Examples:**

```koskari
;; Positional only
(call-attr s upper)
(call-attr s split sep)
(call-attr s replace "l" "L")

;; With keywords
(call-attr obj m 99 name: 42)

;; Star args (emits CALL_ATTR_STAR)
(call-attr obj fn *: args **: kwargs)
```


