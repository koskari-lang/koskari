# Koskari Matcher Syntax Extensions for Hygiene Discovery

## Purpose

Define a small extension to the existing `MATCH(...)` pattern language so special forms can declare:

1. which sub-structures introduce bindings
2. which regions are in scope for those bindings
3. what binding visibility mode applies

The matcher serves two purposes:

* **Maintainability**: Special forms obtain a dict of named matches instead of manually unpacking cons pairs. The matcher becomes the single source of truth for form structure.
* **Hygiene discovery**: The same matcher metadata identifies bindings and their scope regions, enabling generic hygiene logic without special-casing form names.

The goal is to let macro/template hygiene remain generic. The expander should not hardcode knowledge of `compile/let`, `compile/lambda`, `compile/define`, etc. Instead, it should inspect matcher metadata exposed by registered specials and derive a gensym-renaming map from that metadata.

**Related (architecture):** [../language/unified-matcher-transformer-model.md](../language/unified-matcher-transformer-model.md) describes how syntax-rules, procedural macros, and specials share matcher-driven structure and the same post-expand hygiene and unbox stages.

---

## Macro modes and hygiene

Koskari distinguishes two macro styles, each with a different hygiene strategy:

### syntax-rules: bare name substitution

Declarative macros with known structure. Hygiene is **automatic** and handled inside the expander:

* The expander collects binding symbols from the template via `_collect_binding_symbols_matcher` (matcher-driven, using `match(..., with_specs=True)` and role helpers).
* During expansion, introduced binders are renamed to fresh gensyms.
* Substituted pattern variables replace whole nodes; the expander never descends into substituted content. No provenance tracking is required.
* Template authors write normal names (`t`, `loop`); the expander handles freshening transparently.

**Safe by default.** No extra syntax for macro authors.

### defmacro: use-gensyms facility

Procedural macros that return arbitrary code. Hygiene is **opt-in**:

* The expander cannot infer binding structure from procedural expansion.
* We offer `(use-gensyms [%x %y ...] body)` as an explicit translation layer.
* The macro author wraps expansion output in `use-gensyms` when introducing bindings.
* `use-gensyms` allocates fresh symbols for each listed name and rewrites occurrences in the body.

**Naming convention**: Introduced names use a reserved prefix (e.g. `%t`, `%loop`). User code uses normal identifiers (`t`, `loop`). Because they are distinct symbols, `use-gensyms` can rewrite `%t` without touching `t` in substituted user code. No provenance or "don't descend" markers are needed.

**Self-regulation**: Macros that introduce bindings must explicitly use `use-gensyms`. If they forget, capture bugs may occur — that is the accepted trade-off of procedural macros. When used correctly, defmacro authors get hygiene.

### Summary

| Mode        | Hygiene        | Mechanism                    |
|-------------|----------------|------------------------------|
| syntax-rules| Automatic      | Bare substitution in expander|
| defmacro    | Opt-in         | `use-gensyms` wrapper        |

---

## Non-goals

This spec does **not** define:

* bytecode fragment behavior
* runtime namespace semantics
* full `syntax-rules` semantics
* internal-define body lowering
* macro phase separation rules

It only defines:

* matcher-role additions
* how those roles are interpreted by hygiene discovery
* how gensym mappings are applied to introduced names
* the split between syntax-rules (automatic hygiene) and defmacro (opt-in via `use-gensyms`)

---

## Design constraints

### 1. Shared structure language

The matcher system is already used by special forms and by `define-syntax` internals. Hygiene discovery should reuse that same structure language rather than invent a parallel form-description DSL.

### 2. No special-name hardcoding

The hygiene pass must not contain code like:

* `if head == compile/let`
* `if head == compile/lambda`
* `if head == compile/define`

Instead it should resolve a special, inspect its matcher pattern(s), and derive binding/scope information from the winning matcher.

### 3. Pre-compile only

The hygiene pass runs on expanded source before normal special compilation. Its output is still normal source syntax, just with introduced identifiers alpha-renamed.

### 4. Lightweight provenance (syntax-rules)

For syntax-rules expansion, the system assumes template instantiation can distinguish at least two categories of syntax subtree:

* **introduced**: copied from the macro template
* **substituted**: inserted from pattern-variable matches

The hygiene pass must never rewrite identifiers inside substituted subtrees. In practice, the expander achieves this by replacing substituted nodes wholesale and never descending into them. For defmacro, `use-gensyms` with the `%`-prefix convention avoids provenance entirely.

---

## Matcher role additions

The existing matcher system gains semantic roles for binding and scope. Hygiene discovery uses `match(..., with_specs=True)` to obtain both match results and the matcher specs that produced each slot; role helpers (`is_binder_spec`, `is_scope_region_spec`, `extract_binder_symbols`) interpret those specs.

