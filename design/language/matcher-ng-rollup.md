# Koskari Unified Matcher Rollup (matcher_ng)

## Status

Design proposal. Stand-alone next-generation matcher (`matcher_ng`) intended
as a full replacement for the existing `koskari/matcher.py`. Implementation
lands in `koskari/lib/matcher.c` and `koskari/lib/matcher.h`, with Python
interactions driven from `koskari/matcher_ng.py`. The current `matcher.py`
remains untouched until validation of `matcher_ng` is complete; only after a
clean migration will the original matcher be removed. Migration and removal
are out of scope for the work described here.

Complements the current architecture reference at
[unified-matcher-transformer-model.md](unified-matcher-transformer-model.md),
which documents the implementation `matcher_ng` is intended to supersede.

## Purpose

Koskari needs one matcher model that can serve several related uses:

1. **Syntax matching** for macros, syntax transformers, special-form dispatch, and source-form diagnostics.
2. **Runtime matching** for value matching, structured unpacking, and `match/case`-style branching.
3. **Hygiene extraction** for syntax expansion, where matched declarations, uses, and bodies define gensym substitution scopes.
4. **Composite matcher authoring** so reusable matchers such as `FORMALS`, `BINDING`, and `BINDINGS` can be written once and compiled into the same low-level matcher form.

The core idea is that user-facing matcher syntax, Python builder objects, frozen matcher constants, and thawed native matcher instances are different layers over the same underlying rule program.

---

## Layer Model

The full flow is:

```text
surface matcher syntax
  -> Python/object symbolic matcher layer
  -> canonical low-level tagged-tuple representation
  -> frozen code constant on disk / in bytecode
  -> thawed native Matcher instance
  -> runtime invocation with subject + extern bindings
  -> MatchResult
```

### 1. Surface matcher syntax

This is what users or compiler authors write in Koskari source. It reuses ordinary Koskari forms: symbols, quoted symbols, keywords, lists, and annotation syntax.

Examples:

```scheme
x
(x :: symbol)
(x val: 5)
(type: keyword)
(val: 5)
'x

(FORM (name :: symbol) ':: return_type (formals :: (FORMALS scope: body)) (body :: REST))

(data :: (CLASS Point x: 0 y))
```

### 2. Python/object symbolic matcher layer

This is the ergonomic implementation API used by compiler code and bootstrap definitions.

Examples:

```python
FORM(
    CAPTURE("name", type=symbol),
    LITERAL_KW("::"),
    CAPTURE("return_type"),
    FORMALS("formals", scope="body"),
    REST("body"),
)
```

Existing APIs such as `SYMBOL("name")`, `VALUE("expr")`, `REST("body")`, `BINDING(...)`, and `BINDINGS(...)` can remain as Pythonic constructors. They lower into the same symbolic matcher AST as the source syntax.

### 3. Canonical low-level tagged-tuple representation

This is the frozen form suitable for embedding into bytecode constants. It should be versioned, canonical, and compact. It should not contain Python builder objects.

Conceptually:

```text
("MATCHER",
  version,
  slots,
  externs,
  consts,
  nodes,
  entry,
  meta)
```

Where:

* `slots` names capture slots.
* `externs` names externally supplied values such as classes, types, predicates, or symbolic comparison values.
* `consts` stores embedded literal constants or sub-matcher constants.
* `nodes` is a bottom-up list of tagged nodes.
* `entry` is the root node index.
* `meta` stores provenance and hygiene metadata.

### 4. Frozen code constant

The compiler emits bytecode/code constants containing the frozen matcher representation. Named composites are resolved and lowered before this point unless intentionally represented as externs.

A previously compiled function is not affected if a named matcher such as `FORMALS` is redefined later. The named matcher acted like a compile-time matcher macro that expanded to lower-level rules.

### 5. Thawed native Matcher instance

When code is realized into a callable function object, complex constants thaw. The frozen matcher tuple becomes a native `Matcher` instance containing compact C-native arrays and validation-derived metadata.

The runtime function refers to that matcher as a normal constant.

### 6. Invocation

A matcher invocation takes the subject plus extern bindings:

