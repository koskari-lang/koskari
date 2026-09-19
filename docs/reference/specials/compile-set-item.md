# compile/set-item
**Symbol:** `compile/set-item`

**Syntax:** `(set-item OBJ KEY VAL)`

**Use-case:** Set the item at KEY in OBJ to VAL. The form evaluates to None.

**Behaviour:** When KEY and VAL are both feeders (a constant or a
symbol), compiles OBJ, KEY, VAL and emits ITEM_STORE; the compress
pipeline folds the feeders ahead of the row, the value, then the key,
then a symbol OBJ, into the row that names them, so `(set-item d "k"
1)` is one VAR_ITEM_CONST_STORE_CONST row with no stack effect and
`(set-item o.a k v)` is the attribute load then ITEM_VAR_STORE_VAR.
Otherwise the special names what a fold could not reach: a symbol OBJ
under a computed KEY compiles the key and VAL to the stack and emits
VAR_ITEM_STORE; a computed VAL behind a symbol OBJ or a feeder KEY
compiles a compound OBJ and VAL to the stack and emits
VAR_ITEM_VAR_STORE, VAR_ITEM_CONST_STORE, ITEM_VAR_STORE, or
ITEM_CONST_STORE. A named object or key is fetched when the row runs,
after every stack operand, so a VAL that rebinds OBJ stores into the
new binding and an unbound OBJ raises after VAL has run. In a function
body the VAR rows lower to their FAST twins. Then CONST None.

**Examples:**

```koskari
(set-item vec 0 99)
```