### Scope regions: `REST(name)` and `EXPR(name)`

Body-like regions are captured by existing structural matchers:

* **REST(name)** — consumes remaining elements as a list; used for let body, lambda body, etc.
* **EXPR(name)** — consumes a single form

These serve as scope regions when referenced by `scope` or `scopes` on binder matchers. Role helpers treat REST and EXPR as scope regions (`is_scope_region_spec`).

### `SYMBOL(name)`

Match a single identifier that introduces a binding. Examples: function name in shorthand define, recursive name in named let.

#### Parameters

* `name`: capture key

Binder visibility is inferred from the pattern. If a sibling `BINDINGS` or `FORMALS` uses `scope`/`scopes`, those slots define visibility; otherwise visibility defaults to any REST or EXPR key in the same pattern.

### `BINDINGS(name, *, mode="parallel", scope=None, scopes=None)`

Match a binding list structure and expose the identifiers introduced by that structure. Binds `BindingsResult` with `source`, `source_pos`, `names`, and `values`; supports unpacking `names, values = matched["bindings"]`. Implemented in `koskari.compile.helpers_ng` (matcher_ng composites). Used by let, let*, letrec, etc.

#### Parameters

* `name`: capture key
* `mode`: binding visibility across initializer expressions
* `scope`: optional single result-slot name where these binders are in scope (e.g. `"body"`)
* `scopes`: optional list of result-slot names; use when multiple regions share the scope

**Scope inference**: If both `scope` and `scopes` are omitted, scope defaults to any REST or EXPR key in the same pattern. The let form uses `scope="body"` explicitly.

Supported modes:

* `parallel` — initializers do not see newly introduced bindings
* `sequential` — each binding becomes visible to later initializers
* `recursive` — all bindings are visible during all initializers and body

Initial implementation supports `parallel`.

### `FORMALS(name, *, scope=None, scopes=None)`

Match a formal-parameter structure. Similar to BINDINGS but for lambda/formals. Uses `scope`/`scopes` for body regions. Binds `FormalsResult` with `source` (raw formals), `source_pos`, and unpackable `(positional, defaults, kwonly, star, starstar)`. Use `.source` when the raw formals pair or symbol is needed. Implemented in `koskari.compile.helpers_ng` (matcher_ng composites).

### `DEFINE(name)`

Match a single symbol that introduces a binding. Used for forms like `(define X Y)` or `(define X [args] body...)`. Implemented in the native matcher (`koskari.lib.Matcher`) and `matcher_ng` patterns.

**Important**: DEFINE does **not** indicate hygienic rewriting of its value slot. The name (X) is gensym'd; the value expression (Y) is a "value fed in" position and is not recursed into for DEFINE's binding. Use `VALUE` for the value slot.

### `VALUE(name)`

Capture one element; value fed in, not a scope region. Same structural behavior as BIND, but documents intent: the slot is a value expression that does not receive hygienic recursion from binders in the same pattern. Use for `(define X Y)` where Y must not be treated as a scope region. Implemented in the native matcher (`koskari.lib.Matcher`) and `matcher_ng` patterns.

---

## Optional later roles

Not required for the current implementation, but reserved for extension:

* `BINDING(name)` — single binding like `(with [(expr) as: x] ...)`
* `BIND_VALUES(...)` — destructuring / multiple-value binding
* `INIT(...)` — explicitly marks initializer expressions outside body scope
* `BODY_DEFINES(...)` — body region that supports internal defines
* `PATTERN_BINDINGS(...)` — future pattern-match binders
* `BIND_KEYWORDS(...)` — keyword-introducing forms if ever needed

---

## Pattern selection model

A special may expose multiple matcher alternatives.

Example:

```python
_LET_PATTERNS = [
    MATCH(SYMBOL("name"), BINDINGS("bindings", mode="parallel", scope="body"), REST("body")),
    MATCH(BINDINGS("bindings", mode="parallel", scope="body"), REST("body")),
]
```

Hygiene discovery must:

1. resolve the special object from the head symbol
2. try the special's matcher alternatives in normal order
3. select the same winning pattern shape that ordinary special matching would select
4. use **that winning matcher** as the source of binding/scope metadata

The hygiene pass must never merge metadata from multiple unmatched alternatives.

---

## Match result normalization

To make hygiene logic generic, the winning matcher is normalized into a compact binding-scope description.

### Normalized shape

```python
BindingRegion(
    binders=[...],
    bodies=[...],
    mode="parallel" | "sequential" | "recursive" | "plain",
)
```

### Binder record

```python
Binder(
    source_name=<printed symbol>,
    source_node=<matched syntax node>,
    binder_kind="sym" | "bindings" | "formals",
    scopes=("body", ...),  # slot names where this binder is in scope
)
```