```python
m.match(obj, Point=Point, int=int, sentinel=sentinel)
```

Externs are explicit matcher parameters. The matcher may use them for:

* class matching: `(CLASS Point ...)`
* type checks: `(type: Point)` or `(x :: Point)`
* predicate checks
* symbolic value comparisons

The native matcher validates extern bindings at call entry, then matching uses indexed extern slots. Matching itself performs no namespace lookup.

---

## Syntax Proposal

The matcher syntax is a pattern language interpreted in matcher context. It does not require new reader syntax.

### Core pattern forms

```bnf
pattern ::= literal
          | capture
          | anonymous_constraint
          | composite
          | class_pattern
```

### Literals

```bnf
literal ::= number
          | string
          | keyword
          | quoted_symbol
```

Examples:

```scheme
5
"hello"
foo:
'x
```

A quoted symbol is a literal symbol. A bare symbol is a capture.

### Captures

```bnf
capture ::= symbol
          | "(" symbol capture_option* constraint* ")"
```

Examples:

```scheme
x
(x :: symbol)
(x type: int)
(x val: 5)
(x :: (FORM (type: keyword) (type: symbol)))
```

A bare symbol captures any value/form.

### Anonymous constraints

```bnf
anonymous_constraint ::= "(" constraint+ ")"
```

Examples:

```scheme
(type: symbol)
(:: int)
(val: 5)
(INT)
```

### Constraints

```bnf
constraint ::= "::" matcher_expr
             | "type:" matcher_expr
             | "val:" value_expr
             | matcher_expr
```

`::` and `type:` are equivalent as type/predicate/matcher constraints. `val:` is literal/value equality.

### Capture options

Capture options are compile-time matcher options, not matched input.

```bnf
capture_option ::= "scope:" symbol
                 | "mode:" symbol
                 | "role:" symbol
```

Examples:

```scheme
(formals :: (FORMALS scope: body))
(bindings :: (BINDINGS mode: parallel scope: body))
(body :: REST)
```

`scope: body` refers to another capture slot named `body`, not to an input form.

### Composite matchers

```bnf
composite ::= "(" matcher_head pattern* ")"
```

Core matcher heads:

```scheme
FORM
GROUP
MATCH
ONEOF
SEQ
REPEAT
OPTIONAL
REST
NOT
CLASS
```

`MATCH` and `GROUP` are important for grouping multiple rules along the same current input spine. This differs from a nested `FORM`, which descends into a nested form value.

---

## Structural Matchers

### FORM

```scheme
(FORM p1 p2 ...)
```

Matches a nested source/value sequence with the given branch pattern. In compile-time syntax matching, this means a KosPair/proper list form. In runtime matching, this means a sequence-like value according to the runtime subject adapter.

### GROUP / MATCH

```scheme
(GROUP p1 p2 ...)
(MATCH p1 p2 ...)
```

Groups several pattern elements on the current spine without descending into a nested form. This is needed inside `ONEOF` and similar places where each branch may contain multiple sequential elements.

Example concept:

```scheme
(FORM
  a
  (ONEOF
    (GROUP b)
    (GROUP c d)
    (GROUP z (rest :: REST))))
```

This matches forms like:

```scheme
(a b)
(a c d)
(a z ...)
```

The `ONEOF` branches operate from the same originating cursor after `a`.

### ONEOF

```scheme
(ONEOF branch1 branch2 ...)
```

Tries alternatives in order. Each branch may be a single pattern node or a `GROUP` containing multiple nodes on the same spine.

Each `ONEOF` branch is independent for width analysis and REST allowance.

### SEQ

```scheme
(SEQ p)
```

Shorthand for a form containing repeated matches of `p`:

```scheme
(FORM (REPEAT p))
```

Useful for list-of patterns such as `BINDINGS`.

### REPEAT

```scheme
(REPEAT p1 p2 ...)
```

Consumes the current spine repeatedly. Each iteration matches the repeated branch and advances the cursor. Captures inside a named repeat produce a list of per-iteration sub-captures.

Invariant:

```text
REPEAT operand min_width must be >= 1.
```

