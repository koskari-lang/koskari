# Model of evaluation

Both tutorial on-ramps meet here. This chapter states the evaluation story in
plain language. Later guide chapters add detail; reference pages give exact
rules per form.

---

## Three ideas

> **Q:** What happens when Koskari runs a program?
>
> **A:** Three ideas cover most of what you need:
>
> 1. **Read** — turn text into structure Koskari can walk.
> 2. **Evaluate** — run that structure to produce a value (or an effect).
> 3. **Data** — some values are data you can inspect; some are things you can
>    call.

> **Q:** Which did the beginner track already use?
>
> **A:** All three, without the names. When you typed `(+ 1 2)` at the REPL,
> Koskari read the text, evaluated the call, and showed the number `3` — data.

---

## Read

> **Q:** Is `(+ 1 2)` text or structure?
>
> **A:** On disk or at the keyboard it is **text**. After reading, Koskari
> treats it as a **call**: `+` with inputs `1` and `2`. Reading is strict: bad
> punctuation fails before evaluation starts (syntax errors from the beginner
> track).

> **Q:** Do numbers and strings need much reading?
>
> **A:** They are already simple values. `"hello"` reads as text data. `42`
> reads as a number.

---

## Evaluate

> **Q:** What does “evaluate” mean?
>
> **A:** Find the value a form denotes by following Koskari’s rules.

> **Q:** What is the value of `42`?
>
> **A:** `42`.

> **Q:** What is the value of `(+ 1 2)`?
>
> **A:** Evaluate `1` → `1`. Evaluate `2` → `2`. Apply `+` → `3`.

> **Q:** What is the value of `(define x 6)`?
>
> **A:** The side effect is: the name `x` now means `6`. The form’s value is
> not interesting to print at the REPL — that is why the beginner track stayed
> quiet after `define`.

> **Q:** What is the value of `(print "hi")`?
>
> **A:** `print` runs for effect (text on screen). The value of the call is
> not what you care about when learning — you care that `"hi"` appeared.

---

## Data vs things to call

> **Q:** Is `+` data?
>
> **A:** It is a **name** bound to something callable. You do not quote it in
> normal use — you put it first in a call.

> **Q:** Is `"+"` data?
>
> **A:** It is **text data** (a string). It is not the same as the callable `+`.
> **Not yet:** full quoting rules live in later guide and reference material.

> **Q:** What is `(+ 1 2)` before it runs?
>
> **A:** A **form** — structure that can be evaluated. After it runs, you get
> the number `3`.

---

## Order inside a call

> **Q:** In `(print (+ 1 2))`, what runs first?
>
> **A:** The inner call `(+ 1 2)` evaluates to `3`. Then `print` receives `3`
> as its input. Same rule as the beginner track: **inside before outside**.

---

## Predict, then check

> **Q:** What is `(+ (+ 1 2) (* 3 4))`?
>
> **A:** Work it out before the REPL:
>
> 1. `(+ 1 2)` → `3`
> 2. `(* 3 4)` → `12`
> 3. `(+ 3 12)` → `15`
>
> Then type it at `koskari>` and confirm.

---

## What we defer

> **Q:** What is this chapter not explaining?
>
> **A:** Macros (code that rewrites code before evaluation), layout blocks
> (indent syntax that reads into forms), bytecode, and the full catalog of
> special forms. Those have guide and reference sections. The rule here stays
> the same: **forms evaluate to values; calls evaluate their inputs, then
> apply the first position.**

---

## What you should remember

Reading turns text into forms. Evaluation turns forms into values (and
effects). Nested calls evaluate inner forms first. When in doubt, predict the
value, then check at the REPL.

Next in the guide (when written): [Forms and combining](01-forms-and-combining.md).
