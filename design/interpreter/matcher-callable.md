# Matcher as a KosCallable

Status: design.

## Today

`koskari.matcher.Matcher` is a Python class. `Matcher.match(subject)`:

1. runs `check_subject` in Python: reads through `Datum`, refuses a
   looping spine (ValueError) and an improper one (TypeError);
2. on the first call, compiles the frozen tuple to a `Function` with
   `compile_match_function` and wraps it with `demand`;
3. calls the demand wrapper, which runs the function in a nested
   evaluator;
4. wraps the result in `MatchResult(matched, captures)`.

From Koskari, `(m.match s)` is a Python method call. The walk does not
flatten into the caller, and an async predicate or extractor cannot
suspend the caller.

The compile is deferred to the first match, so a pattern error surfaces
where the matcher is first used, not where it is built.

`check_subject` repeats the walk's entry. `KosMatch_Descend` reads
through `Datum`, counts the spine once and refuses a loop with the same
ValueError, and leaves an improper tail to miss. The only difference is
the improper spine: TypeError from `check_subject`, a miss from the walk
and from `compile/match`.

The subject formal is the fixed name `match-subject`. `FunctionTarget.build`
resolves every name by its first occurrence, so a capture or extern named
`match-subject` resolves to the subject's local (bug).

## Change

### The type

`Matcher` is a native `KosEvaluatedType` in `koskari/lib/matcher.c`. Not
subclassable. Fields, each a read-only member:

| field | contents |
|---|---|
| `function` | the compiled `Function` |
| `frozen` | the frozen 6-tuple; a module-compiled matcher carries the tuple its function was lowered from |
| `slots` | the slot names, in slot order |
| `name` | the matcher's name, used by repr |

`externs` and `consts` are getters reading `frozen[2]` and `frozen[3]`.

`tp_new` takes the fields as given. It does not compile.

### Calls

One positional argument, `subject`, the name every Matcher gives its
one-arity call. The Matcher passes it to the function positionally,
untouched.

- `kc_vinstall`: refuse any other arity or a keyword with a TypeError
  naming `subject`, then
  `KosEvalState_Call(es, function, &subject, 1, NULL, tail_call, ...)`.
  From Koskari, `(m s)` installs the frame in the running evaluator:
  flattened, and an awaitable from a predicate or extractor is awaited in
  place.
- `kc_pyvectorcall`: the same arity check, then call `function`.

The arity check is the type's own: the function's formal is a gensym,
and its own arity error would name it.

The result is the function's: the captures (a dict or a `DatumFrame`) on
a match, None on a miss. `Matcher.match` and `check_subject` are removed.
An improper spine is a miss, as in `compile/match`.

### Formal name

`compile_match_function` names the formal with a gensym. No capture or
extern can name it. `compile_dispatch_function` builds through the same
`FunctionTarget` and takes the same gensym.

### Construction

Compile at build. One Python function makes a Matcher from a frozen
tuple:

```python
def thaw(frozen, externs=None, name=None):
    validate_frozen(frozen)
    externs = dict(externs or {})
    # refuse the first extern the frozen tuple declares and externs lacks
    namespace = ModuleNamespace(name or "matcher", externs)
    function = compile_match_function(frozen, namespace, name=...)
    return Matcher(function, frozen, tuple(frozen[1]), name)
```

The externs dict is the builtins of the Matcher's own `ModuleNamespace`;
the closure names resolve once, when the function is built. The
`Function` holds the namespace; the `Matcher` does not.

| today | after |
|---|---|
| `Matcher(frozen, name, externs)` | `thaw(frozen, externs, name)` |
| `Matcher.from_tuple(frozen, externs)` | removed; `thaw` |
| `Matcher.from_function(fn, frozen, name)` | `Matcher(fn, frozen, tuple(frozen[1]), name)`, the module's frozen tuple |
| `MatchCompiler.compile(pattern, externs)` | removed; `pattern.build(externs)` |
| `Pattern.build(externs)` | `thaw(MatchCompiler().lower(self), externs)` |
| `make_matcher(pattern, externs)` | unchanged |
| `Pattern.match(subject, **externs)` | unchanged: `build`, call, `MatchResult` |

`Matcher.missing_externs` is removed; the refusal is the one check in
`thaw`. `interned_consts` exists only to fill `Matcher.consts`; it is
removed and `consts` reads the frozen pool.

## Unchanged

- `Dispatcher` stays a Python compile helper: `select` runs only when the
  compiler expands a syntax.
- `compile/match` arms lower inline and take no Matcher.

## Callers

`tools/matchbench.ki` and the matcher tests call the Matcher directly.
Tests change for `from_tuple`, `MatchCompiler.compile`, `missing_externs`,
the depth refusal at match time, the `FIELD_KW` names in `consts`, and
the TypeError on an improper spine.

## Open

- A `Syntax` arm holds a Matcher, but no production code calls it.
  `Dispatcher` lowers an arm with no externs inline from its `frozen`,
  and calls the `function` of an arm with externs.
  `SyntaxTransform.try_expand` is its only caller, and only tests call
  `try_expand`. Under compile at
  build, each frozen arm compiles a function that only those tests run.
  The arm could hold the frozen tuple or the module function, and
  `Dispatcher` take those directly.
- The module-compiled path (`compile/specials/syntax.py`) also names its
  formal `match-subject`, and that function is serialized into the kio,
  which holds plain names only. A gensym cannot go there.