This forbids zero-width loops such as:

```scheme
(REPEAT (OPTIONAL p))
```

which should be expressed as `REST` if arbitrary suffix capture is intended.

### OPTIONAL

```scheme
(OPTIONAL p)
```

Greedy maybe-match. It succeeds whether `p` matches or not. If absent, captures inside it receive the defined absent/default sentinel behavior.

### REST

```scheme
(REST)
(rest :: REST)
```

Consumes the remaining tail with suffix reservation when nonterminal.

Terminal REST captures the remaining tail as-is.

Nonterminal REST reserves enough suffix width for following patterns. For example:

```scheme
(FORM a (r :: REST) w x)
```

captures into `r` while preserving enough elements for `w x` to match.

Within a single `FORM` branch, only one REST is allowed. Each `ONEOF` branch is its own branch for this constraint.

For width computation, REST is treated specially as the unique variable-tail element for the branch. Operationally it is min zero and unbounded, but branch validation enforces only one variable REST per `FORM` branch.

### NOT

```scheme
(NOT p)
```

Negative check. It must not consume input. It succeeds if `p` does not match at the same cursor.

### CLASS

```scheme
(CLASS Point x: 0 y)
(data :: (CLASS Point x: 0 y))
```

Matches a Python-style class pattern. It checks the subject against an externally supplied class/type and then extracts fields via `__match_args__`, attributes, or keyword field rules.

Explicit `CLASS` avoids ambiguity with named composite matchers and ordinary type constraints.

Optional sugar may allow:

```scheme
(data :: (Point x: 0 y))
```

but only if resolution rules make it unambiguous. The safer canonical form is explicit `CLASS`.

---

## Hygiene Integration

The matcher result can carry both captures and hygiene metadata. Hygiene metadata is inert unless a caller asks for it, for example through `extract_targets`.

Current concepts map into the new model as capture flags and metadata:

* `DECL` / introduced name: capture with `hygiene_role=decl`.
* `USING`: capture with `hygiene_role=use`.
* `BINDING`: composite matcher that identifies a declared name and expression.
* `BINDINGS`: repeated `BINDING` composite.
* `BODY`: capture or rest region marked as a body/scope target.
* `scope:`: link from declaration capture to body capture.

The low-level matcher does not need separate primitive kinds for every hygiene-aware matcher. Most of them lower into capture/test nodes with flags and graph metadata.

Example:

```scheme
(formals :: (FORMALS scope: body))
(body :: REST)
```

The match captures `formals` and `body`; the matcher metadata records that declarations produced by `FORMALS` apply over the body capture slot.

---

## Current Matcher Examples Rewritten

### Lambda

Current Python-style patterns:

```python
MATCH(SYMBOL("name"), LITERAL_KW("::"), VALUE("return_type"), FORMALS("formals", scope="body"), REST("body"))
MATCH(SYMBOL("name"), LITERAL_KW("type:"), VALUE("return_type"), FORMALS("formals", scope="body"), REST("body"))
MATCH(LITERAL_KW("::"), VALUE("return_type"), FORMALS("formals", scope="body"), REST("body"))
MATCH(LITERAL_KW("type:"), VALUE("return_type"), FORMALS("formals", scope="body"), REST("body"))
MATCH(SYMBOL("name"), FORMALS("formals", scope="body"), REST("body"))
MATCH(FORMALS("formals", scope="body"), REST("body"))
```

New matcher syntax:

```scheme
(ONEOF
  (GROUP (name :: symbol) ':: return_type (formals :: (FORMALS scope: body)) (body :: REST))
  (GROUP (name :: symbol) type: return_type (formals :: (FORMALS scope: body)) (body :: REST))
  (GROUP ':: return_type (formals :: (FORMALS scope: body)) (body :: REST))
  (GROUP type: return_type (formals :: (FORMALS scope: body)) (body :: REST))
  (GROUP (name :: symbol) (formals :: (FORMALS scope: body)) (body :: REST))
  (GROUP (formals :: (FORMALS scope: body)) (body :: REST)))
```

