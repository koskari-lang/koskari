# Functions and scope

Python’s `def`, `lambda`, closures, and scoping have Koskari counterparts.
Multi-line functions use the same **layout blocks** as other definitions: a
header with formals, then an indented body.

---

## What this chapter builds

You will define named functions with defaults and keyword parameters, use `let`
for locals, attach Python decorators to Koskari definitions, and see how
closures behave like Python’s nested functions.

---

## Defining functions

> **Q:** `def double(x): return x * 2`?
>
> **A:**
>
> ```koskari
> define double [x]:
>   (* x 2)
> ```
>
> Square brackets hold **formals**. The last expression in the body is the
> return value unless you use **`return`**.

> **Q:** `lambda x: x + 1`?
>
> **A:**
>
> ```koskari
> (lambda [x] (+ x 1))
> ```
>
> Anonymous functions stay compact in parentheses.

> **Q:** Default and keyword parameters?
>
> **A:**
>
> ```koskari
> define greet [name greeting: "hello"]:
>   (print greeting name)
> ```
>
> See [lambda](../../reference/specials/compile-lambda.md) for `*`, `**`, and
> type annotations (`::` / `type:` on formals or return types).

> **Q:** Can a function write back to the caller's variable? (Python can't.)
>
> **A:** Yes — mark the parameter **by-ref** with a leading `&`. The parameter
> then aliases the caller's binding instead of copying the value, so `set!`
> writes through. Python has no equivalent (you'd return a value or mutate a
> shared object); this is closer to C++ `&` references.
>
> ```koskari
> define add-a [a &result]:
>   (set! result (+ a 1))
>
> define value 100
> (add-a value &value)    ; value is now 101
> ```
>
> The `&result` in the formals marks the by-ref parameter; the `&value` at the
> call passes a reference to bind it to. By-ref is allowed on positional and
> keyword parameters only — not `*` / `**`. See
> [bindings and refs](../../reference/concepts/bindings-and-refs.md).

> **Q:** Multi-line body with layout?
>
> **A:** Indent every statement under the header — same rhythm as Python `def`:
>
> ```koskari
> define fib-loop [n]:
>   define v 0
>   define accu 1
>   while (> n 0):
>     decr! n
>     set! v accu
>     set! accu (+ accu v)
>   v
> ```

---

## Explicit return

> **Q:** `return value` inside a function?
>
> **A:** **`return`** — use for early exit or when the last form should not be
> the result. Short functions often omit it.

---

## Decorators

> **Q:** Can I use Python decorators on Koskari functions?
>
> **A:** Yes. Stack `@` forms above `define` (or `define-class`):
>
> ```koskari
> import-from functools [lru_cache as: memoize]

> @(memoize maxsize: None)
> define expensive [n]:
>   ...
> ```
>
> The `@demand` decorator ties into Koskari’s
> demand vs async evaluation paths. For background see
> [Demand, async, and defer](../../reference/concepts/demand-async-and-defer.md);
> you do not need the full model to use ordinary `@memoize` on demand-marked code.

---

## Local bindings

> **Q:** Temporary locals without mutating outer names?
>
> **A:** **`let`** with a layout body:
>
> ```koskari
> let [[x 1] [y 2]]:
>   (+ x y)
> ```
>
> Compare Python: `x, y = 1, 2` at module scope leaks; `let` limits bindings to
> the body.

> **Q:** Named `let` for recursion?
>
> **A:**
>
> ```koskari
> let loop [[n 10]]:
>   when (> n 0):
>     (print n)
>     (loop (- n 1))
> ```
>
> Many Python readers prefer `while` or `for-each` until tail recursion is
> familiar.

---

## Closures

> **Q:** Does Koskari close over outer variables?
>
> **A:** Yes. Inner `define` / `lambda` bodies see outer bindings unless
> shadowed by a formal or `let` binding — same practical model as Python.

> **Q:** Mutable closure pitfalls?
>
> **A:** Same as Python: shared mutable objects vs rebinding outer names behave
> differently. Pass values explicitly while learning.

---

## Scope and modules

> **Q:** Module-level `define` vs function-local bindings?
>
> **A:** Top-level forms in a `.ki` file run at load time into the module
> namespace, like executing a Python module body. **`let`** creates a child scope
> for its body only.

> **Q:** `global` / `nonlocal`?
>
> **A:** Koskari has **`global`** and **`local`** declaration macros for
> intentional outer assignment. See
> [global and local](../../reference/macros/compile-global-and-compile-local.md).

---

## Coroutines and async

> **Q:** `async def`?
>
> **A:** Koskari’s evaluator is async-native. Many Python calls that return
> awaitables are auto-awaited at call sites. Use **`await`** when you need
> explicit control; **`define-coroutine`** marks coroutine definitions. See
> [await](../../reference/specials/compile-await.md) when you build async apps
> when you build async HTTP services or other `asyncio` apps.

---

## Higher-order functions

> **Q:** Passing functions as values?
>
> **A:** Natural — functions are values. Bind a `define` or `lambda` and pass it
> to Python’s `map`, or use Koskari’s `function-compose` macro for point-free
> style.

---

## Runnable mini-program

Save as `greet.ki`:

```koskari
define greet [name greeting: "hello" punct: "!"]:
  (print (+ greeting " " name punct))

when-main:
  (greet "Ada")
  (greet "Grace" greeting: "Hi" punct: ".")
```

Run:

```bash
koskari greet.ki
```

Expected:

```text
hello Ada!
Hi Grace.
```

---

## Try it

Define at the REPL with a layout block, then call:

```koskari
koskari> define sq [x] (* x x)
koskari> (sq 12)
```

You should see `144`.

---

## Remember

`define` / `lambda` replace `def` / `lambda`. Formals use `[...]`. Layout bodies
return the last expression unless `return` says otherwise. `let` gives local
scope; `@` decorators wrap definitions like in Python.

Next: [Classes and objects](classes-and-objects.md).
