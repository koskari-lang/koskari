# compile/set-item
**Symbol:** `compile/set-item`

**Syntax:** `(set-item OBJ KEY VAL)`

**Use-case:** Set the item at KEY in OBJ to VAL. The form evaluates to None.
Same constant-key optimization as compile/item.

**Behaviour:** When OBJ is a symbol and KEY is a constant or a symbol,
compiles VAL first and emits one VAR_CONST_ITEM_STORE or VAR_ITEM_STORE
row that resolves OBJ (and KEY) by name when it runs, so a VAL that
rebinds OBJ stores into the new binding and an unbound OBJ raises after
VAL has run. A constant VAL folds into the row as VAR_ITEM_STORE_CONST or
VAR_CONST_ITEM_STORE_CONST, leaving the stack untouched. Otherwise compiles OBJ; if KEY is constant, compiles VAL and
emits CONST_ITEM_STORE; else merges the key fragment, compiles VAL, emits
ITEM_STORE 0. Then CONST None.

**Examples:**

```koskari
(set-item vec 0 99)
```