When this is the whole form rule for `compile/lambda`, it may be wrapped in `FORM` depending on whether the matcher is invoked on the interior of the form or the whole source form.

Important: `name` must be `(name :: symbol)`. Bare `name` would allow any form.

### Let

Current Python-style patterns:

```python
MATCH(SYMBOL("name"), BINDINGS("bindings", mode="parallel", scope="body"), REST("body"))
MATCH(BINDINGS("bindings", mode="parallel", scope="body"), REST("body"))
```

New matcher syntax:

```scheme
(ONEOF
  (GROUP (name :: symbol) (bindings :: (BINDINGS mode: parallel scope: body)) (body :: REST))
  (GROUP (bindings :: (BINDINGS mode: parallel scope: body)) (body :: REST)))
```

### Letrec

```scheme
(GROUP (bindings :: (BINDINGS mode: parallel scope: body)) (body :: REST))
```

### Binding

Current `BINDING`:

```python
_BINDING_FORMS = [
    LIST(VALUE("expr"), LITERAL_KW("as"), SYMBOL("name")),
    LIST(SYMBOL("name"), VALUE("expr")),
]
```

New composite matcher:

```scheme
(define-matcher BINDING
  (ONEOF
    (FORM expr 'as (name :: symbol))
    (FORM (name :: symbol) expr)))
```

`BINDINGS` is a sequence of repeated `BINDING` records:

```scheme
(define-matcher BINDINGS
  (SEQ (binding :: BINDING)))
```

Or with an outer capture at use site:

```scheme
(bindings :: BINDINGS)
```

### Formals Sketch

```scheme
(define-matcher FORMALS
  (FORM
    (pos :: (REPEAT (FORM (val :: (NOT (type: keyword))))))
    (kwds :: (REPEAT (FORM (key :: keyword) (val :: (NOT (type: keyword))))))))
```

The intended result shape:

```text
{
  pos:  [{val: ...}, ...],
  kwds: [{key: foo:, val: ...}, {key: bar:, val: ...}]
}
```

Named `REPEAT` captures produce lists of per-iteration records. This is the main case where nested capture result shaping matters.

---

## Runtime Matching vs Syntax Matching

The rules are shared, but subject adapters differ.

### Runtime matching

Runtime matching operates over Python values. Sequence matching should use a `PySequence_Fast`-style adapter for tuple/list coercion, not KosPair traversal.

Use cases:

* `match/case` over runtime values
* structured unpacking
* runtime class matching
* runtime value/type/predicate constraints

Runtime matchers receive the object and extern bindings:

```python
matcher.match(obj, Point=Point, symbol=Symbol)
```

The result can then be applied to a namespace as a separate operation.

### Syntax matching

Syntax matching operates over source forms, typically KosPair cons lists. It should use a KosPair subject adapter.

Use cases:

* macros
* syntax transformers
* special-form dispatch
* source-form diagnostics
* hygiene extraction

Syntax matching can request provenance and hygiene data, but the core matcher API still simply determines match/no-match.

### Shared implementation through subject adapters

There should be one native matcher implementation with different subject adapters:

```c
typedef struct {
    Py_ssize_t (*len)(KosMatchSubject *subject);
    PyObject *(*get)(KosMatchSubject *subject, Py_ssize_t index);
    PyObject *(*tail)(KosMatchSubject *subject, Py_ssize_t start);
    int (*as_form)(PyObject *obj, KosMatchSubject *out);
} KosMatchSubjectVTable;
```

Runtime sequence adapter:

* Uses `PySequence_Fast`-style conversion.
* Provides O(1) length and index.
* Tail materialization returns the appropriate runtime sequence or view.

Syntax/KosPair adapter:

* Walks the cons spine.
* May cache length/index cells per invocation.
* Preserves source-form identity for diagnostics and hygiene.
* Handles proper/improper list policy explicitly.

The matcher nodes do not care which adapter is used.

---

## Match Results

The public match result is binary at the lowest level:

```text
matched / not matched
```

A non-matching value is not an error.

Exceptions are reserved for invalid matcher state, invalid invocation environment, failed validation, missing externs, or internal bugs.

On match, the result contains:

