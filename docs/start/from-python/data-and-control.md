# Data and control

Values and control flow are where Python habits help most — and where surface
syntax diverges.

---

## Atoms

> **Q:** Numbers and strings?
>
> **A:** Same literals as Python: `42`, `-3`, `"hello"`. Double quotes for
> strings.

> **Q:** Booleans?
>
> **A:** `True` and `False` — the Python singletons. Tests in conditionals use
> **Python truthiness** (empty collections false, `None` false, and so on).

> **Q:** `None`?
>
> **A:** Python `None`. Empty `cond` with no match also yields `None`.

> **Q:** What is `nil`?
>
> **A:** The empty list / end-of-chain marker for pair structure — not the same
> as `None`. `(nil? nil)` is true; `(None? nil)` is false.

---

## Collections

> **Q:** Python `[1, 2, 3]`?
>
> **A:** Reader shorthand:
>
> ```koskari
> #[1 2 3]
> ```
>
> Builds a Python **list**.

> **Q:** Tuple and dict?
>
> **A:**
>
> ```text
> #(1 2 3)
> #{(a 1) (b 2)}
> ```
>
> These read as constructor forms; evaluation produces normal Python tuple and
> dict values.

> **Q:** Linked lists?
>
> **A:** Often built with **`cons`**:
>
> ```koskari
> (cons 1 2 3 nil)    ; → (1 2 3) as nested pairs
> ```
>
> **Not yet:** full pair/list idioms live in the guide and reference.

---

## Assignment

> **Q:** `x = 6` at module level?
>
> **A:**
>
> ```koskari
> (define x 6)
> ```

> **Q:** Reassign `x = 7`?
>
> **A:**
>
> ```koskari
> (set! x 7)
> ```
>
> Plain `(setq x 7)` is the underlying special; `set!` is the usual macro.

> **Q:** `x += 1`?
>
> **A:**
>
> ```koskari
> (set! x (+ x 1))
> ```
>
> Augmented forms like `(set! x (+ x 1))` also have dedicated `set!` macro
> rules for `(set! x (+ x 1))`-style infix sugar — see [set!](../../reference/macros/compile-set.md).

---

## Conditionals

> **Q:** `if test: a else: b`?
>
> **A:**
>
> ```koskari
> (if test a b)
> ```
>
> Keyword form also works: `(if test then: a else: b)`. One-arm `if` without
> else yields `None`.

> **Q:** `elif` chains?
>
> **A:** Use **`cond`** — first truthful test wins:
>
> ```koskari
> (cond
>   [(lt x 0) "negative"]
>   [(eq x 0) "zero"]
>   [else: "positive"])
> ```

> **Q:** `if test: body` with no else (statement style)?
>
> **A:** **`when`**:
>
> ```koskari
> (when test body ...)
> ```

---

## Loops

> **Q:** `while test: body`?
>
> **A:**
>
> ```koskari
> (while test
>   body ...)
> ```
>
> Optional Python-like **`else`** on loop exit (runs when the test becomes
> false, not when `break` fires):
>
> ```koskari
> (while x
>   (set! x (- x 1))
>   [else: "done"])
> ```

> **Q:** `for item in seq: body`?
>
> **A:**
>
> ```koskari
> (for-each [item seq]
>   body ...)
> ```
>
> Works on Python iterables. Supports `break`, `continue`, and `[else: ...]`
> like `while`.

> **Q:** `break` / `continue`?
>
> **A:** `(break [value])` and `(continue)` inside `while` and `for-each`.

---

## Order of evaluation

> **Q:** Are arguments evaluated left-to-right like Python 3?
>
> **A:** Yes for nested calls: inner forms finish before outer calls use their
> values. `(print (+ 1 2))` evaluates `(+ 1 2)` first.

> **Q:** Short-circuit `and` / `or`?
>
> **A:** Available as special forms — same practical idea as Python’s
> `and`/`or` for boolean combinations. See reference for exact rules.

---

## Layout syntax (optional)

> **Q:** Can I write indent blocks instead of parentheses?
>
> **A:** Yes — **layout** handlers read indented blocks into the same forms
> above. Example shape (conceptual):
>
> ```koskari
> if condition:
>   body
> else:
>   other
> ```
>
> **Not yet:** full layout catalog — see [Layouts](../../reference/layouts/index.md). Parenthesized forms remain the contract the compiler sees.

---

## What you should remember

Literals and Python collections interop directly. Use `define` / `set!` for
bindings, `if`/`cond`/`when` for branches, `while`/`for-each` for loops.
Truthiness matches Python; `nil` does not.

Next: [Functions and scope](functions-and-scope.md).
