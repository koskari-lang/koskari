# Opcode-based function annotations

Status: proposal (internal design). Supersedes the code/formals-based
annotation paths described under "Current state" below. May link into
`docs/` once a stable user-facing surface exists.

## Summary

Move function annotations off the compile-time source-form storage
(`FormalsSpec.annotations` plus `KosCode.extras['return']`) and onto a
**runtime annotations dict** assembled at function-creation time by a
distinct opcode. The annotation *expressions* are compiled and evaluated in
the enclosing (defining) scope, exactly like keyword defaults and exactly
like the existing class-body `__annotations__` lowering. The realized
`__annotations__` dict and the `inspect.Signature` are then cached on the
function, separately from the raw creation-time dict, and computed lazily on
first access so that string (forward-reference) annotations can be resolved
against the function's namespace at that point.

## Motivation / problem statement

### Current state

Function annotations today are stored as **raw reader forms** and are only
turned into values, as strings, when `__annotations__` is read.

- Per-parameter annotations are gathered as the raw datum of the `type`
  attribute and stashed verbatim into `FormalsSpec.annotations` as
  `{param_name_str: {"type": <form>}}`. See
  `koskari/compile/helpers.py` (`_gather_one_positional_frame`,
  `gather_formals`) and `koskari/compile/specials/lambda_.py`
  (`helper_function`).
- The return annotation is stored as a form in `KosCode.extras['return']`
  (`helper_function`).
- `koskari/lib/func.c` `function_get_annotations` builds a **fresh dict on
  every access**, coercing each form to a string via
  `kos_typehint_to_dunder_annotation` (symbol/keyword -> interned name;
  anything else -> `PyObject_Str`).
- `koskari/lib/func.c` `function_get_signature` lazily builds and caches an
  `inspect.Signature` in the `inspect_signature` slot, but the parameter
  builders (`kos_append_inspect_parameter`, `kos_append_var_param`)
  **hardcode `Parameter.empty`** for the annotation, so the signature never
  reflects the annotations.

### Concrete problems

1. Annotations are stringly typed. `:: int` becomes the string `"int"`,
   never the `int` object. A compound form such as an attribute access falls
   through to `PyObject_Str` and yields a meaningless string.
2. `inspect.signature(fn)` reports bare parameters even when
   `fn.__annotations__` is populated, because the two are produced by
   disconnected code paths that are never reconciled.
3. `typing.get_type_hints` / `inspect.get_annotations(eval_str=True)` cannot
   resolve the string names, because those paths call
   `eval(value, globalns, localns)` and require `globalns` to be a real
   `dict`; a namespace exposed as `__globals__` does not satisfy that
   constraint.
4. The annotation bundle is not serializable. `koskari/serialize/codex.py`
   has no `dict` branch in `intern_for_pool` or
   `encode_pool_record_value`; interning a `FormalsSpec` with a non-empty
   `annotations` dict would raise `TypeError`. Annotation-bearing functions
   therefore do not round-trip through `.kio` today.

## Goals

- Produce **real annotation objects** (types, imported classes, etc.) rather
  than strings, evaluated in the correct (defining) scope.
- Keep `__annotations__` and `inspect.Signature` **mutually consistent** by
  deriving both from a single realized annotations mapping.
- Preserve a **forward-reference escape hatch** via string annotations,
  resolved lazily and tolerantly (a failed resolution degrades to the string
  and is never retried).
- Reuse existing machinery (`BUILD_KWDS_NAMES`, the keyword-default stack
  discipline, ordinary expression compilation) rather than inventing a new
  resolver.
- Make annotation-bearing functions serializable by construction.

## Non-goals

- Making `KosFunction` pass `inspect.isfunction` or exposing `__code__` as a
  `types.CodeType`. Unchanged from
  `inspect-interop-kos-callables.md`.
- A `__globals__`-as-dict bridge for `typing.get_type_hints`. With real
  objects cached on the function this path is no longer required for the
  common case; string forward-refs are resolved internally instead.
- Full PEP 563 / `from __future__ import annotations` semantics. Non-string
  annotations are evaluated eagerly at definition time (see below).

