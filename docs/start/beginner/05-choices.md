# Choices

Programs become more useful when they can **choose** what to do. This chapter
adds one new shape: “if this is true, do that; otherwise do something else.”

---

## Truth questions

> **Q:** What kind of question can a program ask?
>
> **A:** A question whose answer is true or false. Koskari names those answers
> `True` and `False`:
>
> ```koskari
> koskari> True
> True
> koskari> False
> False
> ```

> **Q:** Can a call produce `True` or `False`?
>
> **A:** Yes. Comparison calls ask small questions:
>
> ```koskari
> koskari> (eq 3 3)
> True
> koskari> (gt 3 10)
> False
> ```
>
> Read `(eq 3 3)` as “is 3 equal to 3?” and `(gt 3 10)` as “is 3 greater
> than 10?”

---

## `if`

> **Q:** How do we choose between two answers?
>
> **A:** Use `if`:
>
> ```koskari
> koskari> (if (gt 10 5) "bigger" "smaller")
> "bigger"
> ```
>
> The shape is:
>
> ```text
> (if question answer-when-true answer-when-false)
> ```

> **Q:** What happens here?
>
> ```koskari
> (if (eq 2 3) "same" "different")
> ```
>
> **A:** Predict first. `(eq 2 3)` is `False`, so the whole `if` produces
> `"different"`.

---

## Choosing actions

> **Q:** Can each branch do something, like print a message?
>
> **A:** Yes:
>
> ```koskari
> (define temperature 72)
> (if (gt temperature 80)
>   (print "drink water")
>   (print "enjoy the day"))
> ```
>
> Only one of the two `print` calls runs.

> **Q:** Why are there line breaks?
>
> **A:** They are for human readers. This is still one parenthesized form. The
> final `)` closes the `if`.

---

## More than two choices

> **Q:** What if there are several cases?
>
> **A:** Use `cond`. Each bracketed clause has a question and an answer. The
> first true question wins:
>
> ```koskari
> (define score 86)
> (cond
>   [(gt score 89) "A"]
>   [(gt score 79) "B"]
>   [(gt score 69) "C"]
>   [else: "keep practicing"])
> ```
>
> For `score` equal to `86`, the result is `"B"`.

> **Q:** What is `else:`?
>
> **A:** The fallback. It means “if none of the earlier questions matched, use
> this answer.”

---

## A tiny script

Put this in `weather.ki`:

```koskari
(define temperature 72)
(define raining False)

(cond
  [raining (print "take an umbrella")]
  [(gt temperature 80) (print "wear something cool")]
  [else: (print "good day for a walk")])
```

Run it:

```bash
koskari weather.ki
```

Then change one value at the top and run it again. You are practicing the same
edit → run → read loop from chapter 0.

---

## What you should remember

- `True` and `False` are values.
- Comparisons such as `eq` and `gt` produce truth values.
- `(if question yes no)` chooses one of two results.
- `cond` is useful when there are several possible cases.

Next: [Small functions](06-small-functions.md).
