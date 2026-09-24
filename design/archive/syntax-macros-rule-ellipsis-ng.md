# Syntax macros (RULE only): ellipsis, templates, and NG implementation status

**Status:** Archived. The hygiene names it traces were replaced by hygiene
by marks; see [gensym-atom-subtype.md](gensym-atom-subtype.md) and the
[SYNTAX.md](../SYNTAX.md) rollup.

This document is **design and inventory for a future implementation**. It
describes one important use case (ellipsis with repeated sub-patterns), traces
the **actual** `syntax` + `hygiene` pipeline for **syntax macros built
from RULE specifiers**, and states **precisely** what exists today versus what
does not.

**In scope**

- **Syntax macros** only: `Syntax` arms that are `SyntaxTransform` (pattern +
  **template**), not `MacroTransform` (procedure), not `SpecialTransform`.
- **RULE pattern specifiers** only: patterns lowered by
  `parse_syntax_rules_pattern` / `_syntax_pattern_to_pattern` in
  `koskari/syntax.py`. **Not** MATCH-dialect rules from
  `parse_explicit_rule_pattern` / `build_syntax_match` (different lowerer,
  different surface).

**Out of scope (do not read this doc as describing them)**

- Procedural **macro-rules** (`MacroTransform`, Ki `**captures`).
- **Special** forms (`SpecialTransform`, `special(...)`).
- **Removed** legacy **`koskari.syntax`** box types / **`SyntaxRule`** unless explicitly cited as a
  **contrast** for behavior that NG does not replicate yet.

---

## 1. Terminology

- **Use-site form**: the compile-time datum being expanded (proper list spine,
  `nil`-terminated), e.g. `(+ (1 2) (2 3) (3 4))`.
- **Pattern datum**: first element of a RULE row; lowered to a
  `matcher.Pattern` then frozen native matcher tuple.
- **Template datum**: second element of a RULE row; **not** interpreted as
  Scheme `quasiquote`; it is a **pair tree** walked by `hygiene` with
  `Ellipsis` markers in the spine for repetition (see below).
- **`result.captures`**: string-keyed dict from `Matcher.match` (native engine).
- **`bindings`**: dict passed to `expand_template_with_hygiene`: keys are
  **koskari symbols** (pattern variables); values are whatever substitution
  expects—**scalars** or **Python `list`** for ellipsis-driven repetition.

---

## 2. End-to-end pipeline (RULE syntax macros only)

```text
build_syntax_from_datums(name, literals, rules)
  for each (pattern_datum, template_datum):
    ptree = parse_syntax_rules_pattern(pattern_datum, literals, ...)
    SyntaxTransform(ptree, template_datum)
  → Syntax(..., transforms=(SyntaxTransform, ...))

At expansion time (simplified):
  Syntax.compile_impl → SyntaxTransform.try_expand
    result = matcher.match(source_obj)
    bindings = _captures_to_bindings(result.captures)
    return apply_syntax_rules_hygiene(
        template, bindings, compiler.find_compiled)
```

Sources:

- `build_syntax_from_datums`, `SyntaxTransform.try_expand`:
  `koskari/syntax.py`
- `apply_syntax_rules_hygiene`:
  `koskari/hygiene.py`

There is **no** second pass between `_captures_to_bindings` and
`apply_syntax_rules_hygiene` that reshapes repeat captures into per-variable
lists.

---

## 3. Motivating example (desired semantics)

This is the **user-level story** for why ellipsis + nested pattern + template
ellipsis must line up. Surface syntax below is **illustrative**; Koskari’s
actual reader forms may differ. The **structure** is what matters: pattern and
template share an ellipsis **depth** so `x` and `y` advance **together**.

### 3.1 Pattern and template (conceptual)

**Pattern** (RULE list spine, conceptual):

```text
(something (x y) ...)
```

Meaning: fixed head `something`, then **zero or more** repetitions of a **pair**
`(x y)`.

**Template** (conceptual): repeat a skeleton under the same ellipsis discipline:

```text
(begin #((something x y) ...))
```

(Here `#(( ... ) ...)` means: a **vector-like** sequence whose elements are
built by repeating `(something x y)` once per matched pair.)

### 3.2 Example use-site and intended expansion

**Use-site:**

