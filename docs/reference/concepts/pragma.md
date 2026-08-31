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

A second read form exists for code that runs *during* compilation:

```koskari
(active-pragma)                          ; the pragma of the module being compiled
(active-pragma compiler/enable-asserts)  ; one of its settings
```

`(pragma KEY)` reads the pragma of the module the form is written in. A macro
body, though, is a closure over the module that *defined* it, while it runs
while some other module compiles — so inside a macro, `(pragma KEY)` would
answer for the wrong module. `(active-pragma KEY)` reads the pragma the
compiler has bound for the current compilation turn instead, so a macro's
expansion is governed by the settings of the module using it. Outside a
compilation turn it reads as `None`.

## What each setting means

Settings are grouped by what they affect: the reader (`reader/*`), the compiler
(`compiler/*`), and the pragma machinery itself (`pragma/*`).

### Reader

- **`reader/brackets`** *(default on)* — whether `[ ]` reads into a distinct
  bracket-pair value that remembers its origin, rather than a plain pair. Off
  makes `[ ]` and `( )` completely indistinguishable. See
  [Brackets and the call convention](brackets-and-call-convention.md).
- **`reader/index-brackets`** *(default on)* — whether a `[ ... ]` group
  attached directly to a just-read value (no intervening whitespace) desugars
  into an item access: `data["bar"]` reads as `(item data "bar")`. Groups
  chain left to right, so `grid[y][x]` reads as `(item (item grid y) x)`, and
  a space breaks the attachment — `foo [a]` stays two separate forms. Empty
  index brackets are a read error. `[reader/index-brackets False]` turns it
  off. Because the desugar happens in the reader, the result is an ordinary
  `(item …)` form and composes accordingly — for example `&data["bar"]` is a
  ref to that slot.
- **`reader/dot-attrs`** *(default on)* — whether `.` reads as attribute
  access. This governs both spellings at once: `foo.bar` written inside a
  single atom, and a `.` attached to any just-read value, so
  `(get-fn x).baz` and `"".join` are attribute reads on a form and on a
  literal. Both produce `(attr …)`, chain left to right
  (`obj.a.b`), and compose with index brackets in either order —
  `foo[0].bar` and `foo.bar[0]` both read the way they look. A space breaks
  the attachment, so `.name` after whitespace stays an ordinary symbol and
  the leading-dot method call `(.name obj)` is unaffected. With the feature
  on, a dot with no name after it (`foo.`, `foo..bar`, `(f x).`) is a read
  error naming the offending segment; off, a dot is inert in both spellings
  and `foo.bar` is an ordinary symbol.
- **`reader/string-segments`** *(default on)* — whether a `-` attached
  directly to a string literal's closing quote continues that literal into
  the next quoted segment, so a long string can be broken to fit a line
  without restructuring the call around an explicit join:

  ```koskari
  (print "This is one"-
         " long string broken"-
         " up across lines")
  ```

  The segments join into a `(#str …)` form, which folds back to a
  single constant when every segment is one, so the result costs nothing at
  runtime. Each segment carries its own prefix — an `f` segment
  interpolates, an `r` segment is raw — and the gap between a marker and
  its segment may be any amount of whitespace, on the same line or several
  lines down, with comments skipped. The marker is strictly adjacent to the
  closing quote: `"a" -` with a space is a string followed by a minus.
  Inside a layout block a continuation line must be indented at all, since
  column 0 ends the logical line. Once the marker is written a segment must
  follow, so `"a"- 5` is a read error.
- **`reader/layout`** *(default on)* — whether indentation-based layout blocks
  are read. Off leaves only s-expression syntax.
- **`reader/implicit-calls`** *(default on)* — whether several values on one
  layout line form an implicit call. Off makes more than one value on a line an
  error.
- **`reader/fences`** *(default on)* — whether triple-backtick fences read into
  `compile/apply-fence` forms. Off, backtick keeps only its quote and
  quasiquote readings. See
  [compile/apply-fence](../specials/compile-apply-fence.md).
- **`reader/decorators`** *(default on)* — whether leading `@` layout lines
  read as decorators, lowering the decorated form to `compile/decorate`. Off,
  `@` has no layout meaning and reads through the ordinary atom rules. This
  gates the reader surface only; `(decorate ...)` remains available as
  a macro regardless.

### Compiler

- **`compiler/resolution`** *(default `koskari`)* — how writes to names resolve.
  The value is required, one of:
  - `koskari` — strict: a write to an undeclared name is a compile error.
  - `scheme` — an unsatisfied write promotes to a module global.
  - `python` — a first write creates an implicit local unless declared global.
- **`compiler/call-brackets`** *(default `warn`)* — what happens when a
  bracket-pair `[ ]` reaches call position, where `( )` is conventionally
  expected. One of:
  - `warn` — emit a `SyntaxWarning`.
  - `error` — raise a compile error.
  - `ignore` — nothing; the convention is unenforced.

  A bare `[compiler/call-brackets]` selects `warn`. See
  [Brackets and the call convention](brackets-and-call-convention.md).
