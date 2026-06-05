# Testing and quality

Testing is the habit of turning expectations into checks you can run again.
Even when a project has no formal test harness yet, you can write examples that
make behavior clear.

---

## Start with examples

For a tiny function, write the call you expect to work:

```koskari
(define square [x]
  (* x x))

(print (square 5))
```

If the output should be `25`, say so in nearby prose or in a recipe page. Clear
examples become future tests.

## Keep functions small

Small functions are easier to check:

```koskari
(define fahrenheit-to-celsius [f]
  (/ (* (- f 32) 5) 9))
```

A function that returns a value is usually easier to test than one that prints,
imports, mutates, and calculates all at once.

## Use the REPL deliberately

The REPL is a testing tool while you learn:

1. Define one function.
2. Try two or three inputs.
3. Try one edge case.
4. Move the working code into a file.

## Project checks

For this repository, use the project build and test page when working on
Koskari itself. For your own Koskari scripts, create a small command or file
that runs important examples from top to bottom.

## Documentation quality

Good user-facing examples should be:

- short enough to copy,
- complete enough to run,
- named with everyday words,
- followed by what the reader should expect.

## What you should remember

Testing starts before a framework: make a prediction, run the code, and keep the
example. As programs grow, preserve those examples as repeatable checks.

Next optional guide: [Bytecode mental model](bytecode-mental-model.md).