```text
+ (1 2) (2 3) (3 4)
```

(Interpret as: head `+`, then three `(x y)`-shaped actuals.)

**Intended expanded shape** (conceptual):

```text
#( (+ 1 2) (+ 2 3) (+ 3 4) )
```

So each repetition contributes **one** combined form using the **i-th** `x`
and **i-th** `y`. The template engine must have a way to obtain **parallel**
columns `x → [1,2,3]`, `y → [2,3,4]` (or `Datum`-wrapped equivalents) with the
**same length**, then expand `(something x y) ...` **N** times with a shared
index.

This document’s **§6–§8** explain what **must** be true in `bindings` for
`hygiene` to do that, and what **NG actually supplies today**.

---

## 4. RULE pattern lowering today (`parse_syntax_rules_pattern`)

Entry: `parse_syntax_rules_pattern` → `_parse_rule_pattern` →
`_syntax_pattern_to_pattern` / `_syntax_pattern_specs` / `_syntax_elem_pattern`
in `koskari/syntax.py`.

### 4.1 Single symbol at rule top

A pattern that is **just** a non-literal symbol becomes:

```text
MATCH(REST("<name>"))
```

So the **entire** use-site tail is one `REST` capture under that name (native
matcher semantics: one slot, one `Datum` for the tail spine).

### 4.2 List pattern without ellipsis

Each non-literal symbol in a list segment becomes `BIND("name")` inside nested
`FORM` / `MATCH` as produced by `_syntax_elem_pattern`.

### 4.3 Ellipsis in a list: `_syntax_pattern_specs`

When the pattern spine has `pat` followed by Python `Ellipsis`:

- **Bare symbol** `pat` not in literals: today the code builds  
  `repeat_capture(str(pat), CAPTURE(str(pat)))`  
  i.e. **outer slot name equals inner capture name** (`koskari/syntax.py`,
  `_syntax_pattern_specs`). That duplicates the same string for two different
  roles (outer list of frames vs per-iteration slot) and is **known-broken**
  for capture semantics (see discussion elsewhere; this doc records **fact**,
  not the fix).

- **Non-symbol** `pat` (e.g. nested list `(x y)`):  
  `repeat_capture(str(gensym("_repeat")), _syntax_elem_pattern(pat, ...))`  
  The outer repeat slot is an **internal** name such as `"_repeat"` + uniquifier.
  Inner `x` / `y` are `BIND` nodes **inside** the repeated inner pattern.

### 4.4 What the native matcher stores for `repeat_capture`

From `matcher.repeat_capture` and native tests (e.g.
`tests/native/test_matcher.c`, `test_match_repeat_named_per_iteration_frames`):

- **`captures[outer_slot]`** is a **Python `list`**.
- Each element is typically a **`dict`**: keys are **inner** capture slot names
  for that iteration; values are usually **`Datum`** (or unwrap similarly).

Inner variables like `val` in the C test are **not** duplicated as top-level
`captures["val"]` when they only appear inside repeat frames.

---

## 5. What `_captures_to_bindings` does today

Definition: `koskari/syntax.py`, `_captures_to_bindings` +
`_unwrap_syntax_binding_value`.

**Implemented**

- For **each top-level key** `k` in `result.captures`, produce
  `bindings[symbol(k)] = _unwrap_syntax_binding_value(v)`.
- `_unwrap_syntax_binding_value` recurses through `Datum`, `dict`, and `list`
  to peel `Datum` wrappers and handle certain single-key `__sr` dict shapes
  (legacy ellipsis-helper convention).

**Not implemented**

- **No** logic equivalent to the **removed** legacy helper
  **`_syntax_match_result_to_bindings`** (formerly in **`koskari.syntax`**) that **folds** a list of per-iteration **dict frames** into **multiple**
  top-level keys whose values are **lists** of the same length—e.g. turning

  ```python
  {"_repeat0": [{"x": Dx0, "y": Dy0}, {"x": Dx1, "y": Dy1}]}
  ```

  into

  ```python
  {symbol("x"): [Dx0, Dx1], symbol("y"): [Dy0, Dy1]}
  ```

  for template consumption.