## Design overview

Introduce a distinct opcode (working name `BUILD_FUNCTION_ANNOTATED`) that
builds a function and attaches a pre-computed annotations dict in one step.
The `lambda`/`function` special form lowers annotation expressions in the
enclosing fragment, assembles the annotations dict with `BUILD_KWDS_NAMES`,
and emits the new opcode instead of `BUILD_FUNCTION` whenever any annotation
is present. Un-annotated functions keep using `BUILD_FUNCTION` so the common
path is unaffected.

This mirrors the class body, which already compiles each annotation
expression and folds them into a real dict with `BUILD_KWDS_NAMES` before
storing `__annotations__` (see `koskari/compile/specials/class_.py`).

### Compiler lowering (in `helper_function`)

When at least one parameter annotation or a return annotation exists, emit
into the **enclosing** fragment (the same scope where keyword defaults are
compiled — this is the defining scope, which is the correct scope for
annotations, matching Python and the class body):

1. For each annotated entry, in a fixed key order, push its annotation
   value:
   - **string-literal form** -> push the string as a `CONST` (do not
     evaluate; this is the forward-reference form).
   - **any other form** (symbol, attribute access, subscription, call) ->
     compile it as an ordinary expression so it evaluates to a real object.
2. `BUILD_KWDS_NAMES <keys_tuple>` where `keys_tuple` is the tuple of
   annotated parameter-name strings followed by `"return"` when a return
   annotation is present. This leaves the creation-time annotations dict on
   the stack.
3. The existing default-value pushes, the defnames `CONST` tuple (when
   there are keyword defaults), and the `FunctionSpec` `CONST`.
4. `BUILD_FUNCTION_ANNOTATED N`, where `N` is the keyword-default count
   (identical meaning to the `BUILD_FUNCTION` argument).

Resulting stack, bottom to top, at the new opcode:

```
[ ann_dict, def_val_1 ... def_val_N, names_tuple, fspec ]
```

The string-vs-expression decision is a single datum-type check on the stored
type form.

### Opcode semantics and stack discipline

`BUILD_FUNCTION_ANNOTATED` is `BUILD_FUNCTION` (see
`koskari/lib/opcode/BUILD_FUNCTION.c`) with one additional pop:

1. Pop `fspec` (TOS).
2. If `N > 0`, pop the `N` default values plus the names tuple into a
   defaults dict via `KosEvalState_StackPopTailDictNames`.
3. Pop `ann_dict` (the dict left by `BUILD_KWDS_NAMES`).
4. Build the function against the current namespace
   (`CURRENT_NS()`), then attach `ann_dict` as the creation-time annotations
   value.
5. Push the function.

Refcount and pop-order correctness (mirroring the defaults handling in
`BUILD_FUNCTION`) is the primary implementation risk and must be covered by
native and full-stack tests. Ownership: the opcode consumes `fspec`,
`defaults`, and `ann_dict`; the attached dict reference is transferred to the
function.

### C-API

Provide a single entry point so the opcode does not have to poke the struct
directly and so non-opcode callers (tests, tools) can build annotated
functions in one call. Proposed shape:

```
PyObject *KosFunction_NewWithAnnotations(
    PyObject *func_spec,
    PyObject *kw_defaults,
    PyObject *parent_namespace,
    PyObject *annotations);   /* dict or NULL; new function owns a ref */
```

Implemented in terms of the existing `KosFunction_New`, then storing the
annotations dict and initializing the resolution flag (below). The opcode
calls this. `KosFunction_New` remains for the un-annotated path.

## KosFunction storage

Three additions to the struct (`koskari/lib/func.h`), wired into
`function_init` / `function_traverse` / `function_clear` /
`function_dealloc` and the `NewFromFormals` / `New` constructors:

- `creation_annotations` — the raw dict handed in at build time. Cheap to
  store; may contain a mix of real objects and strings. This is the
  authoritative input, retained so the realized caches can be (re)built from
  it.
- `annotations_cache` — the realized `__annotations__` dict, or NULL until
  first access.
