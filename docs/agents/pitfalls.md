# Koskari pitfalls for agents with Python or Scheme priors

Common mistakes and gotchas: Koskari code that parses fine but means
the wrong thing, usually because a Python or Scheme habit was carried
over. Each pitfall states the wrong assumption, the rule, and the
correct form. For rules about source that fails to *parse* as
intended, see [Writing Koskari source files](writing-ki-files.md).

---

## Coming from Python

**`isinstance` is not the name.** Type tests are `instance?` and
`subclass?`:

```koskari
(instance? x str)       ; not (isinstance x str)
```

**Dashes are not folded to underscores on Python attributes.** Python
names are used verbatim: `(asyncio.get_running_loop)`, not
`get-running-loop`. Koskari-defined names keep their dashes -- except
a name a Python caller must import or access by exact identifier
(`from mod import name`, a module attribute read from Python). Python
identifiers cannot contain `-`; any Koskari name crossing that
boundary by name has to be underscore-cased, dashes reserved for names
that stay Koskari-internal.

**Any direct call from Python needs `demand`, not only callback
arguments.** Koskari runs ambiently async. This applies to every call
made from plain Python code, not only a lambda handed to a Python
higher-order function -- calling a plain `define`d top-level function
directly from Python (`mod.some_fn(x)`, no `await`, no event loop)
returns an un-awaited `koskari.lib.Coroutine`, not the value, exactly
like the callback case:

```koskari
(list (map (demand {(* %1 %1)}) numbers))   ; callback argument
```

```koskari
(define-demanding some_fn [x] (+ x 1))      ; direct-call entry point
; equivalent: (define some_fn (demand (function some_fn [x] (+ x 1))))
; equivalent: @demand \n define some_fn [x]: (+ x 1)
```

`define-demanding NAME FORMALS BODY...` and `@demand` above a plain
`define` are equivalent; both wrap the function so a synchronous
Python caller with no event loop gets the resolved value. Any
top-level function meant to be imported and called directly by Python
(a module boundary a Python package will call into, a test harness)
needs one of these forms. A function called only from other Koskari
code does not -- ambient async resolves those calls without help, and
wrapping them anyway is harmless but unnecessary.

`sorted` is a known exception: Koskari's `sorted` demand-wraps a
callable `key:` itself, so no wrap is needed there.

**Call methods on the binding.** When a dict (or any object) is bound
to a name, call its method directly rather than routing through the
type:

```koskari
(config.get "port" 8080)      ; not (dict.get config "port" 8080)
```

**Dict-literal keys are expressions.** A keyword-looking key lowers to
a *name*, not a string: `#{ok: True}` evaluates the name `ok` as the
key and raises `NameError` unless a binding `ok` exists. Quote string
keys:

```koskari
#{"ok": True}
```

## Coming from Scheme or another LISP

**Write Koskari forms, not Scheme stand-ins.** There is no `eq` /
`eqv?`; equality is `==` (also spelled `equal?`). Rest parameters are
`*:` / `**:`, not a dotted tail. `for-each` takes a `[var seq]`
binding, not a procedure argument.

**Brackets are not vectors.** `[ ]` and `( )` read identically and
compare equal; `[ ]` is a readability convention, covered in
[Brackets and the call convention](../reference/concepts/brackets-and-call-convention.md).

**Syntax works in operator position.** The head of a call may itself
be a form headed by a macro; the compiler expands the head and
re-dispatches the whole call, so `((make-op x) a b)` is legal when
`make-op` expands to an operator. A head that is no syntax -- a plain
function call, an attr form -- is left untouched and compiles as an
ordinary call of its value, so method calls like `("-".join items)`
keep their fused spelling. A syntax that has *only* a bare form acts
as an alias in operator position: the head expands and the arguments
ride along. Details in
[Name resolution](../reference/concepts/name-resolution-and-prefixes.md).

**The idiomatic form is the direct one.** Koskari prefers `(when x
...)` over `(if x ... None)` and direct forms over compositions of
primitives; see [Idiomatic Koskari](../reference/concepts/idiomatic-koskari.md).

---

Every pitfall family here is also covered by verified wrong-usage
records: call `search_examples` with `polarity: "invalid"` and the
form's name to get the documented mistake, why it fails, and the
corrected source. See [Querying the corpus](querying-the-corpus.md).
