# compile/slice and compile/item-slice
**Symbols:** `compile/slice` (surface: `slice`), `compile/item-slice` (surface: `item-slice`)

## compile/slice

**Syntax:** `(slice)` or `(slice ARGS ...)`

**Behaviour:** Builds a slice through the [build-slice](../runtime/build-slice.md)
function so keyword arguments are accepted. **`(slice)`** calls it with no
arguments; **`(slice ARGS ...)`** passes **ARGS** to it.

## compile/item-slice

**Syntax:** `(item-slice SEQUENCE)` or `(item-slice SEQUENCE ARGS ...)`

**Behaviour:** Subscripts **SEQUENCE** via **`item`** with the slice that
**`(slice ARGS ...)`** builds; with no **ARGS**, with the slice
**`(slice)`** builds.

**Also:** type [`slice`](../runtime/types-and-predicates.md#collections).