- `inspect_signature` — already present; continues to hold the realized
  `inspect.Signature`, or NULL until first access.

Plus a small flag:

- `annotations_need_resolve` — set at construction if any value in
  `creation_annotations` is a `str` (a forward reference that may need dotted
  resolution). When clear, the realized `__annotations__` is just the
  creation dict (optionally shared directly) and no per-string work is done.

### Lazy realization

`__annotations__` and `__signature__` are cached **separately** from the
creation-time dict and from each other, and are realized on first access:

- On first read of `__annotations__` (`function_get_annotations`):
  - If `annotations_need_resolve` is clear, publish the creation dict as the
    cache (a copy, so callers cannot mutate the creation input) and return
    it.
  - Otherwise, build the cache by copying the creation dict and replacing
    each `str` value with its resolved object where resolution succeeds;
    leave the string in place where it fails. Store the result in
    `annotations_cache`. Failures are cached as strings and **never retried**
    — once a name fails to resolve it stays a string for the lifetime of the
    function.
- On first read of `__signature__` (`function_get_signature`):
  - Ensure the realized `__annotations__` exists (reusing the same resolution
    result), then build the `inspect.Signature`, mapping each parameter's
    annotation from the realized dict and the `"return"` entry to
    `Signature.return_annotation`. Cache in `inspect_signature`.

Because both members draw from the same realized annotations mapping, they
stay consistent. The design doc requirement in
`inspect-interop-kos-callables.md` ("`__annotations__` and `__signature__`
stay consistent once both exist") is satisfied structurally.

Caching is a first-access snapshot: a name unresolved at first access and
degraded to a string will not upgrade later even if it becomes bound. This is
an accepted trade-off in exchange for a stable, no-retry cache.

### String (forward-reference) resolution

For each string annotation value that needs resolving:

1. Split the string on `.`.
2. Look up the first segment in the function's `base_ns`. Namespace `get`
   (`koskari/lib/namespace.c` `ns_m_get`) already walks the parent chain to
   the defining module namespace, and the module namespace falls back to
   builtins, so ordinary imported names and builtins both resolve.
3. `getattr`-chain the remaining segments.
4. On any `NameError` / `AttributeError` (or a lookup miss), keep the
   original string and mark that entry resolved-as-string.

Note the `base_ns` edge case from the interop notes: `base_ns` is the
*template* namespace and carries declared-but-unbound local slots for the
formals and code locals. A first segment that collides with a parameter or
local name could hit an unbound slot instead of chaining upward. Mitigation:
resolve the first segment against the template's parent (the defining
namespace) for annotation lookups, or skip unbound local slots and continue
up the chain. Type names colliding with parameter names is pathological, but
the unbound-slot-shadows-parent path is real and must be handled.

## Semantics: eager vs. forward reference

- **Non-string forms are evaluated eagerly at definition time.** A bare
  symbol annotation (`:: int`, `:: Request`) compiles to a normal expression;
  an unbound name raises `NameError` when the function is created. This
  matches CPython without `from __future__ import annotations`, and it
  matches the existing class-body behavior.
- **String forms are the forward-reference escape hatch.** Writing
  `:: "Request"` (or `:: "pkg.Request"`) defers resolution to first
  introspection and tolerates failure by remaining a string. This is the
  intended way to annotate names that are not yet bound at definition time.

## Superseding the code/formals-based paths

With the runtime dict as the source of truth, the following become redundant
and can be removed:

- `KosCode.extras['return']` as the return-annotation carrier — the return
  type is now the `"return"` entry in the annotations dict.
- The stringifying read path in `function_get_annotations` that walks
  `FormalsSpec`/`Formals` `type` bundles, and `kos_typehint_to_dunder_annotation`.
- Population of `FormalsSpec.annotations` for the purpose of `__annotations__`
  / `__signature__`. If no other consumer relies on the `type` bundles
  (e.g. runtime contract/demand checks), the bundle plumbing in
  `gather_formals` / `helper_function` / `FormalsSpec` can be dropped as
  well. This must be verified before removal; if a consumer remains, the
  bundles stay but are no longer the annotation source of truth.

