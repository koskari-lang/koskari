# Tiny functions

A **function** is a named recipe. You give it inputs, it runs its body, and it
produces a result. Functions let you turn repeated work into one friendly name.

---

## Naming a recipe

> **Q:** How do we make our own thing to call?
>
> **A:** Use `define` with a name, a list of inputs, and an indented body:
>
> ```koskari
> define double [n]:
>   (* n 2)
> ```
>
> `double` is the function name. `[n]` means “this function expects one input,
> and inside the body that input is called `n`.”

> **Q:** How do we use it?
>
> **A:** Call it like any other function:
>
> ```koskari
> koskari> (double 8)
> 16
> ```

---

## The last line is the result

> **Q:** Where is `return`?
>
> **A:** Small Koskari functions usually return the value of their last body
> form:
>
> ```koskari
> define add-tax [price]:
>   define tax (* price 0.08)
>   (+ price tax)
> ```
>
> The `define tax ...` line sets up a helper name. The final `(+ price tax)` is
> the result.

---

## More than one input

> **Q:** Can functions take several inputs?
>
> **A:** Yes. Put each input name in the brackets:
>
> ```koskari
> define rectangle-area [width height]:
>   (* width height)
>
> (print "area:" (rectangle-area 6 7))
> ```

> **Q:** Can the inputs have friendly names?
>
> **A:** Choose names that make the body easy to read. `width` and `height` are
> better than `a` and `b` for a rectangle.

---

## A function that makes a choice

> **Q:** Can a function use `if`?
>
> **A:** Yes. Functions can contain the same forms you have already used:
>
> ```koskari
> define describe-score [score]:
>   if (>= score 10):
>     "great"
>   elif (>= score 5):
>     "good"
>   else:
>     "keep practicing"
>
> (print (describe-score 7))
> ```
>
> The branch value becomes the function result.

---

## Try it

Write this file as `badges.ki`, then change the two numbers and run it again:

```koskari
define badge [name points]:
  if (>= points 100):
    (print name "earned a gold badge")
  elif (>= points 50):
    (print name "earned a silver badge")
  else:
    (print name "is still warming up")

(badge "Ada" 120)
(badge "Grace" 40)
```

---

## What you should remember

- A function definition gives a reusable recipe a name.
- Inputs live in square brackets: `[name points]`.
- The last form in a function body is usually its result.
- Functions can call other functions, define helper names, and make choices.

Next: [Where next](where-next.md).
