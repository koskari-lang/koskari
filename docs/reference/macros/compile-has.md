# compile/has?

**Syntax:** `(has? COLLECTION ELEMENT)`

**Behaviour:** Membership: true when `ELEMENT` is in `COLLECTION`, as
Python's `element in collection`, with the collection first so it reads
as a question about the collection.

**Examples:**

```koskari
>>> (has? #[1 2] 2)
True
>>> (has? #[1 2] 9)
False
```