- **`compiler/stray-colon`** *(default `warn`)* — what happens when a bare `:`
  symbol is compiled as a name. Nothing binds `:`, so this is always a
  mistake; it most often comes from a layout form half-converted to
  s-expressions, where the trailing block colon of

  ```
  for-each [x (xs)]:
      body
  ```

  survives the rewrite as `(for-each [x (xs)]: body)` — which reads as
  `(for-each [x (xs)] : body)` and fails at runtime with a `NameError`. One
  of:
  - `warn` — emit a `SyntaxWarning`.
  - `error` — raise a compile error.
  - `allow` — nothing; the `:` compiles as an ordinary name lookup.

  A bare `[compiler/stray-colon]` selects `warn`. Note that a trailing colon
  attached to a name is a *keyword*, not a stray colon: `(when foo: (foo))`
  passes the keyword `foo:`, which `compiler/keyword-literals` covers
  instead.
- **`compiler/keyword-literals`** *(default `warn`)* — what happens when an
  unquoted keyword is compiled as a literal value. A keyword evaluates to
  itself, which is legal but is more often a typo: `(if foo: 1 2)` tests the
  keyword `foo:` for truthiness — always true — rather than reading the
  variable `foo`. One of:
  - `warn` — emit a `SyntaxWarning`.
  - `error` — raise a compile error.
  - `allow` — nothing; the keyword compiles to itself.

  Quote the keyword to state that the literal is what you meant; `'foo:`
  compiles through the quote special and is always exempt. Two other uses
  are structural rather than value positions, so they are never flagged:
  keyword *arguments* (`(print 1 sep: "-")`) and keyword *markers* consumed
  by a form (`(cond [(> x 1) 'big] [else: 'small])`).
- **`compiler/enable-asserts`** *(default on)* — whether `(assert …)` forms
  expand to their check. Off, an `assert` compiles away entirely: neither the
  condition nor the message is compiled or evaluated, mirroring Python's `-O`
  behavior. The gate is consulted as each `assert` is compiled, so a change
  affects only the asserts that appear after it. See
  [compile/assert](../macros/compile-assert.md).
- **`compiler/bare-control-words`** *(default `convert`)* — what happens when
  a bare `return`, `continue`, `break`, `raise`, `yield`, or `pass` symbol is
  compiled. These
  statement words are call forms in Koskari, and their bare forms expand the
  lone symbol into its call form — `return` becomes `(return)` — matching the
  Python-style spelling. `pass` expands to `(begin)`, a no-op that evaluates
  to `None`. One of:
  - `convert` — expand silently.
  - `warn` — emit a `SyntaxWarning`, then expand.
  - `error` — refuse with a compile error naming the corrective spelling.

  Each module answers for its own setting: the gate reads the pragma of the
  module being compiled, so a strict module does not harden its neighbors.
  The cost of conversion is that these six names always expand and can never
  name a variable. See [compile/return](../specials/compile-return.md),
  [compile/continue](../specials/compile-continue.md),
  [compile/break](../specials/compile-break.md),
  [compile/raise](../specials/compile-raise.md),
  [compile/yield](../specials/compile-yield.md), and
  [compile/begin](../specials/compile-begin.md) (`pass`).
- **`compiler/rewrite-limit`** *(default 32)* — how many macro-expansion
  rewrites a single form may take before the compiler reports it as
  divergent. A macro that expands into an ever-growing form stops with a
  compile error instead of hanging. An expansion that repeats an earlier
  form is reported immediately as a cycle, regardless of this setting.
  `0` removes the limit; legitimate expansion chains are only a few
  rewrites deep, so the default leaves generous headroom.
- **`compiler/tco-enable`** *(default on)* — whether tail calls are optimized.
- **`compiler/convert-fast`** *(default on)* — whether the compiler lowers local
  variable access to its fast form. Off disables that optimization.
- **`compiler/compress`** *(default off)* — reserved for a future bytecode
  compression pass; currently inert.
- **`compiler/cache-function`** *(default on)* — whether function and lambda
  bodies attach a per-site cache to attribute and method-call instructions,
  so a repeated access with the same receiver type skips the resolution walk.
  On is the baseline; the reused code that loops flow through is where it
  pays. See [Attribute and method caching](opcode-caching.md).
- **`compiler/cache-expression`** *(default off)* — the same caching for
  top-level module and REPL expressions. Off by default because top-level
  code usually runs once as it loads, so a cache would not amortize. A bare
  `[compiler/cache-expression]` turns it on. See
  [Attribute and method caching](opcode-caching.md).
- **`compiler/protect-code`** *(default off)* — whether each compiled code
  object's bytecode storage is sealed read-only at the OS level the moment it
  is built. On, a stray or hostile write to live bytecode faults instead of
  corrupting the interpreter, at the cost of a full memory page per code
  object. A bare `[compiler/protect-code]` turns it on. See
  [Code sealing and arena packing](code-sealing.md).
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

  See [Code sealing and arena packing](code-sealing.md) for the model
  and its tradeoffs.

### Pragma machinery

- **`pragma/strict-settings`** *(default on)* — when on, naming a setting that is
  not registered is an error; off downgrades it to a warning.
- **`pragma/strict-watchers`** *(default on)* — when on, an error while applying a
  pragma change is raised; off downgrades it to a warning.

## See also

- [Brackets and the call convention](brackets-and-call-convention.md) — the
  convention `compiler/call-brackets` enforces.
- [Grammar](../grammar.md) — the reader grammar the `reader/*` settings gate.
