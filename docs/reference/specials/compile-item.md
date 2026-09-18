# compile/item
**Symbol:** `compile/item`

**Syntax:** `(item OBJ KEY)`

**Use-case:** Load the item at key KEY from OBJ (subscript/getitem).

**Behaviour:** Compiles OBJ, then KEY, then emits ITEM, and the compress
pipeline folds a symbol OBJ and a constant or symbol KEY into the row
that names them (ITEM_CONST, ITEM_VAR, VAR_ITEM_CONST, VAR_ITEM_VAR, and
their FAST twins in a function body). When OBJ is a symbol and KEY is
neither a constant nor a symbol, the key compiles to the stack and the
special emits VAR_ITEM, which names the object, since no fold reaches
an object load behind a computed key. A named object or key is fetched
when the row runs.

**Examples:**

```koskari
(item vec 0)
(item tbl (compute-key))
```

