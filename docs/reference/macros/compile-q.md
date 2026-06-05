# compile/?
**Symbol:** `compile/?` (surface: `?`)

**Syntax:** `(? COND THEN ELSE)`, `(? COND THEN)` (else **`False`**), or `(? COND)` (boolean test only).

**Behaviour:** Two- and three-argument forms expand to **`compile/cond`** with an **`else:`** arm. The unary form expands to **`(compile/truth COND)`**.
