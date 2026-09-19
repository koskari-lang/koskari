# compile/dict-each

**Syntax:** `(dict-each [BINDING SEQ] key: KEY value: VALUE)`, with an
optional `when:` or `unless:` clause before `key:`.

**Behaviour:** Builds a dict from one `KEY` and `VALUE` per element of
`SEQ`, bound as `BINDING`. It is `dict` over an
[`each`](compile-each.md) that yields key and value pairs, so the filter
clause skips elements the same way.

**Examples:**

```koskari
define words #["a" "bb"]

dict-each [w words] key: w value: (len w)
;; {'a': 1, 'bb': 2}

dict-each [w words] when: (> (len w) 1) key: w value: (w.upper)
;; {'bb': 'BB'}
```

**See also:** [`each`](compile-each.md),
[`list-each`](compile-list-each.md).
