# labels and flet

## Status

Proposed — evaluate and implement.

## Purpose

Add **`labels`** and **`flet`** as bootstrap macros for local function binding.
They are the standard CL-style spellings for “define several functions in this
scope, then run a body”:

- **`labels`** — forward declarations: names exist in the body before their
  **`define`** forms run; each function can call any other in the same
  **`labels`** form (mutual recursion).
- **`flet`** — each binding is an anonymous **`lambda`**; names shadow outer
  bindings but do not forward-reference siblings in the same **`flet`**.

Both expand to existing specials (**`let`**, **`define`**, **`function`**,
**`lambda`**) — no compiler or VM changes.

## Surface syntax

```koskari
(labels [[even? [n]
           (if (== n 0) True (odd? (- n 1)))]
          [odd? [n]
           (if (== n 0) False (even? (- n 1)))]]
  (even? 10))

(flet [[double [x] (* x 2)]]
  (double 21))
```

**`labels`** declarations: **`[[name formals body ...] ...]`** then body forms.

**`flet`** declarations: same shape; each **`name`** binds to **`(lambda formals body ...)`**.

## Expansion semantics

### labels

1. **`let`** bind every declared **`name`** to **`None`** (forward slots).
2. For each declaration, **`(define name (function name formals body ...))`**
   so self-calls and mutual calls resolve through the named **`function`**.
3. Evaluate body forms in that scope.

### flet

1. **`let`** bind each **`name`** to **`(lambda formals body ...)`**.
2. Evaluate body forms; inner **`name`** shadows outer **`name`** only.

## Implementation

| Item | Location |
| ---- | -------- |
| **`define-syntax compile/labels`** | **`koskari/bootstrap/_functions.ki`** |
| **`define-syntax compile/flet`** | **`koskari/bootstrap/_functions.ki`** |
| Surface aliases **`labels`** / **`flet`** | Same file or **`_variables.ki`** if assignment macros live there — prefer **`_functions.ki`** next to **`function-compose`** |

Use **`macro-rules`** (or **`macro-match`** where improper declaration lists
need structure). Match the declaration-list shape used elsewhere in bootstrap
(**`for-each`**, **`decorate`**).

## Tests

**`tests/basics/test_labels_flet.py`** (`phase_basics`):

| Case | Assert |
| ---- | ------ |
| **`labels` mutual recursion | **`even?`** / **`odd?`** on small **`n`** |
| **`flet` shadowing** | Inner **`flet`** binding hides outer function |
| **`labels` forward ref** | Sibling call compiles and runs |
| **`flet` no forward ref** | Reference to later sibling in same **`flet`** is a compile/name error (or documents expected behaviour) |

## User documentation

- **`docs/reference/macros/compile-labels.md`**
- **`docs/reference/macros/compile-flet.md`**
- **`design/MACROS.md`** — new § **`labels`** / **`flet`**
- Optional corpus record in **`docs/corpus/records/functions.yaml`**

## Implementation order

1. Macros in **`_functions.ki`**.
2. Basics-phase tests.
3. Reference pages and **`MACROS.md`** entry.

## Related

- [../MACROS.md](../MACROS.md) — bootstrap macro catalog.
- **`koskari/bootstrap/_functions.ki`** — **`function`**, **`define`**, **`decorate`**.
- [../SPECIALS.md](../SPECIALS.md) — **`compile/let`**, **`compile/function`**.
