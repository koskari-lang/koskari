# Koskari Design: Fenced Foreign Literal Expansion

## Status

Proposed design.

## Summary

Add a fenced foreign-literal mechanism to Koskari source. A fenced block captures raw source in a declared foreign format such as JSON, then lowers it at compile time into ordinary Koskari source.

This is **not** a runtime parser feature. The fence handler is a compile-time transliterator that receives raw fenced source plus source metadata and returns Koskari source to compile in place.

The core special is `compile/apply-fence`, deliberately parallel to compile-time
`:block:` layout expansion (`koskari.reader.layouts.apply_layout`): a dedicated
compiler form that turns captured structure into ordinary source before further
compilation.

## Motivation

There are cases where the most natural notation is not s-expr syntax:

* JSON for nested config-shaped data
* YAML for human-authored mappings/sequences
* XML/HTML for document or template-like structures
* Future domain-specific structured literals

The goal is to let authors write the notation that best matches the data they are expressing, while preserving Koskari's compile-time transformation model.

## Design Goals

* Keep foreign fenced literals in the **compile-time** pipeline, not runtime.
* Reuse the existing two-phase source transformation model.
* Preserve accurate source positions for diagnostics.
* Allow future fence-local metadata and feature flags.
* Keep v1 narrow and predictable.
* Avoid turning fences into a general template language.

## Non-Goals

* No arbitrary expression interpolation in v1.
* No runtime parsing of fence bodies.
* No attempt to make fenced JSON be fully equivalent to general Koskari expression syntax.
* No requirement that all fences lower to generic dict/list-only structures; a fence may lower to domain-specific constructor forms.

## Architectural Fit

Koskari already separates capturing reader structure from expanding it: block
backtick rewrites to `compile/quasiquote`, and macro/syntax finalize applies
`koskari.reader.layouts.apply_layout` in module namespace before hygiene —
without handing fence policy to the reader itself. This fence mechanism should
follow the same pattern (reader lowers to a compile-time applicator; handler
returns ordinary source) rather than teaching the reader to execute fence
handlers directly.

Accordingly:

* the **reader** recognizes fenced syntax and captures raw source
* the reader lowers that syntax to a `compile/apply-fence` form
* the **compiler special** resolves `fence/<name>`
* the fence handler returns ordinary Koskari source
* compilation continues on that returned source

This keeps extensible semantics in compiler space, where Koskari already expects source-to-source transformation.

## Surface Syntax

Proposed fence syntax:

````text
``` json
{ "foo": 123, "bar": 456 }
```
````

The first line names the fence format. The remainder of the fence body is passed through as raw source text.

### Future metadata line support

The design intentionally leaves room for optional fence-local metadata immediately associated with the fence declaration, for example:

````text
``` json
expressions: true
{ "foo": bar }
```
````

or an equivalent header/metadata encoding.

The exact surface syntax for metadata is intentionally left open in this design. What matters is that `compile/apply-fence` accepts metadata as an optional structured argument so future feature flags do not require redesigning the core protocol.

## Core Special

### Symbol

`compile/apply-fence`

### Role

Compile-time expansion of a fenced foreign literal into ordinary Koskari source.

### Conceptual syntax

```scheme
(compile/apply-fence NAME RAW-SRC)
(compile/apply-fence NAME RAW-SRC META)
```

Where:

* `NAME` is the fence format identifier, such as `json`
* `RAW-SRC` is the captured fence payload as raw text
* `META` is optional metadata describing source span, feature flags, and future fence options

### Behaviour

`compile/apply-fence`:

1. validates its arguments
2. resolves handler `fence/<name>` in the compile-time namespace
3. calls that handler with the raw source and optional metadata/context
4. requires the handler to return valid Koskari source
5. compiles the returned source in place

This is analogous to `apply_layout` for `:block:` conses, but for foreign fenced
source instead of layout reader structure.

## Reader Responsibilities

The reader should do as little policy work as possible.

Reader responsibilities:

