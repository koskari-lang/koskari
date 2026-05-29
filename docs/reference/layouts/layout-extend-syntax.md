# layout/extend-syntax

```{admonition} Reference status
:class: note

Adapted from design/LAYOUTS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `layout/extend-syntax`

**Use-case:** Block surface syntax for **`extend-syntax`**, wired the same way as
**`layout/define-syntax`**: **`basics.ki`** has **`define layout/extend-syntax layout/flat`**, so the read result is **`(extend-syntax NAME TRANSFORMER …)`** with no
extra **`compile/begin`** wrapper around the transformer at read time.

**Behaviour:** Same **`layout/flat`** splicing as documented under **layout/flat**
above. Compile-time behaviour is **`compile/extend-syntax`** (see
**compile/extend-syntax** in [MACROS.md](../../design/MACROS.md)). Does not accept peers.

**Syntax:**

```
extend-syntax NAME:
  syntax-rules [LIT ...]:
    PATTERN:
      TEMPLATE...
```

**Implementation:** `koskari/importer/basics.ki` (**`define layout/extend-syntax layout/flat`**); **`koskari/reader/layouts/basics.layout_flat`**.

---

Source: [design/LAYOUTS.md](../../design/LAYOUTS.md)
