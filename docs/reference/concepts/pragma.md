# Pragmas and feature settings

A **pragma** is a set of module-scoped switches that tune how the reader and
compiler treat your source. Koskari's optional features — bracket-call
checking, name-resolution mode, layout reading, and so on — are all controlled
through the pragma. A pragma affects only the code that comes *after* it in the
same module, and never leaks into modules you import.

## Setting a pragma

Use the `pragma` form. Each setting is a bracketed list: a slash-namespaced key
followed by its value(s). A bare `[key]` uses the setting's default value.

```koskari
(pragma [compiler/call-brackets 'error]
        [compiler/resolution 'scheme]
        [reader/brackets False])
```

- Values are ordinary expressions, so a **literal symbol must be quoted** —
  `'error`, `'scheme`, `'warn`. Booleans (`True` / `False`), numbers, and
  strings are self-carrying and need no quote, and a computed value (a variable
  or call) works too.
- The change is **forward-only**: it applies to everything read and compiled
  after the form, not retroactively, and not to modules you import.
- You may set a pragma more than once; a later setting replaces the earlier one.

## Reading a pragma

The same `pragma` form reads the current state:

```koskari
(pragma compiler/call-brackets)   ; the current value of one setting
(pragma)                          ; the whole pragma object
```

And `help` describes it:

```koskari
(help pragma)                        ; current values + every setting's default
(help (pragma compiler/call-brackets))   ; one setting: its signature and doc
```

`(help pragma)` lists the current values alongside every registered setting and
its default; `(help (pragma KEY))` shows the accepted arguments and
documentation for a single setting.

## What each setting means

Settings are grouped by what they affect: the reader (`reader/*`), the compiler
(`compiler/*`), and the pragma machinery itself (`pragma/*`).

### Reader

- **`reader/brackets`** *(default on)* — whether `[ ]` reads into a distinct
  bracket-pair value that remembers its origin, rather than a plain pair. Off
  makes `[ ]` and `( )` completely indistinguishable. See
  [Brackets and the call convention](brackets-and-call-convention.md).
- **`reader/layout`** *(default on)* — whether indentation-based layout blocks
  are read. Off leaves only s-expression syntax.
- **`reader/implicit-calls`** *(default on)* — whether several values on one
  layout line form an implicit call. Off makes more than one value on a line an
  error.
- **`reader/pass-none`** *(default off)* — whether the bare symbol `pass` reads
  as the `None` literal. Off — the baseline — `pass` is an ordinary symbol with
  no special meaning. On, it is quietly rewritten to `None`, a concession for
  arrivals from Python who cannot yet stop typing `pass` as a branch's
  do-nothing; a bare `[reader/pass-none]` turns it on. The habit it accommodates
  is one to grow out of, not into — write `None`.

### Compiler

- **`compiler/resolution`** *(default `koskari`)* — how writes to names resolve.
  The value is required, one of:
  - `koskari` — strict: a write to an undeclared name is a compile error.
  - `scheme` — an unsatisfied write promotes to a module global.
  - `python` — a first write creates an implicit local unless declared global.
- **`compiler/call-brackets`** *(default `ignore`)* — what happens when a
  bracket-pair `[ ]` reaches call position, where `( )` is conventionally
  expected. One of:
  - `ignore` — nothing; the convention is unenforced.
  - `warn` — emit a `SyntaxWarning`.
  - `error` — raise a compile error.

  A bare `[compiler/call-brackets]` selects `warn`. See
  [Brackets and the call convention](brackets-and-call-convention.md).
- **`compiler/tco-enable`** *(default on)* — whether tail calls are optimized.
- **`compiler/convert-fast`** *(default on)* — whether the compiler lowers local
  variable access to its fast form. Off disables that optimization.
- **`compiler/compress`** *(default off)* — reserved for a future bytecode
  compression pass; currently inert.
- **`compiler/protect-code`** *(default off)* — whether each compiled code
  object's bytecode storage is sealed read-only at the OS level the moment it
  is built. On, a stray or hostile write to live bytecode faults instead of
  corrupting the interpreter, at the cost of a full memory page per code
  object. A bare `[compiler/protect-code]` turns it on.
- **`compiler/pack-module`** *(default on)* — whether the module's surviving
  code objects are compacted into shared read-only storage when the module
  finishes loading. On — the baseline — a loaded module's functions share one
  sealed allocation instead of each holding its own page. Options tune the
  packing: `max-count:` caps how many code objects share one allocation, and
  `max-bytes:` is a soft size cap; `0` (the default for both) means unbounded.

  ```
  (pragma [compiler/pack-module True max-count: 32])
  (pragma [compiler/pack-module False])
  ```

### Pragma machinery

- **`pragma/strict-settings`** *(default on)* — when on, naming a setting that is
  not registered is an error; off downgrades it to a warning.
- **`pragma/strict-watchers`** *(default on)* — when on, an error while applying a
  pragma change is raised; off downgrades it to a warning.

## See also

- [Brackets and the call convention](brackets-and-call-convention.md) — the
  convention `compiler/call-brackets` enforces.
- [Grammar](../grammar.md) — the reader grammar the `reader/*` settings gate.
