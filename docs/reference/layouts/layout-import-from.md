# layout/import-from
**Symbol:** `layout/import-from`

**Use-case:** Block syntax for a long import list. The header names the
module, the body lines carry the imported names, and the whole block
lowers to the one form [`import-from`](../specials/import-from.md)
compiles.

**Behaviour:** The same lowering as
[`layout/inline-block`](layout-inline-block.md): every element of every
body line splices onto the header in order and nothing is folded, so a
body line of several names stays several names rather than becoming a
call. Produces `(import-from MODULE NAME ...)`. Does not accept peers.

**Syntax:**

```
import-from MODULE:
  NAME NAME ...
  NAME ...
```

**Examples:**

```koskari
import-from itertools:
  count cycle
  chain tee
;; -> (import-from itertools count cycle chain tee)

import-from itertools:
  chain repeat
  islice
(print (list (chain #[1 2] #[3 4])))
;; prints: [1, 2, 3, 4]
```