### Body record

```python
BodyRegion(
    capture_name="body",
    syntax=<matched subtree>,
    mode="plain" | "sequential",
)
```

Scope regions come from REST or EXPR slots when referenced by BINDINGS/FORMALS `scope` or `scopes`.

The matcher engine may provide a helper that converts the winning match into these normalized records.

---

## Gensym application model

### Overview

Once a form is matched and normalized, hygiene proceeds by building a rename environment for **introduced binders** and applying that environment only across eligible introduced syntax regions.

### Rule 1: only introduced binders are freshened

Identifiers introduced by the macro template are renamed to fresh gensyms when they are recognized as binders by matcher metadata.

Substituted identifiers from macro input are never gensymmed by this pass.

### Rule 2: only introduced references are rewritten

When a rename environment maps:

* `t -> t#G17`

that mapping is applied only to introduced identifier occurrences in the designated scope regions.

Occurrences inside substituted syntax fragments are left unchanged.

### Rule 3: scope regions come from matcher metadata

The pass does not guess lexical scope from symbol names or list shape alone.

It uses:

* `BIND_*` roles (SYMBOL, BINDINGS, FORMALS) to discover newly introduced names
* REST/EXPR slots (referenced by `scope`/`scopes`) to determine where those names may be rewritten
* role `mode` to determine initializer behavior

### Note on provenance

The provenance requirement (introduced vs substituted) applies to **syntax-rules** expansion, where the expander implicitly never descends into substituted nodes. For **defmacro**, the `use-gensyms` facility avoids provenance entirely: introduced names use a distinct prefix (`%`), so rewriting them cannot collide with user identifiers.

---

## Rewrite algorithm

### Inputs

* expanded pre-compile source form
* provenance on each subtree: introduced vs substituted
* special matcher metadata

### Output

* equivalent source form with introduced binders and their bound introduced references rewritten to gensyms

### Algorithm sketch

#### Step 1: inspect form head

If form head resolves to a special with matcher metadata:

* attempt matcher selection
* if no matcher matches, recurse structurally with no special-specific hygiene rule
* if a matcher matches, normalize binding/body metadata

#### Step 2: allocate gensyms for binders

For each binder discovered from `BIND_*` roles:

* allocate a fresh symbol derived from the original printed name
* store mapping from original binder node identity to fresh symbol

**Binder vs reference identity**: The binder positions themselves are rewritten by node identity (the matched syntax node). References are rewritten by symbol-in-scope: any introduced identifier in the designated scope region that matches the binder's printed name is replaced with the gensym. Mapping is keyed by binder record, not by printed name alone, to support multiple binders with the same name in different scopes.

#### Step 3: rewrite binder positions

Rewrite the matched binder occurrences themselves (by node) to the fresh gensyms.

Examples:

* `let` binding names
* `lambda` parameters
* named-let self name

#### Step 4: rewrite scope regions

Walk each scope region (REST or EXPR slot designated by `scope`/`scopes`).

During the walk:

* if subtree provenance is `substituted`: stop descending, leave as-is
* if identifier occurrence (by symbol-in-scope) resolves to one of the active introduced binders: replace with gensym
* otherwise recurse normally

#### Step 5: obey binding mode

When `mode="parallel"`:

* initializer expressions inside `BINDINGS` are outside the active rename scope for those same binders
* only the declared body regions are rewritten

When `mode="sequential"`:

* later initializer expressions are rewritten with earlier binder gensyms
* body regions are rewritten with all binders

When `mode="recursive"`:

* all initializer expressions and body regions are rewritten with all binder gensyms

Initial implementation may support only `parallel` and treat others as reserved.

#### Step 6: recurse into nested forms

After applying the local binder renames for the current matched special, continue recursively into nested forms so inner binding forms receive their own hygiene processing.

Nested binding forms shadow outer rename mappings in the usual lexical way.

---

## Provenance requirement

This spec assumes template instantiation preserves a minimal provenance bit per copied subtree.

### Required states

* `introduced`
* `substituted`

### Semantics

* nodes copied from template literal structure are `introduced`
* nodes inserted from pattern-variable matches are `substituted`

### Consequence

A scope-region rewrite walk must never descend into `substituted` subtrees.

This is the core rule that prevents user syntax from being accidentally rewritten as though it were macro-introduced syntax.

---

## Examples

### Example 1: plain `let`

Matcher:

```python
MATCH(BINDINGS("bindings", mode="parallel", scope="body"), REST("body"))
```

Template output before hygiene:

```lisp
(let [[t 1]]
  (+ t x))
```

Where:

* binding name `t` is introduced
* body `(+ t x)` is introduced except that `x` may have been substituted

Generated mapping:

* `t -> t#G1`

After hygiene:

