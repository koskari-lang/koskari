# compile/letrec
**Symbol:** `compile/letrec`

**Syntax:** `(letrec BINDINGS BODY ...)`

**Bindings:** A list of binding forms; each is `[SYM EXPR]` or `(EXPR as: SYM)`,
the same shape [`compile/let`](compile-let.md) accepts. There is no named form.

**Use-case:** Bindings whose values may refer to each other. Every name is
declared in one child scope before any value is evaluated, so a lambda in one
binding can call a function bound by another.

**Behaviour:** Pushes a child namespace and declares each name with no value.
Each right-hand side is then compiled and stored with `VAR_STORE`, left to
right, and the body runs in that scope. A name read before its own value is
stored raises, so the mutual references have to be inside function bodies
rather than evaluated during the binding pass.

A name/value count mismatch raises `SpecialError` at compile time.

**Examples:**

```koskari
>>> (letrec [[even? (lambda [n] (if (== n 0) True (odd? (- n 1))))]
...          [odd? (lambda [n] (if (== n 0) False (even? (- n 1))))]]
...   (even? 10))
True
```

Self-reference works the same way:

```koskari
>>> (letrec [[f (lambda [n] (if (== n 0) 1 (* n (f (- n 1)))))]]
...   (f 5))
120
```

**See also:** [`compile/let`](compile-let.md), [`compile/define`](compile-define.md).
