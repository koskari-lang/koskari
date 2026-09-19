# compile/incr! and compile/decr!

**Syntax:** `(incr! TARGET [VALUE])`, `(decr! TARGET [VALUE])`, with a
default step of `1`.

**Behaviour:** Expand to [`inplace`](../specials/compile-inplace.md)
with `+` or `-`, so the target is read once, updated, and stored back.
The target is a name, an attribute, or a subscript, as for `set!`.

**Examples:**

```koskari
define x 1
incr! x          ;; 2
incr! x 10       ;; 12
decr! x 2        ;; 10

incr! self.count
incr! counts[word]
```

**See also:** [`set!`](compile-set.md).
