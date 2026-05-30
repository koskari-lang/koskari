# Repeating patterns

The same rules appear again and again. This chapter repeats them with small
variations so the pattern sticks.

---

## The pattern: call with inputs

> **Q:** What is the shape of `(+ 1 2)`?
>
> **A:**
>
> ```text
> (  thing   input ...  )
>     ^       ^
>     |       +-- values you provide
>     +-- what to do
> ```

> **Q:** Does `(print "a" "b")` follow the same shape?
>
> **A:** Yes. `print` is the thing to do. `"a"` and `"b"` are inputs.

> **Q:** Does `(define total 0)` follow the same shape?
>
> **A:** Almost. `define` is the thing to do. `total` and `0` are inputs. The
> result is a name you can use in later calls.

---

## Nesting

> **Q:** What is `(+ (+ 1 2) 3)`?
>
> **A:** Predict step by step:
>
> 1. Inner: `(+ 1 2)` → `3`
> 2. Outer: `(+ 3 3)` → `6`
>
> Check at the REPL.

> **Q:** What is `(print (+ 2 3))`?
>
> **A:** The inner call produces `5`. `print` sends `5` to the screen. Try it.

> **Q:** What is `(+ 1 (+ 2 (+ 3 4)))`?
>
> **A:** Work from the inside out: `7`, then `9`, then `10`. Always the same
> rule — inner calls finish before outer ones use their results.

---

## Names in calls

> **Q:** If `(define width 10)` and `(define height 4)`, what is `(* width
> height)`?
>
> **A:** Predict `40`, then:
>
> ```koskari
> koskari> (define width 10)
> koskari> (define height 4)
> koskari> (* width height)
> 40
> ```

> **Q:** What is `(print "area:" (* width height))`?
>
> **A:** Predict, then run. `print` receives two inputs: the text `"area:"` and
> the number produced by `(* width height)`.

---

## A tiny program

> **Q:** Can we put several lines in a file?
>
> **A:** Yes. Each line is a form Koskari runs in order:
>
> ```koskari
> (define x 6)
> (define y 7)
> (print x y (* x y))
> ```
>
> Save as `numbers.ki` and run `koskari numbers.ki`. You should see three
> values printed on one line (from the single `print` call).

---

## What we are not doing yet

> **Q:** Are there other ways to write programs?
>
> **A:** Yes — layout blocks, macros, many more forms. **Not yet.** This track
> stops with nested calls and small scripts. The [shared
> guide](../../guide/00-model-of-evaluation.md) picks up the evaluation story
> both on-ramps converge on.

---

## What you should remember

Every new form still fits the calling rule until a chapter says otherwise.
Nested calls mean “compute the inside first.” Files run forms top to bottom.

Next: [Where next](where-next.md).
