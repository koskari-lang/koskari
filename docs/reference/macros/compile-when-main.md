# compile/when-main
**Symbol:** `compile/when-main` (surface: `when-main`)

**Syntax:** `(when-main BODY ...)`

**Behaviour:** Expands to **`when (equal? __name__ "__main__"): (begin BODY ...)`**.

