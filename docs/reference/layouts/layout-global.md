# layout/global
**Symbol:** `layout/global`

**Use-case:** Block syntax for the [`global`](../macros/compile-global.md)
prefix. `global` adds nothing of its own to a block; the word after it
decides how the block reads. `global class NAME:` reads as
[`class NAME:`](layout-class.md) does, with `global` put back in front,
so an omitted superclass list is `()` and the body is one `begin`.

**Behaviour:** No peers. The header is `(global HEAD ARG ...)`. When
**HEAD** has a layout, the block `HEAD ARG ...:` over the same body is
lowered through it and `global` is prepended to the result. When
**HEAD** has no layout of its own, or the header is a bare `global`,
the block reads through the [default block layout](layout-block-default.md),
so `global def NAME [FORMALS]:` and `global var NAME:` splice their body
lines onto the header as any other call does.

**Syntax:**

```
global class NAME:
  BODY
;; -> (global class NAME () (begin BODY))

global class NAME (Base):
  BODY
;; -> (global class NAME (Base) (begin BODY))

global def NAME [FORMALS]:
  BODY
;; -> (global def NAME [FORMALS] BODY)
```

**Examples:**

```koskari
def setup []:
  global class Registry:
    var size 0
;; -> (global class Registry () (begin (var size 0)))
;; Registry is bound at the module once setup runs

def start []:
  global def serve [port]:
    (listen port)
;; -> (global def serve [port] (listen port))
```

**See also:** [`class`](layout-class.md), [compile/global](../macros/compile-global.md).
