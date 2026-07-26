# compile/parameter
**Symbol:** `compile/parameter`

**Syntax:** `(parameter NAME [DEFAULT])`

**Use-case:** Read a parameter by literal name. This is what `parameterq`
expands to; the `parameter` builtin remains for names computed at runtime.

**Behaviour:** Resolves the enclosing `parameterize` extent, then the root scope
of the current domain. Neither store is written and neither gains an entry on a
miss. With no `DEFAULT` a miss raises `NameError`. A `DEFAULT` that compiles to
a single CONST is folded into the `PARAM_CONST` instruction; otherwise the
expression is compiled and `PARAM` is emitted with a count of 1, so the stacked
value answers a miss.

**Defaults declare, they do not store.** The `DEFAULT` answers this read site
only. It never lands in a parameter store, so it cannot change what any other
read sees.

**Examples:**

```koskari
;; unbound name raises NameError
(parameterq verbose)

;; literal default folds into the instruction
(parameterq verbose False)

;; computed default is evaluated and left on the stack
(parameterq verbose (default-for 'verbose))
```