* capture slots
* nested capture records
* repeated capture lists
* optional absent/default sentinels
* optional provenance data
* optional hygiene graph data

Applying captures to a namespace is a later, separate operation. This allows matcher inspection, dispatch, unpacking, and hygiene extraction to share the same result object.

---

## Width Model

Every node computes at least:

```text
min_width
max_width
```

`min_width == 0` covers the `can_empty` concern.

Width is measured in elements consumed from the current spine.

Important cases:

* Literal/test/capture of one element: min=1, max=1.
* `GROUP`: sum of child widths.
* `FORM`: consumes one element from the outer spine, then matches an inner subject.
* `ONEOF`: min is min(branch.min); max is max(branch.max), with unbounded if any branch is unbounded.
* `OPTIONAL`: min=0, max=child.max.
* `REPEAT`: min=0, max=unbounded, but child min must be >= 1.
* `REST`: unique branch-level variable tail; min=0 and operationally unbounded, with suffix reservation.
* `NOT`: min=0, max=0; must not consume.

Branch validation:

* No recursive matcher definitions.
* No more than one REST per `FORM` branch.
* Each `ONEOF` branch is its own branch for REST allowance.
* `REPEAT` operand must consume at least one element.
* Subnode indices must be acyclic.

---

## Native C Representation

The native matcher should be an array of tagged enum structures. Each node has a kind and a union payload. Sub-checks are indices into the same array.

To prevent cycles, validation should require referenced child indices to be lower than the current node index. Nodes are emitted bottom-up.

Sketch:

```c
typedef enum {
    KOS_MATCH_TEST,
    KOS_MATCH_CAPTURE,
    KOS_MATCH_FORM,
    KOS_MATCH_GROUP,
    KOS_MATCH_ONEOF,
    KOS_MATCH_REPEAT,
    KOS_MATCH_OPTIONAL,
    KOS_MATCH_REST,
    KOS_MATCH_NOT,
    KOS_MATCH_CLASS,
    KOS_MATCH_FIELD,
} KosMatchKind;

typedef struct {
    uint16_t kind;
    uint16_t flags;
    uint32_t min_width;
    uint32_t max_width;
    union {
        KosMatchTest test;
        KosMatchCapture capture;
        KosMatchForm form;
        KosMatchGroup group;
        KosMatchOneof oneof;
        KosMatchRepeat repeat;
        KosMatchOptional optional;
        KosMatchRest rest;
        KosMatchNot not_;
        KosMatchClass class_;
        KosMatchField field;
    } as;
} KosMatchNode;
```

Possible payloads:

```c
typedef struct {
    uint16_t test_kind;
    uint16_t ref_kind;
    uint32_t ref_index;
} KosMatchTest;

typedef struct {
    uint32_t slot;
    uint32_t subnode;
    uint32_t hygiene_role;
    uint32_t scope_slot;
} KosMatchCapture;

typedef struct {
    uint32_t first_child;
    uint32_t child_count;
} KosMatchGroup;

typedef struct {
    uint32_t child;
} KosMatchRepeat;

typedef struct {
    uint32_t slot;
} KosMatchRest;

typedef struct {
    uint32_t extern_index;
    uint32_t first_field;
    uint32_t field_count;
} KosMatchClass;
```

Child groups may be represented either as contiguous ranges in a child-index array or as contiguous node ranges where that is practical. The key is that behavior is driven by node kind, while type/literal/hygiene details are fields and flags.

### Primitive node policy

Use distinct node kinds only when cursor behavior differs.

Likely primitive node kinds:

* `TEST`
* `CAPTURE`
* `FORM`
* `GROUP` / `MATCH`
* `ONEOF`
* `REST`
* `REPEAT`
* `OPTIONAL`
* `NOT`
* `CLASS`

Do not make every surface matcher a native primitive. Examples:

