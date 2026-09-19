# layout/inline-block
**Symbol:** `layout/inline-block`

**Use-case:** Opt-in layout for a head whose block breaks up a long line
rather than expressing sub-expressions. Bind in a `.ki` module, for
example `define layout/report layout/inline-block`.

**Behaviour:** Splices the elements of every body line onto the header
and folds nothing, producing one flat list
`(HEAD ARG... ELEMENT...)`. A body line holding several elements stays
several elements; it is not read as a call, which is the difference from
[the default block handler](layout-block-default.md). A sub-block whose
header opens with a keyword splices as the keyword and its value, the
same way the default handler treats one. Does not accept peers.

**Syntax:**

```
HEAD ARG ... :
  ELEMENT ELEMENT ...
  ELEMENT ...
```

**Examples:**

```koskari
;; with define layout/gather layout/inline-block in the module:
gather a:
  b (c d)
  e
;; -> (gather a b (c d) e)
```

```koskari
;; with define report print and define layout/report layout/inline-block:
report "chained:":
  (list (chain #[1 2] #[3 4]))
  (list (islice (repeat "x") 2))
;; prints: chained: [1, 2, 3, 4] ['x', 'x']
```
