# By-ref formals

## Status

**Implemented and archived (2026-06-28).** By-ref formals are conservative over
the existing ref system: every surface form below is the call-site `Ref` we can
already pass, plus an aliasing rebind during application. No new evaluation
model — just a declared *boundary* that tells the binder a slot aliases the
caller's storage.

Implementation: a `by_refs` tuple on `FormalsSpec` / `Formals`, parse-side
marking in [helpers.py](../../koskari/compile/helpers.py) /
[matcher.py](../../koskari/matcher.py), and bind-time aliasing in the four apply
variants of [formals.c](../../koskari/lib/formals.c). Tests:
[tests/importer/test_byref_formals.py](../../tests/importer/test_byref_formals.py)
and [tests/lib/test_formals.py](../../tests/lib/test_formals.py). Example:
[examples/by-ref.ki](../../examples/by-ref.ki). Shipped via the `formals-by-ref`
branch (PR #75).

(Destructuring formals — nested patterns in a formals list — were rejected
2026-06-27 and are out of scope. Structured unpacking stays in `define-values`
/ `setq-values`.)

## Goal

A formal may be marked *by-ref*. Instead of minting a fresh slot and copying the
argument value in, application installs a **caller-supplied `Ref` as the slot's
storage**. The body sees a plain-valued local that happens to share storage with
the caller's binding, so `set!` in the callee writes through to the caller.

```koskari
define add-a [a &result]:
    (set! result (+ a 1))

define value 100
(add-a value &value)
; value is now 101
```

By-ref is permitted on **positional and keyword** formals only, never on `*` /
`**`.

## How this rests on machinery that already exists

The pieces are all in the tree today; by-ref formals wire them together.

- **Call site.** `&value` is reader sugar: [parser.py:1316](../../koskari/reader/parser.py:1316)
  rewrites the `&`-atom to `(compile/ref value)`. The `compile/ref` macro
  ([_variables.ki:174](../../koskari/bootstrap/_variables.ki:174)) lowers the
  bare-symbol case to `(compile/refq value)`, which compiles to `VAR_REF`
  ([opcode/VAR_REF.c](../../koskari/lib/opcode/VAR_REF.c)). `VAR_REF` calls
  `frame_ns_ref`, handing back the `KosSymbolicRef` that *backs the caller's
  namespace slot* — a namespace slot is lazily wrapped as a `KosRef` on first
  ref-access (see [namespace.h](../../koskari/lib/namespace.h) `fast[]` /
  `KosNamespace_REF`), so the returned `Ref` and the slot share storage.
- **Installing a Ref as a slot.** `KosNamespace_Bind(ns, key, ref)`
  ([namespace.h](../../koskari/lib/namespace.h)) already binds a name directly to
  a supplied `Ref`. This *is* the aliasing primitive — by-ref apply calls it with
  the caller's `Ref`.
- **The Ref API.** `KosSymbolicRef` exposes `.deref()`, `.store(v)`, `.clear()`,
  `.empty` (true iff unbound), and `.name`
  ([ref.c](../../koskari/lib/ref.c), [ref.h](../../koskari/lib/ref.h)). These are
  the only operations by-ref needs. (Earlier drafts referred to a `.cell` /
  `.is_unbound()` / `.set()` API — that does not exist; the `Ref` *is* the cell,
  `.empty` is the unbound test, `.store` is the write.)

So the only genuinely new code is: recognize the by-ref marking while parsing
formals, carry it on the spec, and have apply bind marked slots via
`KosNamespace_Bind` at the moment it would otherwise store the argument.

## Surface forms

A formal can be marked by-ref three ways. All mean the same binding:

| Surface | Meaning |
|---|---|
| `&foo` | by-ref positional (reader sugar) |
| `(compile/ref foo)` | by-ref positional (explicit; what `&foo` expands to) |
| `(ref foo)` | by-ref positional (treated identically to `(compile/ref foo)`) |
| `&foo: V` | by-ref keyword with floor `V` |
| `[foo byref: True]` | by-ref via the explicit bracket form |
| `[foo :: int value: None byref: True]` | by-ref with type ascription + floor |

`&foo`, `(compile/ref foo)`, and `(ref foo)` are all valid in formals position
and mean the same thing. This is not redundancy: the `&`-reader macro may be
disabled, in which case `&foo` survives as a literal symbol token. By-ref is a
property of the *formal*, not of the reader; the reader sugar is only a
convenience at the call site that happens to also read as an acceptable formal.

A by-ref formal with no floor is a **required positional**; one with a floor is
an **optional keyword** (the floor is its default), exactly as for any formal.
The recognized shapes resolve to the same marking. Note the two terse families
read differently: `&foo` (no colon) becomes `(compile/ref foo)` and lands in the
positional run, while `&foo:` (the keyword form) reads as a keyword *named*
`&foo` and lands in the keyword run — so the parser handles each in its own
place:

```
&foo                 ->  positional, by-ref     ; ref arm: (compile/ref foo)
&foo  (reader off)   ->  positional, by-ref     ; leading & stripped in gather
(compile/ref foo)    ->  positional, by-ref     ; payload must be a lone symbol
(ref foo)            ->  positional, by-ref     ; identical to (compile/ref foo)
&foo: V              ->  keyword, by-ref, floor V  ; keyword '&foo', & stripped
[foo byref: True]    ->  positional, by-ref
[foo value: V byref: True]  ->  keyword, by-ref, floor V
```

A non-symbol payload (`(compile/ref (f x))`) does not match the by-ref arm and
falls through to the ordinary positional parse.

## Spec changes

`byref:` is one new attribute keyword on the bracket form, parsed at the single
point that already reads the type ascription (`type:` / `::`) and the floor
(`value:`) — `_attrs_frames_to_ann` in
[helpers.py](../../koskari/compile/helpers.py). It returns the by-ref flag
alongside the annotation bundle and default. The terse `(compile/ref …)` /
`(ref …)` / `&` shapes feed the same gather path via `frame_formal_name`.

`FormalsSpec` ([formals.h](../../koskari/lib/formals.h)) gains one field:

```
by_refs : tuple[str, ...]   # names of formals marked by-ref
```

Existing fields (`pos`, `pos_only`, `kwd`, `kwd_only`, `star`, `starstar`,
`annotations`) are unchanged. `by_refs` is also carried on the live `Formals`
(copied from the spec) and serialized in the `.kio` `FormalsSpec` pool record
(an ABI bump — [constants.py](../../koskari/serialize/constants.py)). The field
is `None`/absent when empty; the constructor argument is optional.

A `by_refs` name may also have a floor (its default). The floor is an ordinary
default: evaluated once at definition time, like every other koskari default —
there is no per-application floor.

## The floor

A by-ref formal's default is the value its slot holds when the body starts —
the *floor*. Three cases:

- **Supplied a bound cell.** The caller passed a `Ref` that already holds a value
  (even `None`); it is aliased as-is and the floor is inert.
- **Supplied an empty cell, with floor.** The caller passed an unbound `Ref`;
  after aliasing, the floor is written *through* it, so the caller's binding is
  pre-filled.
- **Omitted, with floor.** The slot is bound to a fresh private `Ref` seeded with
  the floor. The caller is not involved.

A by-ref formal with no floor is required: omitting it is an ordinary missing-
argument error.

## Application

By-ref handling happens **at the moment each argument is bound**, not in a
trailing pass — that is where the cleanest signal lives. The decision turns on
two things the binding code already knows: is this name a by-ref formal, and (for
a keyword) is the value coming *from the caller* or *from the default*.

The `store` at each binding site becomes:

```
if name not in formals.by_refs:
    store value normally
elif supplied by the caller:                 # positional arg, or a kwarg in kwargs
    if value is not a Ref:                    # bare value to a by-ref slot
        raise TypeError("by-ref formal ... requires a Ref")
    bind slot to value                        # alias the caller's cell
    if value.empty and name in defaults:
        value.store(defaults[name])           # floor through an empty caller cell
else:                                          # keyword falling back to its default
    bind slot to a fresh Ref(name, defaults[name])   # private floor cell
```

This lives in the `BIND_SUPPLIED_BY_REF` macro (the strict `Ref` check plus the
`KosNamespace_Bind` alias) and the `bind_supplied_by_ref` / `bind_default_by_ref`
helpers (the keyword floor cases), called from `apply_formals_kwargs_phase` and
the per-run positional loops of the four apply variants in
[formals.c](../../koskari/lib/formals.c). Those positional loops were decomposed
from a single counter-loop-with-expiry into one tight loop per formal run, with
the by-ref membership test hoisted out of the inner loop so a call with no
by-ref formals pays nothing for it. The supplied-vs-default fork in the keyword
loop (`if (val) … else if (defaults) …`) **is** the suppliedness signal, so no
separate bookkeeping is needed.

Properties:

- **Strict at the boundary.** A bare value handed to a by-ref slot fails with a
  `TypeError` naming the callable and the formal —
  `try-parse-int() by-ref formal out requires a Ref, not int` — before the body
  runs, rather than silently dropping the author's intended write-through. This
  applies to positional by-ref and to supplied (non-defaulted) keyword by-ref
  alike. The function-name prefix comes from `formals_type_error`, which every
  formals-apply error now routes through.
- **Bind, not store-then-rebind.** A by-ref slot is bound to its `Ref` directly at
  the binding site; there is no intermediate value-store that a later pass undoes.
- **Aliasing, not copy-in/copy-out.** No copy-back step. A `shift` / `call/cc`
  escaping the callee cannot strand a half-applied write — the shared cell simply
  holds whatever it last held, exactly as under an exception.
- **Note on early floor writes.** Because the floor write-through (the empty-cell
  case) now happens at bind time rather than after a fully successful apply, a
  failure *after* that write — e.g. an unexpected-keyword error later in the same
  apply — can leave the caller's cell pre-filled. Aliasing alone never mutates the
  caller; only this floor write does, and only for a supplied empty `Ref`.

## Worked surfaces

```koskari
; sidecar result — case A (caller's cell is full)
define result-sidecar [a b &c]:
    (set! c (+ a b))

define invoke [a b]:
    (define result)                  ; declared, unbound
    (result-sidecar a b &result)     ; case B if sidecar had a floor; A after the set!
    result

; out-parameter with a floor — the sink is elidable (terse keyword by-ref)
define collect-good [items &acc: (list)]:
    for x in items:
        (when (good? x) (push! acc x))
    acc
; (collect-good xs)         -> omitted: pure, returns the floor list
; (collect-good xs &mine)   -> aliases: appends into the caller's list in place

; explicit bracket form — type + floor + byref without sigil cramping
define try-parse-int [s [out :: int value: None byref: True]]:
    if (valid? s):
        (set! out (to-int s))
        true
    else:
        false                        ; floor None already visible to an empty caller cell
```

## What was built

1. **Formals recognition.** One extra arm on the positional formal pattern
   ([matcher.py](../../koskari/matcher.py), `formal_param_pattern`) matches
   `(compile/ref <sym>)` and `(ref <sym>)`; a shared `frame_formal_name` helper
   resolves the positional binding symbol and the by-ref flag (including the
   leading-`&` strip for the reader-off case) for both gather and the hygiene
   introducer. The terse keyword form `&foo:` reads as a keyword named `&foo`;
   `gather_formals` strips the leading `&` from keyword and kwonly names
   (`_strip_byref_kw` / `_strip_byref_sym` in
   [helpers.py](../../koskari/compile/helpers.py)) and records them.
2. **Bracket-form parse.** The `byref:` case in `_attrs_frames_to_ann`
   ([helpers.py](../../koskari/compile/helpers.py)); `gather_formals` collects the
   marked names from all of the positional, keyword, and kwonly runs and returns
   them as a seventh value.
3. **Spec / Formals.** A `by_refs` tuple on `KosFormalsSpec` and `KosFormals`
   ([formals.h](../../koskari/lib/formals.h), `formals.c`), copied spec→formals,
   plus the `.kio` serialization and ABI bump
   ([codex.py](../../koskari/serialize/codex.py),
   [constants.py](../../koskari/serialize/constants.py)).
4. **Apply.** Bind-time handling via the `BIND_SUPPLIED_BY_REF` macro and the
   `bind_supplied_by_ref` / `bind_default_by_ref` helpers
   ([formals.c](../../koskari/lib/formals.c)), wired into the shared
   `apply_formals_kwargs_phase` and the per-run positional loops of all four
   apply variants (`Apply`, `ApplyFast`, `VectorApply`, `VectorApplyFast`), using
   `KosNamespace_Bind`, `KosRef_IsEmpty` / `KosRef_Store`, and `KosSymbolicRef_New`
   for the fresh-floor case. The positional phase was restructured into one tight
   loop per formal run with the by-ref test hoisted out, keeping the no-by-ref
   call path cost-free.
5. **Named errors.** `formals_type_error` prefixes the function name — read from
   the call frame namespace, which already carries it — onto every formals-apply
   `TypeError`: by-ref mismatch, missing argument, too many positional, and
   unexpected keyword alike.
6. **Tests.** [tests/importer/test_byref_formals.py](../../tests/importer/test_byref_formals.py)
   — the surface shapes, the bracket form, write-through, the floor (omitted and
   empty-cell), and the function-named `TypeError` for a bare value to a
   positional or keyword by-ref slot; plus a frame-naming check in
   [tests/lib/test_formals.py](../../tests/lib/test_formals.py).

## Related

- [../api/types.md](../api/types.md) — `Ref` variants.
- [../language/seq-values-kwargs-formals-apply.md](../language/seq-values-kwargs-formals-apply.md)
  — kwargs application shared with other binding sites.