* recognize triple-backtick fence syntax as a distinct source form
* capture the declared fence name
* capture raw body text exactly enough for downstream diagnostics
* attach source-location metadata for the fence and payload
* lower the whole thing to `compile/apply-fence`

Reader non-responsibilities:

* do not parse JSON/YAML/XML/etc.
* do not invoke fence handlers directly
* do not decide semantic policy for foreign content

## Fence Handler Contract

A fence handler is a compile-time transliterator.

Conceptually:

```scheme
(fence/json raw-src)
(fence/json raw-src meta)
(fence/json raw-src meta compiler-context)
```

Exact call signature can be chosen during implementation, but the semantic contract should be:

* input: raw fenced source plus metadata/context
* output: a valid Koskari source form to be compiled in place

A handler may return:

* pure literal-building source
* constructor calls
* quoted/quasiquoted source
* `compile/begin` if multiple sequential forms are genuinely needed

A handler must not return a runtime value object in place of source unless that object is already valid source in Koskari terms.

## Metadata

Metadata is optional in v1 but should be present in the protocol from the start.

Recommended metadata contents:

* source filename/module identity
* start line/column of the fence payload
* end line/column if available
* fence name as parsed
* future feature flags/options

Examples of future metadata use:

* `expressions: true`
* strict vs extended mode
* namespace selection for embedded names
* HTML/XML whitespace mode
* parser-specific options

This allows future fence evolution without changing the reader/special contract.

## V1 Fence Semantics: JSON

V1 should start with a deliberately narrow JSON fence.

### Supported model

The JSON fence is mostly normal JSON with one narrow Koskari extension:

* ordinary JSON strings remain string literals
* ordinary JSON numbers remain numeric literals
* `true`, `false`, and `null` remain JSON literals
* arrays and objects remain arrays and objects
* **bare identifiers may appear in value position and lower to ordinary Koskari name references**

Example:

````text
``` json
{ "foo": bar, "count": 3, "enabled": true }
```
````

Lowers conceptually to something like:

```scheme
(#dict "foo" bar
       "count" 3
       "enabled" true)
```

or to whatever canonical dict-construction source Koskari chooses.

### Important restrictions for v1

Bare identifiers are allowed **only in value position**.

Allowed:

```json
{ "foo": bar }
{ "items": [1, 2, bar] }
```

Not allowed:

```json
{ foo: bar }
{ "foo": (+ 1 2) }
{ "foo": config.user }
```

These restrictions are important because they preserve the feature as "JSON plus one narrow extension" rather than creating a second full expression grammar inside JSON fences.

### Meaning of bare identifier

In v1 JSON fences, a bare identifier means an ordinary Koskari **name reference**, not a symbol literal.

So:

```json
{ "foo": bar }
```

means the value position is compiled as the source form `bar`, exactly as though that name had appeared in surrounding Koskari source.

It does **not** mean quoted symbol literal unless a future mode explicitly opts into that.

### Reserved JSON literals

Within JSON fences:

* `true`
* `false`
* `null`

always mean the JSON literals, not name references, even if the surrounding program has bindings with those names.

## Why v1 JSON Should Keep Quoted Keys

Do **not** allow bare keys in v1.

Keep this valid:

```json
{ "foo": bar }
```

Keep this invalid:

```json
{ foo: bar }
```

Reasons:

* it preserves the visual identity of JSON
* it avoids ambiguity between key strings and name references
* it keeps the parser extension small
* it avoids drifting into a completely separate object literal syntax

## Lowering Policy

Fence handlers should lower to **canonical Koskari source** for the intended resulting value.

For JSON/YAML-like data, that likely means collection builders or literal constructors. For HTML/XML, that may mean domain-specific constructors rather than generic dict/list structures.

The semantic requirement is:

> the fence handler returns Koskari source representing the intended runtime value or structure

not:

> every fence must return only generic nested dict/list literals

## Error Handling

Fence expansion errors must point into the fence body, not merely at the surrounding `compile/apply-fence` form.

The metadata contract should be sufficient to support:

