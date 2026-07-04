# Types and contracts

Koskari runs on Python values. Type annotations are available as useful metadata
for tools and readers, but they are not the first thing to learn.

---

## Annotating definitions

Definitions and formals can carry annotations:

```koskari
(define total :: int 0)

define double :: int [n :: int]:
  (* n 2)
```

Use annotations to document intent at module boundaries, especially when Python
code will call Koskari code or tools will inspect it.

---

## Runtime reality

Koskari interoperates with Python, so values are Python objects at runtime.
Function and class annotations evaluate in the defining scope and land on
`__annotations__` as real objects — `[n :: int]` gives the function
`{'n': int}`, with `inspect.signature` reporting the same — and a string
annotation like `:: "Request"` stays a lazily resolved forward reference.
Annotations are still metadata, though: nothing checks argument values
against them at call time.

That means examples should still validate their assumptions with clear code:

```koskari
if (< count 0):
  (raise (ValueError "count must be non-negative"))
```

---

## Contracts by convention

A friendly public function states expectations in three places:

1. Its name.
2. Its parameter names and optional annotations.
3. Early checks with helpful errors when bad input would be confusing later.

---

## What you should remember

Use annotations as reader and tooling help, not as a substitute for simple
runtime checks. Keep beginner examples focused on values and behavior first.
