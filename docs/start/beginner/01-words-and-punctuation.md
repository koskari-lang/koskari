# Words and punctuation

Programs are made of **words** and **punctuation**. This chapter names the
pieces you will see in almost every Koskari program.

---

## Numbers

> **Q:** Is `42` a Koskari program?
>
> **A:** It is a **number** — a kind of value. At the REPL you can type it alone:
>
> ```koskari
> koskari> 42
> ```
>
> Koskari shows the number back to you. (Chapter 3 explains when results appear
> on screen.)

> **Q:** Are `0` and `-3` numbers too?
>
> **A:** Yes.

---

## Text (strings)

> **Q:** How do we write text in a program?
>
> **A:** Put it in **double quotes**:
>
> ```koskari
> "Hello, Koskari!"
> ```
>
> The quotes are punctuation. They are not part of the text that is stored —
> they mark where the text starts and stops.

> **Q:** Can we use single quotes for strings?
>
> **A:** Not for this kind of text. Use `"` as shown.

---

## Names

> **Q:** What is `x`?
>
> **A:** By itself, `x` is a **name**. A name is a word you choose. Names hold
> values once you **define** them in chapter 2.

> **Q:** Is `print` a name?
>
> **A:** Yes. It is a name that already means something in Koskari: a thing you
> can call to send text to the screen.

> **Q:** Can names contain digits?
>
> **A:** Often, yes — for example `x2`. They cannot start with a digit. For now,
> use simple names like `count`, `message`, and `total`.

---

## Parentheses

> **Q:** What does `(` do?
>
> **A:** It starts a **group**. The matching `)` ends the group. Everything
> between them belongs together:
>
> ```koskari
> (+ 1 2)
> ```
>
> This group is one **call** (chapter 2). The parentheses are not optional
> decoration — they tell Koskari where the group begins and ends.

> **Q:** Can groups nest?
>
> **A:** Yes:
>
> ```koskari
> (+ 1 (+ 2 3))
> ```
>
> Inner groups can sit inside outer groups. Chapter 4 practices this pattern.

---

## Spacing

> **Q:** Does `(+ 1 2)` mean something different from `(+1 2)`?
>
> **A:** Koskari allows flexible spacing inside a group. These are the same
> idea:
>
> ```koskari
> (+ 1 2)
> (+  1   2)
> ```
>
> Good style uses spaces so human readers can see the pieces. Koskari is strict
> about parentheses and quotes, not about how many spaces you use.

---

## One rule to keep

> **Q:** What is the one punctuation rule not to break?
>
> **A:** Every `(` must have a matching `)`. If you leave one out, Koskari
> cannot read the program and will report a **syntax error** (chapter 3).

---

## What you should remember

- Numbers like `42` are values.
- `"text"` is text in double quotes.
- Names like `x` and `print` are words you use to refer to things.
- `(...)` groups pieces together; groups can nest.

Next: [Calling things](02-calling-things.md).
