# layout/extend-syntax
**Symbol:** `layout/extend-syntax`

**Use-case:** Block surface syntax for **`extend-syntax`**. With no dedicated
handler registered, the default **`layout/block`** handler splices the
transformer after the name: **`(extend-syntax NAME TRANSFORMER …)`**.

**Behaviour:** Compile-time behaviour is **`compile/extend-syntax`** (see
[Macros](../macros/index.md)). Does not accept peers.

**Syntax:**

```
extend-syntax NAME:
  syntax-rules [LIT ...]:
    PATTERN:
      TEMPLATE...
```

**Implementation:** default **`layout/block`** via **`koskari/reader/layouts/basics.layout_block`**
