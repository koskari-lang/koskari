# Functions and scope

Python’s `def`, `lambda`, closures, and scoping have Koskari counterparts.
This chapter maps them without pretending the models are identical.

---

## Defining functions

> **Q:** `def double(x): return x * 2`?
>
> **A:**
>
> ```scheme
> (define double [x]
>   (* x 2))
> ```
>
> Square brackets hold **formals** (parameter names). The body is one or more
> expressions; the last expression’s value is the return value — like implicit
> return in a one-expression Python function, but for multi-expression bodies
> too unless you use explicit `return`.

> **Q:** `lambda x: x + 1`?
>
> **A:**
>
> ```scheme
> (lambda [x] (+ x 1))
> ```

> **Q:** Default and keyword parameters?
>
> **A:** Supported in formals, Python-style:
>
> ```scheme
> (define greet [name greeting: "hello"]
>   (print greeting name))
> ```
>
> See [lambda](../../reference/specials/compile-lambda.md) for `*`, `**`, and
> type annotations (`::` / `type:`).

---

## Explicit return

> **Q:** `return value` inside a function?
>
> **A:** **`return`** special form — use when you need early exit or clarity in
> longer bodies. Anonymous one-liners often omit it.

---

## Local bindings

> **Q:** Temporary locals without mutating outer names?
>
> **A:** **`let`** — lexical scope for the body:
>
> ```scheme
> (let [[x 1] [y 2]]
>   (+ x y))
> ```
>
> Compare Python:
>
> ```python
> x, y = 1, 2
> x + y   # but x,y leak unless in a function
> ```
>
> `let` limits bindings to the body.

> **Q:** Named `let` for loops?
>
> **A:** Recursion-shaped loop (Lisp idiom):
>
> ```scheme
> (let loop [[n 10]]
>   (when (gt n 0)
>     (print n)
>     (loop (- n 1))))
> ```
>
> Python readers often prefer `while` or `for-each` until recursion clicks.

---

## Closures

> **Q:** Does Koskari close over outer variables?
>
> **A:** Yes. A `lambda` or `define` body captures bindings from enclosing
> scopes — same practical idea as Python closures. Inner functions see outer
> names unless shadowed by a formal or `let` binding.

> **Q:** Mutable closure pitfalls?
>
> **A:** Same class of bugs as Python: shared mutable objects and rebinding
> outer names behave differently. Prefer passing values explicitly while learning.

---

## Scope and modules

> **Q:** Module-level `define` vs function-local bindings?
>
> **A:** Top-level forms in a `.ki` file run at import/load time into the
> module namespace — analogous to executing a Python module body. **`let`**
> creates a child namespace for its body only.

> **Q:** `global` / `nonlocal`?
>
> **A:** Koskari has **`global`** and **`local`** declaration macros for
> intentional outer binding. Default **`setq`** / **`set!`** resolve names
> like Python’s LEGB-style lookup. See [global and local](../../reference/macros/compile-global-and-compile-local.md) when you need cross-scope assignment.

---

## Coroutines and async

> **Q:** `async def`?
>
> **A:** Koskari’s evaluator is **async-native**. Many Python calls that return
> awaitables are **auto-awaited** at call boundaries. For explicit control,
> **`await`** exists; **`define-coroutine`** macro marks coroutine definitions.
> **Not yet:** full async tutorial — see [await](../../reference/specials/compile-await.md) and guide I/O chapters.

---

## Higher-order functions

> **Q:** Passing functions as values?
>
> **A:** Natural — functions are values. `(map fn seq)`-style code works once
> `fn` is a `lambda` or `define` result. Compose macros exist for point-free
> style (`function-compose` in builtins).

---

## What you should remember

`define` / `lambda` replace `def` / `lambda`. Formals use `[...]`. Bodies
return the last expression unless `return` says otherwise. `let` gives local
scope; closures work like Python’s. Module load binds top-level names.

Next: [Errors and debugging](errors-and-debugging.md).