Therefore: **even if** the matcher produces perfect per-frame dicts under a
gensym outer key, **`SyntaxTransform` does not today promote `x` / `y` into
`bindings[x]` / `bindings[y]` lists** for `expand_template_with_hygiene`.

---

## 6. What `apply_syntax_rules_hygiene` / `expand_template_with_hygiene`
    implement today

### 6.1 Entry

`apply_syntax_rules_hygiene(template, bindings, find_compiled)`  
(`koskari/hygiene.py`)

- Computes `binding_symbols` via `collect_binding_symbols(template, find_compiled)`.
- Calls `expand_template_with_hygiene(template, bindings, binding_symbols, {})`.

### 6.2 Ellipsis in the **template** spine

`expand_template_with_hygiene` flattens the template with `unpack(template)` to
`items`. When `items[i+1] is Ellipsis`:

1. **`_repeat_count(elem, bindings)`** collects every **symbol** appearing under
   `elem` (including nested pairs). For each such symbol `s` that appears in
   `bindings` with a **Python `list`** value, it records `len(bindings[s])`.
2. All such lengths must be **equal**; otherwise
   `CompilerError('incompatible ellipsis repetition lengths', template)`.
3. The loop `for rep_i in range(count)` re-expands **`elem`** with
   `index=rep_i`.
4. When a **symbol** is expanded with `index` set and `bindings[template]` is
   a **`list`**, the result is **`val[index]`** (`ellipsis variable outside
   repeated template` if list-valued but `index is None`).

**Implemented**: full **template-side** ellipsis machinery **given** the right
`bindings` shape.

**Not implemented here**: anything that **creates** list-valued entries from
native repeat frames—that is **upstream** in `_captures_to_bindings` (today
absent).

### 6.3 Concrete template shape requirement

For the motivating example, the template cannot be “magic”; it must be a spine
where **one list cell** holds the repeated **subform** and the **next** cell is
`Ellipsis`, e.g. unpacked like:

```text
...,  (begin #((something x y))),  Ellipsis,  ...
```

(exact nesting depends on how `#(` desugars into pair structure—**whatever**
ends up as “`elem` followed by `Ellipsis` in the same `unpack` sequence” is what
the expander matches.)

If `x` and `y` never appear as **top-level** `bindings` keys with **list**
values, `_repeat_count` sees **no** list-valued symbols under `elem` and returns
**0**, so the template ellipsis produces **zero** copies (silent empty
expansion for that segment), not an error—another sharp edge for implementers.

---

## 7. Status matrix (RULE syntax macros only)

| Concern | Implemented today? | Where / notes |
|--------|---------------------|---------------|
| RULE patterns lowered via `parse_syntax_rules_pattern` | Yes | `syntax.py` |
| List patterns with `BIND` / `LITERAL` / nested `FORM` | Yes | `_syntax_elem_pattern` |
| Bare `id ...` lowered with distinct outer/inner slots | **No** (current code uses colliding names) | `_syntax_pattern_specs` |
| Native `repeat_capture` list-of-frame dicts | Yes | matcher engine + `matcher` |
| `_captures_to_bindings` unwraps `Datum` / nested lists / dicts | Yes | `syntax.py` |
| **Transpose** repeat frames → `{pattern_var: […]}` for template | **No** | **Gap** vs legacy `syntax.py` |
| `expand_template_with_hygiene` ellipsis + `index` | Yes | `hygiene.py` |
| `apply_syntax_rules_hygiene` | Yes | `hygiene.py` |
| MATCH-dialect `build_syntax_match` | Different code path | **Out of scope** for this doc |

---

## 8. Worked micro-examples (bindings truth table)

Assume pattern variables appear as **symbols** `x`, `y` in the template. Below
describes what **`bindings`** must contain for **template ellipsis to work**,
vs what **NG** produces today.

### 8.1 No ellipsis in template

**Pattern** binds `foo` once (scalar). **Template** is `(bar foo)`.

**Need:** `bindings[foo]` = single value (not necessarily a list).

**NG:** OK if `result.captures["foo"]` is set appropriately after unwrap.

### 8.2 Bare `xs ...` in pattern, template uses `xs` outside ellipsis

**Pattern** `(head xs ...)` with intended semantics “`xs` is the tail after
`head`.”

**Template** `(list xs)` with **no** `...` after `xs`.

