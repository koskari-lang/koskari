# Macros

Python shapes programs with decorators, context managers, and
metaclasses. Koskari adds a macro: a function that receives code before
it runs and returns new code. A macro is not needed on day one; it is
how a repeated shape becomes a word.

## A route

:::{pair}
```python
@app.get("/hello/{name}")
async def hello(name: str):
    return {"message": f"hello {name}"}
```
+++
```koskari
define-route "/hello/{name}" hello [name]:
  #{"message": f"hello {name}"}
```
:::

:::{dialogue}
What makes `define-route` exist?
+++
A `define-syntax` form. The rule names the shape to match and the code
to produce:

```koskari
define app (fastapi.FastAPI)

define-syntax define-route:
  syntax-rules []:
    define-route path name args body ...:
      define name args body ...
      (app.add_api_route path (coroutine-fn name) methods: #["GET"])
```

Each use expands into a `define` and a registration call, at compile
time. Nothing about it is special at runtime.
:::

:::{dialogue}
What is `...`?
+++
An ellipsis in a pattern matches zero or more forms, and the same
ellipsis in the template splices them back. `body ...` is the whole
block under the header.
:::

:::{dialogue}
Why is the `define` in the template on one line?
+++
The template is read by the layout reader before the ellipsis is
expanded, and a line holding `body ...` is two elements, so it would
read as the call `(body ...)`. Keeping `body ...` on the header line
makes it part of the `define` form. Where a body has to sit under a
block header, spell it `(begin body ...)`.
:::

## A command

:::{pair}
```python
import click

@click.command()
@click.argument("name")
def greet(name):
    print(f"hello {name}")
```
+++
```koskari
define-command main "name" greet [name]:
  print f"hello {name}"
```
:::

:::{dialogue}
And the macro behind it?
+++
```koskari
import argparse

define-syntax define-command:
  syntax-rules []:
    define-command cli-name option fn-name [value] body ...:
      define fn-name [value] body ...

      define cli-name []:
        define parser (argparse.ArgumentParser)
        (parser.add_argument option)
        define options (parser.parse_args)
        (fn-name (attr options value))
```

Two definitions come out: `greet`, the function you can call from
tests, and `main`, which parses arguments and calls it. Growing it,
with flags, defaults, and help text, is more arms in the same rule.
:::

:::{dialogue}
Why does the use site name `main` as well as `greet`?
+++
Hygiene. A name the template introduces on its own, such as a `define
main` written inside the rule, is marked so it cannot collide with the
caller's names. A name the caller supplies is bound as written. Both
functions are meant to be called, so both names come from the use.
:::

## Function or macro

:::{dialogue}
When is a function enough?
+++
Whenever the inputs are values. A macro is for a new binding shape, for
controlling whether or when something is evaluated, or for boilerplate
that should wrap a body you wrote. `slug` is a function; `unless` is a
macro.
:::

:::{pair}
```python
# no equivalent: Python cannot
# add a statement form
```
+++
```koskari
define-syntax unless:
  syntax-rules []:
    unless test body ...:
      if test:
        None
      else:
        (begin body ...)
```
:::

## Seeing the expansion

:::{dialogue}
How do I see what a macro produced?
+++
```koskari
>>> (macroexpand '(unless ready (go)))
(cond [ready None] [else: (go)])
```

`unless` is a builtin, so that expansion comes from the builtin's
template, a `cond`. The `define-syntax unless` above is a worked example
of writing the rule; the definition in force here is the builtin's.

Keep a parenthesized use beside the layout use while you learn; the
s-expression is the tree the macro sees.
:::

:::{dialogue}
Where must a macro live?
+++
In a form compiled before its first use: an earlier top-level form in
the same file, or a module you import. A `begin` that defines a macro
and uses it in the same block fails, because the macro is registered
when its form finishes compiling.
:::

The matching rules, hygiene, and the procedural `macro-rules` form are
in [Macros and hygiene](../../guide/macros-and-hygiene.md).
