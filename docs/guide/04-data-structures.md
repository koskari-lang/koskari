# Data structures

Data structures let one value hold several pieces. Koskari works well with
Python values such as strings, numbers, tuples, lists, and dictionaries, and it
also has Lisp-style pairs.

---

## Tuples, lists, and dictionaries

You can call the usual constructors and methods that are available in the
runtime namespace:

```koskari
(define names (list "Ada" "Grace" "Katherine"))
(define point (dict #("x" 10) #("y" 20)))
```

Use simple Python-like data when that is what your program needs: a list for an
ordered collection, a dict for keyed lookup, and a tuple when a fixed group of
values belongs together.

## Reading items

Use item access to read from indexed or keyed values:

```koskari
(define letters (list "a" "b" "c"))
(item letters 0)

(define settings (dict #("theme" "light")))
(item settings "theme")
```

The exact behavior comes from the value being indexed, just as Python objects
define their own item access.

## Pairs and lists

Koskari also has pair operations:

```koskari
(cons 1 (cons 2 None))
(car (cons "first" "rest"))
(cdr (cons "first" "rest"))
```

`cons` builds a pair; `car` reads the first part; `cdr` reads the rest. Pairs
matter most when you work with Lisp-shaped data, macro inputs, and quoted forms.
For everyday scripting, Python lists and dictionaries are often friendlier.

## Traversal

Use `for-each` when you want to run a body once per item:

```koskari
(define names (list "Ada" "Grace" "Katherine"))
(for-each [name names]
  (print "hello" name))
```

Use `list-each` when you want to build a new list from an existing sequence:

```koskari
(list-each [name names]
  (+ "Hello, " name))
```

## Choosing a shape

- Use a **list** when order matters and the number of elements can change.
- Use a **dict** when you want to look values up by key.
- Use a **tuple** for small fixed records.
- Use **pairs** when you are manipulating Lisp forms or writing macros.

## What you should remember

Data structures are ordinary values. You can bind them to names, pass them to
functions, traverse them, and return them from functions.

Next: [Control flow](05-control-flow.md).
