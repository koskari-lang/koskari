# compile/define-environ

**Syntax:** `(define-environ NAME [DEFAULT])`

**Behaviour:** Binds `NAME` to a ref into `os.environ` for the variable
of the same name: reading `NAME` reads the variable and `set!` on it
stores the variable. Without a default the ref is an `ItemRef`, and a
read of an unset variable raises `KeyError`. With a default the read
returns it when the variable is unset, and clearing the ref deletes the
variable.

The same ref is available anonymously as `(ref (environ KEY [DEFAULT]))`
and through `(bind! NAME (environ KEY [DEFAULT]))`, which let the
binding's name differ from the variable's. An empty key raises
`ValueError`.

**Examples:**

```koskari
import os

(define-environ HOME)
(instance? HOME str)           ;; True

(define-environ APP_MODE "dev")
APP_MODE                       ;; "dev" when unset
(set! APP_MODE "prod")
os.environ["APP_MODE"]         ;; "prod"
```

**See also:** [`bind!`](compile-bind.md), [`ref`](compile-ref.md).
