# compile/function-compose
**Syntax:** `(function-compose FN)` or `(function-compose FN1 FN2 ...)`

**Behaviour:** A single **FN** is returned unchanged. Two or more **FN**s
make a function taking **`*: args **: kwds`** that nests calls from right to left:
the rightmost **FN** receives **`*: args **: kwds`**, and each **FN** to the left wraps
the accumulated call (same order as **`(decorate [DECORATOR ...] FN)`** folding).

```koskari
(function-compose fn1 (fn2 foo) fn3)
```

behaves like:
```koskari
(lambda [*: args **: kwds]
  (fn1 ((fn2 foo) (fn3 *: args **: kwds))))
```

Composition is syntactic: argument expressions are not evaluated until the composed
function is invoked.
