# Forms and combining

A **form** is one complete piece of Koskari syntax. It can be as small as a
number or as large as a multi-line function definition. This chapter shows how
forms combine into bigger forms.

---

## Atomic forms

Some forms evaluate to themselves:

```koskari
42
"hello"
True
False
None
```

These are useful because you can pass them directly to calls.

## Names

A name form looks up a binding:

```koskari
(define answer 42)
answer
```

After the `define`, the name `answer` evaluates to the value `42`. If a name has
not been defined in the current scope, evaluation raises an error instead of
inventing a value.

## Calls

The most common combined form is a call:

```koskari
(+ 1 2)
(print "total:" (+ 1 2))
```

Koskari evaluates the inputs, then applies the thing in first position. Nested
forms finish from the inside out, so `(+ 1 2)` produces `3` before `print` runs.

## Special forms

Some first-position names have special evaluation rules. For example, `define`
does not evaluate the name being introduced:

```koskari
(define count 0)
```

Here `count` is the new binding name, not a value to look up. `if`, `cond`,
`lambda`, `let`, `while`, imports, and several macro forms also have their own
rules. Use the reference when you need exact syntax for one of them.

## Combining with `begin`

Use `begin` when several forms should behave like one form:

```koskari
(begin
  (print "setting up")
  (+ 20 22))
```

The forms run in order. The value of the whole `begin` is the value of the last
form. That makes `begin` useful inside branches, functions, and macros when you
need a sequence but the surrounding position expects one expression.

## Brackets and layout

Koskari supports s-expression syntax and layout syntax. The reference examples
show both where a form has a layout equivalent. While learning, prefer the
explicit parenthesized shape first; it makes the tree structure visible.

## Try it

Predict each result, then check it at the REPL:

```koskari
(+ 1 (* 2 3))
(begin (define x 5) (* x x))
(if (gt 10 3) "yes" "no")
```

## What you should remember

- A form is one complete syntactic unit.
- Atomic forms such as numbers and strings are already values.
- Name forms look up bindings.
- Calls combine forms by evaluating inputs and applying the first position.
- Special forms deliberately break the ordinary call rule when the language
  needs different behavior.

Next: [Bindings and environments](02-bindings-and-environments.md).
