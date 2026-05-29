# Results and messages

Running programs produces **output** and sometimes **errors**. Reading both is
part of programming — not a separate skill for “later.”

---

## Normal results at the REPL

> **Q:** When does Koskari show a result on screen?
>
> **A:** At the REPL, when a line **finishes with a useful value**, Koskari
> prints that value so you can see it:
>
> ```scheme
> koskari> (+ 1 2)
> 3
> ```

> **Q:** What about `(define x 99)`?
>
> **A:** Try it. Defining `x` does its job quietly — you should **not** see `99`
> printed just because you defined the name. The form’s result is not interesting
> to show.

> **Q:** What about `(print "hi")`?
>
> **A:** `print` sends `"hi"` to the screen as its main effect. Focus on the
> text that appears. (Side effects and “return values” get a fuller story in
> the [guide](../../guide/00-model-of-evaluation.md).)

---

## The last result

> **Q:** Can we ask for the previous result again?
>
> **A:** Yes. Type an underscore:
>
> ```scheme
> koskari> (+ 3 4)
> 7
> koskari> _
> 7
> ```
>
> `_` means “the value from the last line I ran.”

---

## Syntax errors

> **Q:** What if the punctuation is wrong?
>
> **A:** Koskari cannot read the program. Try:
>
> ```scheme
> koskari> ( 1 2 ]
> ```
>
> Koskari reports a **syntax error**. The message usually mentions mismatched
> brackets or an unexpected token.

> **Q:** Where does the message appear?
>
> **A:** At the REPL, syntax errors go to the **error output** (often the same
> terminal, but separate from normal printed results). Read the **first useful
> line** — it often says what kind of problem Koskari saw.

> **Q:** What should we do?
>
> **A:**
>
> 1. Read the first line of the message.
> 2. Look back at what you typed — count `(` and `)`.
> 3. Fix one thing, run again.

---

## Runtime errors

> **Q:** Can punctuation be correct and the program still fail?
>
> **A:** Yes. The program can be **readable** but ask for something impossible.
> Example:
>
> ```scheme
> koskari> (/ 1 0)
> ```
>
> Koskari read the program fine, but running it hits a **runtime error**.

> **Q:** How is that different from a syntax error?
>
> **A:**
>
> - **Syntax error** — Koskari could not understand the text.
> - **Runtime error** — Koskari understood the text, but something went wrong
>   while doing it.

---

## Continuing a line

> **Q:** What if I press Enter too early?
>
> **A:** Koskari may wait for the rest of the group. This is fine:
>
> ```scheme
> koskari> (+ 1
> ...> 2)
> 3
> ```
>
> The prompt may change until the `)` closes the group. Finish the parentheses,
> then Koskari runs the whole call.

---

## What you should remember

- Useful values show at the REPL; quiet forms like `define` often do not.
- `_` recalls the last result.
- Syntax errors mean “fix the text.” Runtime errors mean “fix what the program
  tries to do.”
- Read the first line of an error message before guessing.

Next: [Repeating patterns](04-repeating-patterns.md).