```lisp
(let [[t#G1 1]]
  (+ t#G1 x))
```

If `x` was substituted syntax containing `t`, that subtree is not rewritten.

### Example 2: named `let`

Matcher:

```python
MATCH(SYMBOL("name"), BINDINGS("bindings", mode="parallel", scope="body"), REST("body"))
```

Template before hygiene:

```lisp
(compile/let loop [[n 10]]
  (if n
      (loop (- n 1))
      0))
```

Generated mappings:

* `loop -> loop#G1`
* `n -> n#G2`

After hygiene:

```lisp
(compile/let loop#G1 [[n#G2 10]]
  (if n#G2
      (loop#G1 (- n#G2 1))
      0))
```

Later special expansion may still lower named let into lambda-call form; hygiene has already disambiguated identities before compilation.

### Example 3: lambda

Matcher:

```python
MATCH(FORMALS("formals", scope="body"), REST("body"))
```

Before hygiene:

```lisp
(lambda [x]
  (+ x y))
```

After hygiene:

```lisp
(lambda [x#G1]
  (+ x#G1 y))
```

Only introduced body references are rewritten.

### Example 4: defmacro with use-gensyms

A procedural macro that introduces a temporary:

```lisp
(use-gensyms [%t]
  (let [[%t 99]]
    (+ %t x)))
```

Where `x` was passed from the caller and may expand to user code like `(foo t)`.

* `%t` is rewritten to `t#G1` everywhere in the body.
* The user's `t` in `(foo t)` is unchanged — it is a different symbol from `%t`.
* No provenance tracking needed; the naming convention provides the separation.

---

## Special matcher API expectations

Registered specials should expose matcher metadata in a stable way.

### Minimal API

Each special object must provide:

* ordered matcher alternatives
* enough structure for the hygiene pass to identify role nodes (`BIND_*`, `BODY`, etc.)

Possible helper API:

```python
special.get_match_patterns() -> list[MatchPattern]
special.match_for_hygiene(rest) -> (pattern, matched) | None
```

Optional helper:

```python
normalize_binding_regions(pattern, matched) -> list[BindingRegion]
```

The exact API shape is implementation detail; the semantic requirement is that the hygiene pass can reuse the same matcher grammar the special itself uses.

---

## Error handling

### Unsupported role mode

If a matcher uses a role mode not yet implemented by hygiene:

* raise a compile-time hygiene error
* include special name and mode name

### Ambiguous binder extraction

If a matcher role succeeds structurally but the normalization logic cannot extract concrete binders from the matched subtree shape:

* raise a compile-time hygiene error
* include special name and capture name

### Missing provenance

If the form reaches hygiene processing without provenance markers available:

* either reject hygienic processing for that expansion
* or conservatively skip all body rewriting

The preferred behavior is explicit failure rather than unsafe broad rewriting.

---

## Implemented vs planned

**Implemented**:

* `SYMBOL` — single-name binder
* `BINDINGS(slot, mode="parallel", scope=..., scopes=...)` — binding-list capture with scope
* `REST`, `EXPR` — structural matchers; EXPR captures single form, scope region
* `match(..., with_specs=True)` — returns `(result_dict, specs_dict)` for hygiene discovery
* Role helpers: `is_binder_spec`, `is_scope_region_spec`, `extract_binder_symbols`
* `_collect_binding_symbols_matcher(template, find_compiled)` — drives hygiene from special forms via matcher metadata
* Used by `compile/let` (named and plain)

**Planned**:

* `FORMALS` — for lambda and similar
* `BINDING` — single binding
* `DEFINE` — single-name shorthand define
* Sequential/recursive binding modes
* Internal-definition bodies

---

## Summary

The matcher language is extended with semantic roles describing:

* **what introduces names**: `SYMBOL`, `BINDINGS`, `FORMALS`
* **where those names apply**: REST and EXPR slots referenced by `scope`/`scopes`
* **how visibility behaves**: role modes such as `parallel`

Matchers serve a dual role: parsing (named dict for maintainability) and hygiene discovery (binding/scope metadata for generic freshening).

Two macro modes are supported:

* **syntax-rules**: Bare name substitution during expansion. Automatic hygiene; the expander handles freshening. Safe by default.
* **defmacro**: `use-gensyms` facility for opt-in hygiene. Macro authors wrap output in `(use-gensyms [%x %y] body)` when introducing bindings. The `%`-prefix convention avoids collision with user names, so no provenance is required.

A generic hygiene pass (for syntax-rules) then:

1. resolves the special
2. selects the winning matcher
3. normalizes binder/body metadata
4. allocates gensyms for introduced binders
5. rewrites only introduced references inside declared body regions
6. never descends into substituted syntax fragments

This allows hygiene to be driven by special-declared matcher metadata rather than special-cased expander logic.
