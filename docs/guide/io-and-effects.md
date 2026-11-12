# I/O and effects

An effect is what a form does besides producing a value: printing,
reading a file, storing into a name, raising an error. Koskari's
effects are Python's, reached through the same objects.

## Printing

`print` is Python's `print`:

```koskari
print "hello" "Koskari"
print "no newline" end: ""
print "to stderr" file: sys.stderr
```

Keyword arguments pass as `name: value`. At the REPL, a form's value is
shown without `print`; in a file, only `print` writes to the terminal.

## Files and context managers

`with` runs a body inside a context manager and binds its enter value:

```koskari
with [f (open path)]:
  print (f.read)
```

The bracket holds the name and the expression. With no name to bind,
write `with [(lock)]:`. Cleanup runs when the body completes, raises, or
is left by `return`, `break` or `continue`. An async context manager
works the same way. A continuation transfer out of or back into the body
runs neither exit nor enter; `dyn-with` is the form for a manager that a
transfer exits and enters again.

## Exceptions

Errors are Python exceptions. `raise` raises one, and bare `raise`
re-raises the current one:

```koskari
raise (ValueError "count must be non-negative")
raise
```

`try` handles them. `except` arms name a class and optionally bind the
instance; `else:` runs when nothing was raised; `finally:` always runs:

```koskari
try:
  (load-config path)
except [FileNotFoundError as: e]:
  print "missing config" path
else:
  print "loaded"
finally:
  print "finished config attempt"
```

The arm `[e FileNotFoundError]` is the same binding in the older order,
and a bare `except:` catches everything.

`try` is an expression. Its value is the body's value, or the handling
arm's when one ran:

```koskari
var port:
  try:
    (int text)
  except [ValueError]:
    8080
```

The parenthesized spelling puts the arms after the body:

```koskari
(try (int text)
  [[e ValueError] 8080]
  [finally: (print "done")])
```

Raise for a case the caller cannot be expected to handle by checking
first. Catch close to where recovery is possible. A missing name, a
failing import, and a bad argument are all ordinary Python exceptions
with the classes Python gives them; the Koskari-specific classes, for
the read and compile stages and for the evaluator, are in the
[Error guide](../reference/error-codes.md).

## Stores

`set!`, `incr!`, and friends are effects on bindings, and a store into
an attribute or a subscript is an effect on an object:

```koskari
set! obj.count 0
incr! counts[word]
```

[Bindings and refs](bindings-and-refs.md) covers what a store does.

## Async effects

A call that returns an awaitable is awaited where it is called, so I/O
through an async library reads the same as through a sync one:

```koskari
def body [url]:
  (client.get url)     ;; awaited here if it suspends
```

The explicit `await` is for a value you chose to hold: a `defer`, a
Task, a Future. The rules, and the demand path where suspension is not
allowed, are in [Demand, async, and defer](demand-async-and-defer.md).
