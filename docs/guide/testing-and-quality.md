# Testing and quality

Good Koskari code is easy to run, easy to change, and easy to check.

---

## Start with tiny examples

Before building a large script, write a tiny file that proves the core idea:

```koskari
define add-tax [price]:
  (+ price (* price 0.08))

(print (add-tax 100))
```

Run it, change one input, and run it again. This is the same edit → run → read
loop from Little Koskari.

---

## Test through Python when useful

Koskari is a Python-hosted language, so project tests often use Python test
runners around Koskari modules, CLI scripts, or compiler behavior. Keep examples
small enough that a failing test points at one idea.

---

## Check user-facing scripts

For scripts, test at least three paths:

1. A normal successful run.
2. A small edge case (empty input, zero count, missing optional value).
3. A friendly failure path with an understandable message.

---

## Documentation examples are tests for readers

Every example should have a job: demonstrate one form, one pattern, or one
pitfall. Prefer names like `score`, `path`, and `total` over placeholder names
once an example has a story.

---

## What you should remember

Quality starts with runnable examples. Keep functions small, name the intent,
and test the paths a new reader is most likely to try.

---

**Go deeper:** [Build and test](../project/build-test.md) — running the suite,
the tox environments, and how CI interprets them.
