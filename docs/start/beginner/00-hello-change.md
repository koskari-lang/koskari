# Hello and change

This chapter introduces the **edit → run → read** loop.

---

## Starting Koskari

> **Q:** How do we talk to Koskari?
>
> **A:** Start the REPL (read-eval-print loop):
>
> ```bash
> koskari
> ```
>
> Koskari waits for you to type a line and press Enter. When it is ready, it
> shows a prompt. In this track we write that prompt as `koskari>`.

---

## A first program

> **Q:** Can we make Koskari say hello?
>
> **A:** Yes. Type this at the prompt:
>
> ```koskari
> koskari> (print "Hello, Koskari!")
> ```
>
> Koskari sends the text between the quotes to your screen:
>
> ```text
> Hello, Koskari!
> ```

> **Q:** What are the parentheses for?
>
> **A:** Not yet. For now, treat `(print ...)` as one complete phrase: “print
> this text.” Chapter 2 explains calling in general. Chapter 1 explains the
> quotes.

> **Q:** Can we run the same thing from a file?
>
> **A:** Yes. Put the line in a file named `hello.ki`:
>
> ```koskari
> (print "Hello, Koskari!")
> ```
>
> Run:
>
> ```bash
> koskari hello.ki
> ```
>
> The same text appears. A file is a program you can run again without
> retyping.

---

## Change one thing

> **Q:** What happens if we change the text inside the quotes?
>
> **A:** Try it. Edit the string, save the file if you are using one, and run
> again:
>
> ```koskari
> (print "Hello, world!")
> ```
>
> The output changes because **you changed the program**. The program is the
> text Koskari reads; the output is what happens when Koskari runs it.

> **Q:** Is that the whole job of programming?
>
> **A:** Almost. Programming is:
>
> 1. Write or change a program (text Koskari understands).
> 2. Run it (`koskari` or `koskari myfile.ki`).
> 3. Read the result on the screen (or read an error message — chapter 3).
> 4. Go back to step 1.

---

## What you should remember

One program is a piece of text. Change the text, run again, observe what changed.
You do not need to memorize every symbol yet.

Next: [Words and punctuation](01-words-and-punctuation.md).