Removing the `FormalsSpec.annotations` dependency also **resolves the codex
serialization gap**: annotations become ordinary compiled expressions plus a
`CONST` keys tuple (all already serializable), and the un-encodable
`dict`-of-forms disappears from `FormalsSpec`. No new pool encoder is
required for the annotation feature.

## Interaction with class-based annotations

Class bodies already implement precisely this model. In
`koskari/compile/specials/class_.py`, when a class body has annotation
bindings, each `type_expr` is compiled as an expression and folded into a
real dict via `BUILD_KWDS_NAMES`, then stored as the class `__annotations__`:

```
for name, type_expr in collapsed.items():
    annos.merge(compiler.full_compile(type_expr, False))
annos.instruction(OpCode.KOS_OP_BUILD_KWDS_NAMES, tuple(collapsed.keys()))
annos.instruction(OpCode.KOS_OP_VAR_STORE, "__annotations__")
```

Implications:

- The function design makes functions **symmetric with classes**: both
  compile annotation expressions in their defining scope and build a real
  dict with `BUILD_KWDS_NAMES`. The opcode is the function-side analogue of
  the class body's explicit `__annotations__` store.
- Class annotations are already **eager**: an undefined bare name in a class
  annotation raises at class-definition time today. The function design
  adopts the same rule, so behavior is uniform across classes and functions.
- Class annotations currently have **no string forward-reference /
  lazy-resolution layer** and no per-`__annotations__` caching machinery
  (the class simply owns a normal dict attribute). The lazy re-resolution
  and separate caching described here are function-object features. If we
  want parity, the same string-resolution helper could later be applied when
  reading a class `__annotations__`, but that is out of scope for this
  proposal and should not block it.
- Methods defined in a class body are ordinary functions and therefore get
  the function-side treatment (opcode + lazy resolution). A class thus has
  two independent annotation surfaces: the class's own `__annotations__`
  (from the class body) and each method's `__annotations__` (from the new
  opcode). These are already separate in CPython and remain separate here.

## Serialization

- Annotation expressions compile to normal bytecode; the keys tuple is a
  normal `CONST`. Both already serialize through `codex.py`.
- The creation-time dict is produced at runtime by the opcode, not stored in
  a `FunctionSpec`/`FormalsSpec`, so it does not need a pool encoder.
- Once `FormalsSpec.annotations` is no longer used for this feature, the
  latent `TypeError` on interning a non-empty annotations dict is avoided by
  construction.

## Test and migration impact

- New native/full-stack tests for the opcode stack discipline and refcounts.
- New tests asserting real objects: `fn.__annotations__['x'] is int`,
  `inspect.signature(fn).parameters['x'].annotation is int`, and
  `return_annotation`.
- New tests for string forward references: resolution success, dotted
  resolution, and the failure-stays-string-and-is-not-retried guarantee.
- `tests/lib/test_inspect_kos_function.py` currently asserts the stringified
  behavior (`{'a': 'int'}`, `{'return': 'int'}`) and constructs functions via
  the `FormalsSpec` annotation bundles. Those constructions and expectations
  change with the source-of-truth move; they must be rewritten against the
  new build path rather than adjusted to keep passing.
- Bound methods: `boundmethod_get_signature` rebuilds a signature from the
  remaining parameters via `kos_signature_drop_first`, which drops
  `return_annotation`. Parameter annotations survive (they live on the
  `Parameter` objects); if bound methods must retain the return annotation,
  `kos_signature_drop_first` needs to carry it across the rebuild.

## Open questions

- Final opcode name (`BUILD_FUNCTION_ANNOTATED` vs. a shorter form) and
  whether it warrants its own doc page under
  `docs/reference/vm/opcodes/`.
- Whether any current consumer needs `FormalsSpec.annotations` `type`
  bundles beyond `__annotations__` (contracts/demand), which determines how
  much of the old plumbing can be deleted.
- Whether class `__annotations__` should later share the same string
  forward-reference resolution helper for parity.
- Namespace target for first-segment resolution (template vs. its parent) and
  the exact unbound-local-slot skip rule.