* `SYMBOL(name)` lowers to `CAPTURE slot=name` with `TEST_TYPE symbol`.
* `DECL(name)` lowers to `CAPTURE slot=name`, `TEST_TYPE symbol`, hygiene role `decl`.
* `VALUE(name)` lowers to unconstrained capture.
* `LITERAL 5` lowers to `TEST_LITERAL 5`.
* `(x val: 5)` lowers to `CAPTURE slot=x` wrapping or carrying `TEST_LITERAL 5`.
* `BINDING` lowers to a composite `ONEOF` over `FORM` branches.
* `BINDINGS` lowers to repeated `BINDING` records.

---

## Tagged Tuple Frozen Representation

Example frozen matcher for a lambda branch:

Source rule:

```scheme
(GROUP (name :: symbol) ':: return_type (formals :: (FORMALS scope: body)) (body :: REST))
```

Conceptual frozen tuple:

```text
("MATCHER", 1,
  ;; slots
  ["name", "return_type", "formals", "body"],

  ;; externs
  [],

  ;; consts
  [FORMALS_MATCHER, "::", "symbol"],

  ;; nodes, bottom-up
  [
    ("TEST_TYPE", builtin:symbol),                    ;; 0
    ("CAPTURE", slot:name, sub:0),                    ;; 1
    ("TEST_LITERAL", const:"::"),                    ;; 2
    ("CAPTURE", slot:return_type, sub:any),           ;; 3
    ("CALL_MATCHER", const:FORMALS_MATCHER),          ;; 4, if sub-matcher call is represented explicitly
    ("CAPTURE", slot:formals, sub:4, scope:body),     ;; 5
    ("REST", slot:body, flags:body_region),           ;; 6
    ("GROUP", children:[1,2,3,5,6])                   ;; 7
  ],

  ;; entry
  7,

  ;; meta
  { hygiene: [("DECL_SET", "formals", "body")] })
```

A `CALL_MATCHER` node may be its own primitive or may be represented as a `TEST`/`FORM` family node depending on implementation. If named composites are fully inlined before freeze, no `CALL_MATCHER` is needed in the lowest form.

Class example:

```scheme
(data :: (CLASS Point x: 0 y))
```

Conceptual frozen tuple:

```text
("MATCHER", 1,
  ["data", "y"],
  ["Point"],
  ["x", "y", 0],
  [
    ("TEST_LITERAL", const:0),
    ("FIELD", name:"x", sub:0),
    ("CAPTURE", slot:y, sub:any),
    ("FIELD", name:"y", sub:2),
    ("CLASS", extern:Point, fields:[1,3]),
    ("CAPTURE", slot:data, sub:4)
  ],
  5,
  {})
```

Invocation:

```python
matcher.match(obj, Point=Point)
```

---

## Extern Binding ABI

Extern bindings are accepted through the ordinary Python call API initially:

```python
matcher.match(obj, Point=Point, int=int, sentinel=sentinel)
```

Later this can be optimized with vectorcall or prebuilt extern environments.

At call entry:

1. Validate that all required externs are present.
2. Validate kind/signature where required.
3. Populate a compact extern slot frame.
4. Run the matcher using extern indices.

This avoids module-relative thaw complexity while still making class/type/value symbolic references explicit and available to low-level matcher execution.

---

## Dynchain / Runtime Match Case Strategy

Runtime `match/case` should avoid repeatedly converting the same subject for each case.

Likely strategy:

1. Evaluate the match subject once.
2. Push or establish a match-case dynnode/state object.
3. Convert/adapt the subject once for the relevant matching mode.
4. Walk case matchers in order.
5. On match, apply captures to a namespace as a separate operation.
6. Jump to the selected handler body.

The dynnode/state object can hold:

* original subject
* adapted subject
* current case index
* capture/result frame
* cleanup/unwind information for applied namespace bindings

Syntax matching for special forms may use a similar internal loop, but over KosPair source forms and without runtime dynchain concerns unless needed by compilation flow.

---

## Error and Failure Policy

Matcher failure is normal:

```text
not matched
```

A subject value that does not satisfy a pattern is never an error.

Exceptions are reserved for:

* invalid matcher structure
* invalid frozen representation
* missing extern binding
* invalid extern binding kind
* internal matcher bug
* validation failure that should have been caught earlier

Creation/thaw validation should be strict so runtime matching does not discover structural matcher errors.

