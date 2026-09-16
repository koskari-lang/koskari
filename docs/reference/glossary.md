# Glossary

Shared terms used across tutorials and reference pages. For longer explanations
with examples, see [Concepts](concepts/index.md).

**async evaluation**
: Running Koskari under an asyncio-hosted loader or REPL; top-level and `await`
  may suspend on Python awaitables. See
  [Demand, async, and defer](concepts/demand-async-and-defer.md).

**bind**
: Attach a name to a ref in the current namespace (introduce or replace the
  mapping). Contrast **store**. See [Bindings and refs](concepts/bindings-and-refs.md)
  and [compile/bindq](specials/compile-bindq.md).

**bracket-pair**
: A `pair` subtype the reader produces from `[ ]` source forms. Value-equal to
  and interchangeable with an ordinary pair; `[ ]` is **not** a vector literal
  and compiles like `( )` by default. The marker records bracket origin so the
  `( )` = call / `[ ]` = data convention can be checked via the
  `compiler/call-brackets` pragma. See [Brackets and the call convention](concepts/brackets-and-call-convention.md).

**bytecode**
: `KosCode` instruction sequence executed by the Koskari VM.

**datum**
: Either the `(datum expr)` macro form that captures source position in templates,
  or a hygiene syntax box wrapping captured macro fragments. See
  [Macro matching and hygiene](concepts/macro-matching-and-hygiene.md) and
  [compile/datum](macros/compile-datum.md).

**defer**
: Single-shot await handle; not auto-awaited at use sites—use explicit
  [compile/await](specials/compile-await.md), which consumes it (reuse
  raises `RuntimeError`). See
  [Demand, async, and defer](concepts/demand-async-and-defer.md) and
  [koskari.lib](../api/koskari-lib.md).

**demand evaluation**
: Running Koskari to completion without an event loop for that run; suspension
  that needs asyncio raises `KoskariSyncViolation`. Default import path. See
  [Demand, async, and defer](concepts/demand-async-and-defer.md).

**deref**
: Read the value held by a ref. See [Bindings and refs](concepts/bindings-and-refs.md)
  and [compile/deref](specials/compile-deref.md).

**form**
: A Koskari expression: atoms, lists, or layout-derived s-expressions.

**layout**
: A read-time handler for a block head (`layout/if`, `layout/block`, …).
  Handlers run during layout expansion (`apply_layout` / `apply_layouts`) with
  signature `(reader, source, peers)`. For general layout syntax, see
  [Koskari and s-expressions](concepts/layout-and-s-expressions.md).

**layout block**
: A header line ending in structural `:`, a newline, and an indented body.
  See [Reader and layout syntax](reader-layout.md).

**layout line**
: A depth-0 source line without a structural trailing `:`. See
  [Reader and layout syntax](reader-layout.md).

**layout mode**
: The layout reader surface at depth 0: blocks, lines, and embedded
  s-expressions. See [Reader and layout syntax](reader-layout.md).

**macro**
: A compile-time transformer defined with `define-syntax`. Expands to ordinary
  forms before compilation.

**match**
: In compile-time docs, test a source form against a pattern and expand the
  matching arm (`syntax-rules`, `syntax-match`, special patterns, etc.). Not the
  same as `switch` or runtime value dispatch. See
  [Macro matching and hygiene](concepts/macro-matching-and-hygiene.md).

**namespace**
: Lexical environment mapping names to reference cells; each name resolves through
  a ref, not a raw value. See [Bindings and refs](concepts/bindings-and-refs.md).

**on-demand?**
: Builtin predicate: true when the current path forbids bare `(await)` (demand
  context). See [on-demand?](runtime/on-demand.md) and
  [Demand, async, and defer](concepts/demand-async-and-defer.md).

**opcode**
: One VM instruction (see [Opcode reference](vm/opcodes/index.md)).

**pair**
: A cons cell (`car` / `cdr`); lists are proper chains of pairs terminated by `nil`.

**parameter scope**
: Dynamic configuration shared across calls in an evaluation **domain**, separate
  from lexical `let` / `define` bindings. Names are declared with
  [`define-parameter`](macros/compile-define-parameter-and-compile-parameterq.md);
  [`parameterize`](specials/compile-parameterize.md) temporarily overrides values
  for a body (restored on exit). Consumers read the current value via
  [`ParamRef`](macros/compile-ref.md) (for example `(ref (parameter NAME))`).
  Contrast **lambda** formals, which are ordinary lexical parameters.

**pragma**
: Module-scoped switches that tune reader and compiler behavior — bracket-call
  checking, name resolution, layout reading, and so on. Set with the `pragma`
  form; forward-only and never inherited by imported modules. See
  [Pragmas and feature settings](concepts/pragma.md).

**ref**
: Mutable handle to a binding site (variable, attribute, item, parameter, etc.).
  See [Bindings and refs](concepts/bindings-and-refs.md) and [compile/ref](macros/compile-ref.md).

**REPL**
: Read-eval-print loop started by `koskari` with no script argument.

**resolve**
: Look a symbol up the way evaluation does: the value held in the current
  namespace, else in each enclosing namespace, else a module's builtins.
  Contrast **deref**, which reads only the current namespace's own binding.
  See [Bindings and refs](concepts/bindings-and-refs.md).

**special form**
: A form whose head is a compiler symbol such as `lambda`, or `cond`.

**store**
: Write a value through the ref a name already uses; does not change which ref
  the name maps to. Contrast **bind**. See [Bindings and refs](concepts/bindings-and-refs.md)
  and [compile/setq](specials/compile-setq.md).

**thunk**
: Brace syntax `{ ... }` for a short anonymous function with inferred formals
  (`%1`, `%2`, ...); expands to [compile/gensym-thunk](specials/compile-gensym-partial.md).
  Dyn-wind before/after bodies are also described as null-arity thunks in
  [compile/dyn-wind](specials/compile-dyn-wind.md).

**unpack**
: Assign several names from one value using a list pattern (including `*:` rest).
  See [Bindings and refs](concepts/bindings-and-refs.md),
  [compile/define-values](specials/compile-define-values.md), and
  [compile/setq-values](specials/compile-setq-values.md).
