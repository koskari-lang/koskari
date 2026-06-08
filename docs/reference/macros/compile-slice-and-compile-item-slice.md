# compile/slice and compile/item-slice
**Symbols:** `compile/slice` (surface: `slice`), `compile/item-slice` (surface: `item-slice`)

## compile/slice

**Syntax:** `(slice)` or `(slice ARGS …)`

**Behaviour:** Routes slice construction through the [build-slice](../runtime/build-slice.md)
function so keyword arguments are accepted. **`(slice)`** expands to
**`(call build-slice)`**; **`(slice ARGS …)`** expands to **`(call build-slice ARGS …)`**.

## compile/item-slice

**Syntax:** `(item-slice SEQUENCE)` or `(item-slice SEQUENCE ARGS …)`

**Behaviour:** Slice a sequence via **`item`**. **`(item-slice SEQUENCE)`** expands to
**`(item SEQUENCE (call build-slice))`**. With slice arguments:

```koskari
(item-slice sequence start ...)
```

is equivalent to:
```koskari
(item sequence (slice start ...))
```
