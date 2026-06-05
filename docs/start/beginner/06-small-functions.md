# Small functions

A **function** is a reusable action with a name. You have already called
functions like `print`, `+`, `eq`, and `gt`. Now you will define your own.

---

## Why functions?

> **Q:** Why not copy and paste the same calls everywhere?
>
> **A:** Because a name makes the idea easier to reuse and easier to change.
> Instead of repeating the same calculation, define it once.

---

## A first function

> **Q:** How do we define a function?
>
> **A:** Use `define` with a name, a parameter list, and a body:
>
> ```koskari
> (define square [x]
>   (* x x))
> ```
>
> Read this as: “define `square`; it takes one input named `x`; its result is
> `(* x x)`.”

> **Q:** How do we call it?
>
> **A:** Put the function name first, just like `+` or `print`:
>
> ```koskari
> koskari> (define square [x] (* x x))
> koskari> (square 5)
> 25
> ```

---

## Parameters are temporary names

> **Q:** Is `x` always `5` after `(square 5)`?
>
> **A:** No. `x` is a temporary name inside the function call. Each call gets
> its own value:
>
> ```koskari
> koskari> (square 3)
> 9
> koskari> (square 12)
> 144
> ```

> **Q:** Can a function have two inputs?
>
> **A:** Yes. Put both names in the parameter list:
>
> ```koskari
> (define area [width height]
>   (* width height))
>
> (print "area:" (area 8 3))
> ```

---

## Functions can choose

> **Q:** Can a function use `if`?
>
> **A:** Yes:
>
> ```koskari
> (define describe-temperature [temperature]
>   (if (gt temperature 80)
>     "hot"
>     "comfortable"))
>
> (print (describe-temperature 90))
> (print (describe-temperature 72))
> ```

---

## The last form is the result

> **Q:** Where is the `return` word?
>
> **A:** In these small examples, the function result is the value of the last
> form in the body:
>
> ```koskari
> (define greeting [name]
>   (+ "Hello, " name "!"))
> ```
>
> The call `(greeting "Mira")` produces a string. You can print it, store it,
> or pass it to another call.

---

## What you should remember

- A function definition gives a reusable idea a name.
- Parameters are names for the inputs of one call.
- Call your function with the same shape you already know: `(thing input ...)`.
- A small function’s result is usually the value of its last body form.

Next: [Where next](where-next.md).
