# Modules and program structure

A small Koskari program can be one file. As it grows, split reusable definitions
into modules and import them where needed.

---

## One file

A file is a sequence of forms evaluated from top to bottom:

```koskari
(define message "Hello from a file")
(print message)
```

Save it as `hello.ki` and run it with the `koskari` command.

## Define before use

Put supporting names before the forms that use them:

```koskari
(define square [x]
  (* x x))

(print (square 12))
```

This is the simplest structure for scripts: constants and helpers first, main
actions last.

## Imports

Use `import` to bind a module name:

```koskari
(import os.path as: path)
(print (path.basename "notes/today.txt"))
```

The alias after `as:` is optional, but it can make code easier to read.

Use `import-from` to bind selected names from a module:

```koskari
(import-from os path sep)
(print sep)
```

Relative imports use leading dots in the module name for package code.

## Main scripts

When a file may be imported by other code, keep “run this as a script” behavior
behind `when-main`:

```koskari
(define main []
  (print "running as a script"))

(when-main
  (main))
```

That keeps definitions reusable without causing script output during import.

## Keeping files friendly

A reader should be able to skim a file in this order:

1. Imports.
2. Constants and simple data.
3. Helper functions.
4. Main function or final script forms.

Prefer clear names over clever abbreviations. Add docstrings to functions that
will be reused.

## What you should remember

- Files run top to bottom.
- Keep helper definitions before their uses.
- `import` binds a module; `import-from` binds names from a module.
- `when-main` separates reusable definitions from script behavior.

Next: [Macros or syntax extension](07-macros-or-syntax-extension.md).
