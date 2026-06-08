# compile/has?

**Symbol:** `compile/has?` (surface: `has?`)

**Syntax:** `(has? COLLECTION ELEMENT)`

**Behaviour:** Membership test. Expands to **`(compile/in ELEMENT COLLECTION)`** — true when
**COLLECTION** contains **ELEMENT**.
