# Making choices

Programs become more interesting when they can choose one path or another. This
chapter introduces the beginner version of **conditionals**: ask a question,
then run the matching branch.

---

## A yes/no question

> **Q:** How do we ask Koskari to choose?
>
> **A:** Use `if` with a question and an indented body:
>
> ```koskari
> define temperature 72
>
> if (> temperature 70):
>   (print "warm enough for a walk")
> ```
>
> Read it as: “if `temperature` is greater than `70`, print the message.”

> **Q:** What does `>` do?
>
> **A:** It compares numbers. `(< 3 10)` is true, `(> 3 10)` is false, and
> `(equal? 3 3)` is true. Comparison calls are still calls: the first word says
> what to do, and the rest are inputs.

---

## Two paths

> **Q:** What if we want one message when the question is false?
>
> **A:** Add `else:` at the same indentation as `if`:
>
> ```koskari
> define score 8
>
> if (>= score 10):
>   (print "bonus level unlocked")
> else:
>   (print "keep going")
> ```
>
> Only one body runs.

> **Q:** Is `else:` a call?
>
> **A:** No. It is layout punctuation for the conditional block. The indented
> lines underneath are the body Koskari runs for that branch.

---

## More than two paths

> **Q:** Can one program choose among several cases?
>
> **A:** Use `elif` (short for “else if”):
>
> ```koskari
> define coins 17
>
> if (>= coins 20):
>   (print "buy the big snack")
> elif (>= coins 10):
>   (print "buy the small snack")
> else:
>   (print "save your coins")
> ```
>
> Koskari tests from top to bottom and uses the first true branch.

---

## A conditional as a value

> **Q:** Can a choice produce a value instead of printing immediately?
>
> **A:** Yes. The value of a conditional is the value of the branch that ran:
>
> ```koskari
> define age 12
>
> define price:
>   if (< age 13):
>     5
>   else:
>     9
>
> (print "ticket price:" price)
> ```
>
> This is handy when you want to name the decision and use it later.

---

## Try it

Change one number each time and predict the output before running:

```koskari
define steps 4500

if (>= steps 10000):
  (print "goal met")
elif (>= steps 5000):
  (print "halfway there")
else:
  (print "short walk today")
```

---

## What you should remember

- `if` runs a body when its question is true.
- `else:` is the fallback branch.
- `elif` lets you test more questions in order.
- A branch can print, define names, call functions, or produce a value.

Next: [Tiny functions](06-tiny-functions.md).
