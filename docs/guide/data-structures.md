# Data structures

Data structures collect values so a program can pass them around as one value.
Koskari works comfortably with Python values and also has Lisp-style pairs and
lists.

---

## Lists from Python and Koskari calls

When you import or receive a Python list, use ordinary calls and loops to work
with it:

```koskari
(define numbers (list (range 5)))
(for-each [n numbers]
  (print n))
```

Because Koskari runs on Python, Python containers are everyday values.

---

## Pairs and Lisp lists

Koskari also has pair operations:

```koskari
(define pair_val (cons "first" "rest"))
(car pair_val)    ;; first part
(cdr pair_val)    ;; rest part
```

Use pair operations when you are writing macros, manipulating Koskari forms, or
following Lisp-style examples.

---

## Quoting data

Normally a list-shaped form is a call. Quote it when you want the structure as
data instead of running it:

```koskari
(quote (red green blue))
```

Which can be written in shorter form using the reader macro. The single quote
character tells the reader that the form immediately following it should be
wrapped in the `quote` invocation.

```koskari
'(red green blue)
```

Quasiquote is the template version: most of the structure is data, but selected
parts can be computed with unquote. Here the special character is the single
backtick. The comma then performs un-quoting for one sub-form.

```koskari
(define name "Ada")
`(hello ,name)
```

---

## Dictionaries and objects

Use Python dictionaries, objects, attributes, and item access when that is the
natural shape of the data:

```koskari
(define user #{"name": "Ada" "score": 10})
(print user["name"])
```

Keys are expressions, so a string key is quoted. A `name:` keyword key
looks up the variable `name` instead. For computed keys there is a pair
form of the dictionary literal

```koskari
(define user #{("name" . (get-user-name)) ("score" . (get-score))})
```

For public examples, choose the simplest structure that makes the program's
purpose obvious.

---

## What you should remember

Use Python containers for ordinary application data. Use pairs and quoted forms
when you need Lisp-shaped data or code-as-data. Quote prevents evaluation.
