# compile/dict-each

**Syntax:** `(dict-each [BINDING SEQ] key: KEY value: VALUE)` or
`(dict-each [BINDING SEQ] BODY ...)`, with an optional `when:` or
`unless:` clause on the header.

**Behaviour:** Builds a dict with one entry per element of `SEQ`, bound
as `BINDING`. `SEQ` is evaluated in the scope the form is in, so a
class-body name is in reach. Without `key:` and `value:`, the value of
`BODY` is a two-item key and value pair. A `when:` clause keeps only
elements whose test is true; `unless:` keeps those whose test is false.
A body that is a single `when` or `unless` form also filters,
contributing nothing when its test fails. An `await` or a `shift` in
the keys, values or body works as it does in
[`list-each`](compile-list-each.md), and the dict is likewise one
object for the whole loop: a continuation invoked again writes into
the dict the first shot already returned.

**Examples:**

```koskari
var words #["a" "bb"]

dict-each [w words] key: w value: (len w)
;; {'a': 1, 'bb': 2}

dict-each [w words] when: (> (len w) 1) key: w value: (w.upper)
;; {'bb': 'BB'}
```

**See also:** [`each`](compile-each.md),
[`list-each`](compile-list-each.md).
