# Two levels of Koskari repr: identity and detail

## Status

Proposal -- not implemented. Recorded 2026-11-01 while fixing the module
namespace repr (the Koskari repr fell through to the dict handler and
showed only the scratch dunders).

## Problem

`kos_repr` answers two questions with one string. For a list or a pair
the answer is the program's data and there is only one. For a module
namespace, a frame namespace or a module pragma the string is identity
plus a dump of internal state, and the reader usually wants one or the
other: "which object is this" when it sits inside something else, "what
is in it" when it is the thing being looked at. Today every occurrence
gets the dump, so a namespace inside a list prints its whole binding
table and a namespace that holds a namespace prints a `...` marker
where identity would do.

Python's split of `repr` and `str` is not the model. Using `str` as a
reporting format gives a conversion a second meaning, and `tp_repr`
takes no argument anyway.

## Proposal

`kos_repr` takes an optional `detail` flag, default true:

```koskari
(kos_repr ns)                 ; <ModuleNamespace "foo" #{"k": 3 ...}>
(kos_repr ns detail: False)   ; <ModuleNamespace "foo">
```

The same call from Python, `kos_repr(obj, detail=False)`, is the only
Python-side route to the identity form. Python's `repr` stays the
detail form, since a dict-like object is expected to show its contents
there, and `str` stays whatever `repr` is.

### The hand-off rule

- A detail repr prints identity plus contents, and renders every
  opt-in object it contains at the identity level.
- Plain containers (list, tuple, dict, pair, values) are transparent:
  they pass the level they were given through to their elements. Their
  contents are the program's data, and truncating `#[#[1 2]]` would
  turn output into noise.
- Only types that opt in consume the flag.

So `<ModuleNamespace "foo" #{"other": <ModuleNamespace "bar"> ...}>`,
and a list of two namespaces at the REPL prints both in detail.

`Py_ReprEnter` stays as the cycle guard. The hand-off removes the
common case (a namespace reachable from its own bindings) from the
guard's reach, but a list that holds itself still needs it.

### Handlers

`__kos_repr__` handlers today are `METH_NOARGS`. A handler that
distinguishes the levels declares a parameter, and the registry
dispatch in `KosUtils_Repr` passes the flag only to handlers that take
it; the existing handlers stay as they are. The C entry grows a
sibling, `KosUtils_ReprDetail(obj, int detail)`, with `KosUtils_Repr`
as the detail-true spelling.

### Opt-in types to start

- `ModuleNamespace`: identity `<ModuleNamespace "foo">`; detail as
  committed in 10246d5a.
- `Namespace` (frame): identity `<Namespace f1 in __main__>`; detail
  the current member listing.
- `ModulePragma`: identity `<ModulePragma>` with the module name if it
  carries one; detail the settings table.
- The REPL quitter keeps its message as its only form; it exists to be
  typed bare at the prompt.

### What does not change

- The REPL result display stays at detail.
- `print` of any plain container is unchanged.
- The flag is a boolean. A depth count buys nothing under the hand-off
  rule, since the rule already says what the next level is.

## Open

- A `*repr-detail*` parameter as the source of the default when the
  argument is absent, so `(parameterize [[*repr-detail* False]] ...)`
  reads a whole region at identity level. The argument stays the
  primitive; the parameter would only set its default.
- Whether `Function` and `Code` objects want a detail form (source
  position, formals) or are short enough already.
