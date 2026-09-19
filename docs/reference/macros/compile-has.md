# compile/has?

**Syntax:** `(has? COLLECTION ELEMENT)`

**Behaviour:** Membership. Expands to `(in ELEMENT COLLECTION)`, which
is Python's `element in collection`, with the collection first so it
reads as a question about the collection.

**Examples:**

```koskari
koskari> (has? #[1 2] 2)
True
koskari> (has? #[1 2] 9)
False
```
