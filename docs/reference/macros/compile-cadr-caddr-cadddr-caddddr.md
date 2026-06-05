# cadr, caddr, cadddr, caddddr, first … fifth

**Syntax:** **`(cadr pair)`**, **`(caddr pair)`**, **`(cadddr pair)`**,
**`(caddddr pair)`**; also **`(first pair)`** … **`(fifth pair)`**

**Behaviour:** **`cadr`**, **`caddr`**, **`cadddr`**, and **`caddddr`** expand to nested
**`car`** / **`cdr`**. **`first`** … **`fifth`** are aliases for **`car`** …
**`caddddr`** respectively (**`second`** = **`cadr`**, **`third`** = **`caddr`**, and so
on).