For diagnostics, callers may request provenance or best-failure tracing, but this is optional. The core matching API can simply say no-match.

---

## Implementation Strategy: Low to High

### Phase 1 — Native low-level matcher core

Implement the C-native tagged node array and validator.

Initial primitive nodes:

* `TEST`
* `CAPTURE`
* `GROUP`
* `FORM`
* `ONEOF`
* `REST`
* `REPEAT`
* `OPTIONAL`
* `NOT`
* possibly `CLASS`

Implement:

* bottom-up node validation
* child index acyclicity
* width computation
* one REST per FORM branch
* REPEAT min-width validation
* capture slot validation
* extern table validation

### Phase 2 — Subject adapters

Implement adapters for:

1. Runtime `PySequence_Fast` matching.
2. Compile-time `KosPair` source-form matching.

Keep matcher execution independent of adapter type.

### Phase 3 — Frozen tuple schema and thaw

Define the tagged tuple schema.

Implement:

* tuple parser
* schema version check
* node construction
* validation
* native `Matcher` instance creation
* constant thaw integration

### Phase 4 — Python builder API

Implement ergonomic Python constructors:

```python
FORM(...)
GROUP(...)
ONEOF(...)
CAPTURE(...)
SYMBOL(...)
VALUE(...)
REST(...)
BINDING(...)
BINDINGS(...)
FORMALS(...)
```

These produce symbolic matcher objects and lower to frozen tuples.

### Phase 5 — Bootstrap composites

Define composites like `BINDING`, `BINDINGS`, and `FORMALS` using the Python builder API.

Use these to replace current ad hoc matcher objects while preserving existing behavior.

### Phase 6 — Matcher syntax parser

Implement a matcher-syntax parser that uses bootstrap matchers to unpack matcher syntax itself.

This enables source-level matcher authoring:

```scheme
(define-matcher BINDING
  (ONEOF
    (FORM expr 'as (name :: symbol))
    (FORM (name :: symbol) expr)))
```

The matcher language can then increasingly describe itself.

### Phase 7 — Special-form dispatch integration

Move special forms from manually running Python matcher APIs to compiled matcher groups.

A special form can have a precompiled matcher constant plus associated handler functions. Dispatch becomes:

1. Match source form or form tail.
2. Receive capture result.
3. Optionally extract hygiene targets.
4. Invoke matching handler with captures.

### Phase 8 — Runtime match/case

Add runtime `match` syntax using the same matcher constants.

Runtime flow:

1. Evaluate subject.
2. Adapt subject once.
3. Run case matchers in order.
4. Apply captures to namespace.
5. Execute selected body.

### Phase 9 — Hygiene extraction migration

Reimplement `extract_targets` over the new `MatchResult` metadata.

Ensure old behavior maps to new metadata for:

* lambda formals over body
* let bindings over body
* letrec bindings over body
* with/except/using-style forms
* macro expansion hygiene substitution

---

## Remaining Design Decisions

1. Exact absent/default sentinel for failed `OPTIONAL` captures.
2. Exact duplicate capture-name behavior outside named repeats.
3. Whether named composites are always inlined before freeze or whether a low-level `CALL_MATCHER` node exists.
4. Exact improper-list policy for KosPair syntax matching.
5. Exact field extraction semantics for `CLASS`.
6. Whether shorthand `(Point x: 0 y)` is accepted or only canonical `(CLASS Point x: 0 y)`.
7. Whether source diagnostics use best-failure tracing inside the matcher or an external retry/tracing mode.
8. Whether `GROUP` and `MATCH` are aliases or have distinct meaning.

---

## Working Summary

The matcher system should be a compiled, frozen, native rule program with ergonomic syntax and builder layers above it. The same rules can match runtime values and syntax forms, but through different subject adapters. Hygiene is metadata produced by matching, not a separate matching engine. Named matchers are compile-time conveniences that lower into primitive rules. Runtime symbolic dependencies such as classes and types are provided explicitly as extern bindings at match invocation. The low-level matcher should remain small, array-based, acyclic, and strict about validation so matching is fast and failures are ordinary no-match results.
