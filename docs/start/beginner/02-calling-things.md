# Calling things

Most of Koskari work is **calling** something with inputs. This chapter states
the rule in plain language and practices it.

---

## The calling rule

> **Q:** What is a call?
>
> **A:** A call is a parenthesized group whose **first word** is the thing to
> do. The rest are **inputs**:
>
> ```koskari
> (print "Hello")
> ```
>
> Here `print` is the thing to do. `"Hello"` is the input.

> **Q:** What is `(+ 1 2)`?
>
> **A:** A call. `+` is the thing to do. `1` and `2` are inputs.

> **Q:** Before you run it — what should `(+ 1 2)` produce?
>
> Think, then try at the REPL:
>
> ```koskari
> koskari> (+ 1 2)
> ```
>
> **A:** `3`.

> **Q:** What about `(+ 10 5)`?
>
> **A:** Predict first: `15`. Then check at the REPL.

---

## More than one input

> **Q:** Can a call have many inputs?
>
> **A:** Yes:
>
> ```koskari
> (+ 1 2 3 4)
> ```
>
> Try it. `+` adds all the inputs together.

> **Q:** What is `(print "a" "b" "c")`?
>
> **A:** A call to `print` with three inputs. `print` sends each piece to the
> screen in order. Try it and read what appears.

---

## Giving a name a value

> **Q:** Can we remember a value under a name?
>
> **A:** Yes. Use `define`:
>
> ```koskari
> koskari> (define x 6)
> koskari> (define y 7)
> ```
>
> After that, `x` means `6` and `y` means `7`.

> **Q:** What is `(print x y (* x y))`?
>
> **A:** A call to `print` with three inputs: the value of `x`, the value of
> `y`, and the value of `(* x y)`. Predict the middle piece before you run it.
> Then try the whole call.

---

## Calls inside calls

> **Q:** Can an input be another call?
>
> **A:** Yes. That is how `(* x y)` sits inside the `print` call above. Koskari
> works from the inside out: inner calls finish first, then outer calls use
> their results.

> **Q:** What is `(+ 1 (+ 2 3))`?
>
> **A:** Predict: the inner call is `5`, then `+` adds `1` and `5` to get `6`.
> Check at the REPL.

---

## What you should remember

A call looks like `(thing input ...)`. The first word after `(` is what happens;
the rest are inputs. Inputs can be numbers, text, names, or other calls.

Next: [Results and messages](03-results-and-messages.md).
