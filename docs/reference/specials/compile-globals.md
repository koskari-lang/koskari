# compile/globals
**Symbol:** `compile/globals`

**Syntax:** `(globals)`

**Use-case:** Obtain the module globals namespace for the current frame:
walk `KosNamespace_PARENT` from the frame's namespace to the outermost parent
(analogous to Python's `globals()`). Useful for reflection, explicit binding
into the module namespace, or passing the globals dict to APIs that expect it.

**Behaviour:** Emits `GLOBALS` (argument 0). Pushes that namespace onto the
stack as the expression's value. Any arguments are rejected with `SpecialError`.

**Examples:**

```scheme
;; Module globals namespace for the current frame
(globals)
```

**Implementation:** `koskari/compile/specials/variables.special_globals`


