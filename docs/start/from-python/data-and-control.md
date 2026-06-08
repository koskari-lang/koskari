# Data and control

Values and control flow are where Python habits help most. This chapter
assumes you have read [Layout and calls](layout-and-calls.md) — examples below
use indented blocks, not only parenthesized forms.

---

## What this chapter builds

You will bind names, branch with `if` / `elif` / `else`, loop with `while` and
`for-each`, and use `incr!` / `decr!` for counter updates. Everything still
evaluates to Python objects.

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
> ```koskari
> #(1 2 3)
> #{(a . 1) (b . 2)}
> ```
>
> Evaluation produces normal Python tuple and dict values.

> **Q:** Linked lists?
>
> **A:** Often built with **`cons`** for macro and list-processing code:
>
> ```koskari
> (cons 1 2 3 nil)
> ```
>
> For everyday data, prefer `#[…]` and Python APIs.

---

## Assignment

> **Q:** `x = 6` at module level?
>
> **A:**
>
> ```koskari
> define x 6
> ```
>
> Or `(define x 6)` on one line.

> **Q:** Reassign `x = 7`?
>
> **A:**
>
> ```koskari
> set! x 7
> ```

> **Q:** `x += 1` / `n -= 1`?
>
> **A:** Use **`incr!`** and **`decr!`** (macros over inplace update):
>
> ```koskari
> incr! x
> decr! n
> decr! n 2
> ```
>
> Typical in counting `while` loops. See
> [incr! / decr!](../../reference/macros/compile-incr-and-compile-decr.md).

> **Q:** Subscript and attribute assignment?
>
> **A:** `set!` accepts `(item obj key)` and `(attr obj name)` as assignment
> targets, same idea as Python’s `obj[k] = v` and `obj.attr = v`.

---

## Conditionals (layout)

For the relationship between layout blocks, layout lines, and s-expressions when
writing conditionals, see
[Layout and s-expressions](../../reference/concepts/layout-and-s-expressions.md).

> **Q:** `(if test a b)`?
>
> **A:** Layout peers (preferred in `.ki` files):
>
> ```koskari
> if test:
>   a
> else:
>   b
> ```
>
> One-arm `if` without else yields `None`. Use **`when`** when you only need the
> true branch.

> **Q:** `elif` chains?
>
> **A:** `elif` peers after the first body (same as Python):
>
> ```koskari
> if (lt x 0):
>   "negative"
> elif (equal? x 0):
>   "zero"
> else:
>   "positive"
> ```
>
> Under the hood this becomes **`cond`**. You can also write `(cond …)` directly;
> see [compile/cond](../../reference/specials/compile-cond.md).

> **Q:** `if test: body` with no else?
>
> **A:**
>
> ```koskari
> when test:
>   body
> ```

> **Q:** `if not test: body`?
>
> **A:**
>
> ```koskari
> unless test:
>   body
> ```

### Fast-return checks with `unless`

Python code often starts with a guard clause: check the boring case, return
early, then let the rest of the function tell the happy-path story.

```python
def invite(user):
    if not user:
        return "missing user"
    if not user.email:
        return "missing email"
    return f"sent invite to {user.email}"
```

In Koskari, **`unless`** makes those negative checks read like a little safety
rail at the top of the function:

```koskari
define invite [user]:
  unless user:
    return "missing user"
  unless user.email:
    return "missing email"
  f"sent invite to {user.email}"
```

That is a macro, not a new kind of `if`: it expands to a conditional whose body
runs only when the test is false. The s-expression spelling is handy when the
check is nested inside another form:

```koskari
(lambda [user]
  (unless user (return "missing user"))
  (send-invite user))
```

Use `unless` for short, local “stop here if this is not true” moments. If the
negative branch grows large, an `if` / `else:` pair is usually kinder to future
you.

---

## Loops (layout)

> **Q:** `while test: body`?
>
> **A:**
>
> ```koskari
> while test:
>   body
> ```
>
> Optional **`else:`** peer (runs when the test becomes false, not when `break`
> fires), matching Python:
>
> ```koskari
> while (> n 0):
>   decr! n
> else:
>   "done"
> ```

> **Q:** `for item in seq: body`?
>
> **A:**
>
> ```koskari
> for-each [item seq]:
>   body
> ```
>
> Works on Python iterables. Supports `break`, `continue`, and an `else:` peer
> like `while`. See [layout/for-each](../../reference/layouts/layout-for-each.md).

> **Q:** `break` / `continue`?
>
> **A:** `(break [value])` and `(continue)` inside `while` and `for-each`.

---

## Order of evaluation

> **Q:** Are arguments evaluated left-to-right like Python 3?
>
> **A:** Yes for nested calls: inner forms finish before outer calls use their
> values.

> **Q:** Short-circuit `and` / `or`?
>
> **A:** Available as special forms — same practical idea as Python’s `and` /
> `or`. See the specials reference when you need exact rules.

---

## Runnable mini-program: countdown

Save as `countdown.ki`:

```koskari
define countdown [n]:
  while (> n 0):
    print n
    decr! n
  else:
    print "lift-off"

when-main:
  (countdown 3)
```

Run:

```bash
koskari countdown.ki
```

Expected:

```text
3
2
1
lift-off
```

Full recipe: [Countdown pattern](../../patterns/recipes/countdown.md).

---

## Try it

In the REPL, bind a list and walk it:

```koskari
koskari> define total 0
koskari> for-each [x #[1 2 3]]:
  ....   incr! total x
koskari> total
```

You should see `6` (sum of the list).

---

## Remember

Literals and Python collections interop directly. Use `define` / `set!` /
`incr!` / `decr!` for bindings and updates. Branch and loop with layout blocks;
truthiness matches Python; `nil` does not.

Next: [Functions and scope](functions-and-scope.md).