* parse errors with accurate line/column relative to the original module source
* diagnostics that mention the fence name
* future nested-feature diagnostics if fence-local options are added

Examples of error classes:

* unknown fence type / unresolved `fence/<name>`
* malformed foreign source
* disallowed extended token in strict mode
* invalid bare identifier placement
* handler returned non-source or invalid source

## Trust and Execution Model

Custom fence handlers are compile-time plugins.

That means a module using custom fences may execute arbitrary compile-time code via the fence handler resolution path, exactly as with other compile-time extensibility mechanisms.

This is acceptable, but it should be documented explicitly.

## Open Design Questions

### 1. Exact surface syntax for metadata

The protocol should support metadata now, but the concrete fence-header syntax can be settled later.

### 2. Strict JSON vs extended JSON naming

There are two reasonable policies:

* `json` means Koskari's extended JSON fence
* strict JSON is a separate mode/name if needed

or:

* `json` means strict JSON
* `jsonx` / `json+k` / similar means extended JSON with bare identifier values

This design does not force one answer, but v1 implementation should pick and document one explicitly.

### 3. Exact handler signature

The semantic contract matters more than the exact call signature, but it should be standardized early so built-in and user-defined fences behave the same way.

### 4. Canonical collection constructors

The transliterator should lower to whatever literal/builder forms the language treats as canonical for lists/dicts/tuples/etc.

## Rejected / Deferred Ideas

### Full interpolation syntax inside foreign strings

Example:

```json
{ "foo": "{{ (+ 1 2) }}" }
```

Deferred.

This turns the fence into a template language, substantially complicates parsing, escaping, and positional diagnostics, and is not needed for the initial value of the feature.

### Arbitrary expression syntax in value position

Example:

```json
{ "foo": (+ 1 2) }
```

Deferred.

This would embed a second expression grammar into the fence and erase the simplicity of the narrow extension model.

### Bare keys in JSON object position

Deferred/rejected for v1.

This makes the feature drift away from recognizable JSON and introduces avoidable ambiguity.

## Recommended V1 Scope

* add reader support for fenced forms
* add `compile/apply-fence`
* add metadata plumbing from day one, even if sparse initially
* implement one built-in JSON fence
* JSON fence supports strict JSON literals plus bare identifier values only
* object keys remain quoted strings
* no arbitrary expressions
* no interpolation
* no dotted/member syntax

## Example End-to-End Expansion

Source:

````text
``` json
{ "host": host_name, "port": port, "tls": true }
```
````

Reader lowers conceptually to:

```scheme
(compile/apply-fence json
  "{ \"host\": host_name, \"port\": port, \"tls\": true }\n"
  META)
```

`compile/apply-fence` resolves `fence/json` and the handler returns source conceptually like:

```scheme
(#dict "host" host_name
       "port" port
       "tls" true)
```

The compiler then compiles that returned source normally.

## Invariants

* Fence expansion is compile-time, not runtime.
* The reader captures fenced source but does not parse foreign formats.
* `compile/apply-fence` is the dispatch point for fence expansion.
* Fence handlers return source, not opaque runtime results.
* Metadata is optional semantically in v1 but supported by the protocol from the start.
* JSON v1 allows bare identifiers only in value position.
* JSON object keys remain quoted strings in v1.
* No full interpolation or arbitrary embedded expressions in v1.

## Suggested Documentation Placement

This feature likely needs entries in at least:

* `SYNTAX.md` for fenced surface syntax
* `SPECIALS.md` for `compile/apply-fence`
* a new fence reference document, or sections grouped by built-in fence type
* possibly invariants/open-questions notes if strict-vs-extended JSON naming remains unsettled

## Suggested First Implementation Sequence

1. Extend reader to recognize fenced source and lower to `compile/apply-fence`.
2. Add `compile/apply-fence` special and handler resolution path.
3. Define metadata object/shape, even if minimal initially.
4. Implement `fence/json`.
5. Enforce v1 restrictions strictly.
6. Add precise diagnostics with remapped source positions.
7. Only later consider optional metadata-controlled extended modes.
