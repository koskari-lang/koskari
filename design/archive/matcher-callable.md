# Matcher as a function

Status: stages 1-3 implemented; stage 4 in progress.

## Before

`koskari.matcher.Matcher` was a Python class. `Matcher.match(subject)`
ran `check_subject` in Python, compiled the frozen tuple to a `Function`
on the first call, called it through a `demand` wrapper in a nested
evaluator, and wrapped the result in `MatchResult`. From Koskari,
`(m.match s)` was a Python method call: no flattening, and an async
predicate could not suspend the caller. A pattern error surfaced at the
first match, not where the matcher was built.

A `Syntax` held a Matcher per arm and a `Dispatcher` that lowered every
arm's frozen tuple again into one function; the arm's own Matcher was
never called in production.

The frozen tuple existed to reproduce the old engine: a portable,
validated, thawable form of a pattern.

## After

The matcher is the `Function` the lowering builds. There is no Matcher
type.

- One positional formal, a gensym, so no capture or extern can name it.
- The captures (a dict or a `DatumFrame`) on a match, None on a miss.
- A KosCallable: from Koskari, `(m s)` installs in the running
  evaluator, flattened, and an awaitable from a predicate or extractor
  is awaited in place. Python callers wrap it with `demand` once.
- No subject check outside the walk: `KosMatch_Descend` reads through
  `Datum`, refuses a looping spine, and an improper spine is a miss, as
  in `compile/match`.

`Pattern` is a compile-time helper: it describes a pattern and lowers
it. Nothing holds a Pattern at runtime to match with.

The frozen tuple is an in-memory handoff from `TableBuilder` to
`MatchLowering` only. It has no public form.

## Stages

1. Implemented: the subject formal is a gensym on every target (the
   runtime function and the function a module compiles for
   `define-syntax`), minted after the extern atoms.
2. Implemented, then superseded by stage 4: a module-compiled arm
   carried its frozen tuple.
3. Implemented: a `Syntax` passes the subject and its arms' matcher
   functions, in order, to one shared first-match function assembled
   from bytecode at import; it returns the index and captures of the
   first that is not None, or -1 and None. The `Dispatcher` and
   `compile_dispatch_function` are removed.
4. The Matcher is removed and the Function is the matcher.
   - `Pattern.build(externs)` refuses a missing extern, then compiles at
     build over a `ModuleNamespace` whose builtins are the externs dict,
     and returns the Function. `make_matcher` returns it.
   - `Pattern.build` refuses a LITERAL whose value is not a pool
     constant (a list, dict, set, pair, Datum or user object): TypeError
     at build.
   - Removed: the Python `Matcher` class, `Matcher.match`, `from_tuple`,
     `from_function`, `TableBuilder.compile`, `missing_externs`,
     `interned_consts`, `check_subject`, and the frozen tuple's public
     surface: `thaw`, `validate_frozen`, the `"MATCHER"` tag.
     `TableBuilder` hands its tables to the lowering directly.
   - The checks of `validate_frozen` that refuse a pattern a user can
     write stay, as `check_nodes(nodes, entry)`, called where
     `validate_frozen` was: `Pattern.build`, `compile/match` and the
     module path. Errors raised by `TableBuilder.lower` itself keep
     their own path.
   - A module-compiled arm passes its `FunctionSpec` (`CONST`,
     `BUILD_FUNCTION`) and its repeat variables; no frozen tuple.
   - A `Syntax` arm holds its function, plus its Pattern or quoted
     source for help text and repeat variables.
   - Python callers (`Pattern.match`, the parser matchers, `try_expand`)
     call through `demand` and test against None; `Pattern.match` keeps
     returning `MatchResult`.
5. User docs and corpus drop the Matcher API.

## Out of scope

Patterns emitting instructions directly, as Specials do, with no node
tables between `TableBuilder` and `MatchLowering`.
