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

**Note:** `((attr obj name) args)` expands to this form before compilation. So
does `(.name obj args...)` when `.name` is a symbol whose text starts with `.`
and at least `obj` is present (the dotted callee is not used as a variable).

**The postfix spelling reaches this too, for any receiver.** Because the reader
turns a dot attached to a value into `(compile/attr ...)`, and this special
matches that form in call position, `(obj.name args)` is a method call whatever
`obj` is — a name, a form, or a literal:

```koskari
(s.upper)                  ; on a name
((get-fn x).baz 1 2)       ; on an expression
("hello".upper)            ; on a literal
("-".join parts)
```

The leading-dot spelling `(.upper "hello")` remains valid and compiles to the
same thing; it is no longer the only option for a receiver that is not a bound
symbol. Both spellings are governed by `reader/dot-attrs` — see
[Grammar](../grammar.md#suffix-syntax).

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