**Need:** `bindings[xs]` = one value (for `REST`-style tail) **or** list if the
design chooses list-of-cells—**either way** the template must agree.

**NG today:** Lowering still uses `repeat_capture` for bare symbol ellipsis (not
`REST` in the snippet this doc was written against); tail shape vs list-of-cells
is a **separate** design decision. `_captures_to_bindings` does not reinterpret
repeat lists into “tail `Datum`”.

### 8.3 Nested `(x y) ...` + template `(skeleton x y) ...`

**Need for `hygiene`:**

```text
bindings[x] == [Dx0, Dx1, ...]   # same length N
bindings[y] == [Dy0, Dy1, ...]
```

**NG today:** Typically only

```text
bindings[symbol("_repeat…")] == [ {"x": Dx0, "y": Dy0}, ... ]
```

(unwrapped). **No** `bindings[x]` / `bindings[y]` lists → template ellipsis does
**not** receive the parallel columns needed for the motivating example
(§3).

---

## 9. Contrast: removed legacy `_syntax_match_result_to_bindings` (reference only)

Before **`koskari/syntax.py`** was deleted, **`_syntax_match_result_to_bindings`** merged, for list-
shaped boxed repeat results, **merges** inner dict keys into list-valued
bindings:

```python
merged.setdefault(rk, []).append(val.datum)
```

That is exactly the **missing** transpose step for NG `SyntaxTransform` today.
The deleted legacy **`SyntaxRule.try_expand`** path called that helper before
`apply_syntax_rules_hygiene`.

**This doc does not propose switching RULE macros to legacy**; it records that
the **behavioral gap** is understood and previously implemented elsewhere.

---

## 10. Implementation checklist (later)

Use this as a task list when implementing; **no code here**.

1. **Decide** bare `id ...` lowering: `REST` vs `repeat_capture` + inner
   `TEST_ANY` vs other, and document resulting `captures["id"]` shape (single
   tail `Datum` vs list of per-cell values).
2. **Implement** a transpose (name TBD), either:
   - extend `_captures_to_bindings`, or
   - add `captures_to_syntax_rule_bindings(result.captures, pattern_metadata?)`,
     so list-of-dict repeat results become **symbol → list** entries for every
     inner pattern variable that appears under that ellipsis in the **RULE**
     pattern.
3. **Handle** outer-only gensym repeat keys: either remove the need for them in
   captures exposed to templates, or teach template discovery to ignore them.
4. **Tests** (RULE + `SyntaxTransform` only):
   - End-to-end: pattern `(m (x y) ...)`, template repeating `(m x y) ...`,
     fixed literals for `m`, assert expanded shape matches §3 style.
   - Regression: `_repeat_count` incompatible lengths → existing error.
   - Edge: ellipsis template segment with **no** list-valued symbols under
     `elem` → document whether you want error vs zero repeats.
5. **Explicitly exclude** from this workstream: `MacroTransform`,
   `SpecialTransform`, `build_syntax_match` / MATCH lowering—**or** add
   separate docs if the same transpose must apply there too.

---

## 11. File index (read when implementing)

| File | Relevance |
|------|-----------|
| `koskari/syntax.py` | `parse_syntax_rules_pattern`, `_syntax_pattern_specs`, `_captures_to_bindings`, `SyntaxTransform.try_expand`, `build_syntax_from_datums` |
| `koskari/hygiene.py` | `expand_template_with_hygiene`, `_repeat_count`, `apply_syntax_rules_hygiene` |
| Removed **`koskari/syntax.py`** | `_syntax_match_result_to_bindings` (historical; see git history) |
| `koskari/matcher.py` | `repeat_capture`, `REST`, `BIND`, `CAPTURE` |
| `koskari/lib/matcher.c` | Native `REST`, `CAPTURE`+`REPEAT` semantics |
| `tests/native/test_matcher.c` | Repeat frame shapes |
| `tests/hygiene/test_hygiene.py` | Template ellipsis errors / manual `bindings` |
| `tests/syntax/test_syntax_bootstrap.py` | RULE ellipsis pattern smoke tests |

---

## Document history

- Author intent: capture **syntax macros, RULE only**, ellipsis + template
  alignment (§3), and the **exact NG gap** between matcher output and
  `hygiene` expectations.
