# Formal default modes (`value:` / `copy:` / `new:`)

## Status

**Proposal (2026-08-09).** Not implemented. Companion to
[archive/by-ref-formals.md](../archive/by-ref-formals.md), which this extends:
by-ref formals introduced the *floor* (a by-ref formal's default), and this
proposal generalizes what a floor -- and every other default -- actually is.

The recommended first step is to implement it and live with it, with the
explicit intent of making the **transposer** use-case comfortable. See
[TODO.md](../TODO.md) §Recorded proposals.

## Goal

Today a defaulted formal carries **a value**, evaluated once when the closure is
built and stored into the slot at each application that omits the argument. The
generalization: a defaulted formal carries **an operand plus an operation**, and
the operation produces the slot's value at each such application.

| Mode | Operand | Operation at application |
|---|---|---|
| `value` | any object | store the operand directly (share it) |
| `copy` | any object | store a duplicate of the operand |
| `new` | a callable | store the result of calling it with no arguments |

`value` is today's behavior, restated as one mode among three rather than as a
special case. Nothing that exists changes meaning.

The operand is still evaluated exactly once, in the defining scope, when the
closure is built. Only the operation applied to it at application time is new.
This is deliberate: it keeps the whole feature outside the evaluator, which a
per-application *expression* would not (see
[§`new:` and the evaluator](#new-and-the-evaluator)).

## The problem being solved

A koskari default is a single object shared by every application that omits the
argument. For an immutable operand that is invisible. For a mutable one it is a
cross-call aliasing bug:

```koskari
define collect [items &acc: #[]]:
    for-each [x items]:
        (acc.append x)
    acc

(collect #[1 2 3])    ; -> [1, 2, 3]
(collect #[4 5])      ; -> [1, 2, 3, 4, 5]   <-- same list
```

The workaround is the None-dance: declare the floor as `None` and rebuild it in
the body. Three copies of it were written into
[tools/corpus/quick.ki](../../tools/corpus/quick.ki) when the `_stage_*`
functions were converted to a by-ref warning sink, purely to dodge this:

```koskari
define _stage_read [case source &warnings: None]:
    when (None? warnings):
        set! warnings #[]
    ...
```

Base rate in this tree: zero `def` sites in `koskari/` or `tools/` use a mutable
default deliberately; the None-dance appears at
[compiler.py:504](../../koskari/compile/compiler.py:504) and
[hygiene.py:541](../../koskari/hygiene.py:541), plus the three in `quick.ki`.
The intentional case is not merely rarer than the workaround, it is absent.

## What already works today (and what does not)

The accumulator model is **already viable** without this proposal, provided the
accumulation is by *assignment to the storage* rather than by *mutation of the
collection*. A shared immutable floor is harmless, because each `set!` rebinds
through the ref instead of touching the shared object:

```koskari
;; cons'ing onto a nil floor
define collect-pairs [items &acc: nil]:
    for-each [x items]:
        set! acc (cons x acc)
    acc

;; extending an empty-tuple floor
define collect-tuple [items &acc: #()]:
    for-each [x items]:
        set! acc (+ acc #(x))
    acc
```

Verified against the current build:

| Form | call 1 | call 2 |
|---|---|---|
| `&acc: nil` + `(set! acc (cons x acc))` | `(3 2 1)` | `(5 4)` |
| `&acc: #()` + `(set! acc (+ acc #(x)))` | `(1, 2, 3)` | `(4, 5)` |
| `&acc: #[]` + `(acc.append x)` | `[1, 2, 3]` | `[1, 2, 3, 4, 5]` |

The caller-supplied path is unaffected either way: passing `&mine` against an
`#()` floor writes the accumulated tuple back through the caller's binding.

So this proposal is not what makes accumulators possible. It is what makes the
**mutable** accumulator possible, and what removes the requirement that an
author reach for a copy-on-write collection to stay correct. Persistent-style
accumulation stays a legitimate and slightly cheaper choice; `copy:` / `new:`
remove the trap from the style most people will reach for first.

## Surface forms

The long (bracket) form carries the mode as an attribute key, alongside the
existing `value:`, `type:` / `::`, and `byref:`:

| Surface | Mode | Meaning |
|---|---|---|
| `x: V` | `value` | terse keyword default (today) |
| `[x value: V]` | `value` | long form of the same |
| `[x copy: V]` | `copy` | a duplicate of `V` per application |
| `[x new: F]` | `new` | `F()` per application |
| `&x: V` | `copy` | by-ref keyword floor -- **copied** per application |
| `[x byref: True value: V]` | `value` | explicit opt-in to a shared floor |
| `[x :: list copy: #[]]` | `copy` | composes with type ascription |

`value:`, `copy:`, and `new:` are mutually exclusive; more than one on a formal
is a `SpecialError` from the attribute fold. `type:` / `::` and `byref:` compose
with all three.

There is **no terse spelling for `copy:` or `new:` on a by-value formal.** The
bracket form is the spelling. See
[§Why no marker in default position](#why-no-marker-in-default-position).

## Default mode per formal kind

- **By-value formals default to `value`.** Unchanged from today. A shared
  default is frequently what the author means (`handler: some-object`,
  `cls: SomeClass`, and the late-binding capture idiom `x: x`, all of which want
  identity), so the mode stays explicit when it differs.
- **By-ref formals default to `copy`.** A shared mutable floor is never correct
  on a by-ref formal, in any of the three floor cases documented in
  [archive/by-ref-formals.md](../archive/by-ref-formals.md) §The floor:
  - *Supplied a full cell*: the floor is inert, so sharing is unobservable.
  - *Supplied an empty cell*: the floor is written **through** into the caller's
    binding, so a shared operand hands the same object to every such caller.
  - *Omitted*: the slot gets a private Ref seeded with the floor, so a shared
    operand accumulates across calls (the bug above).

  There is no reading under which sharing is the intent. Copying an immutable
  operand is a no-op that fast-paths trivially, so the rule stays uniform with
  no compile-time mutability sniffing: `&n: 0` copies and costs nothing, `&acc:
  #[]` copies and is correct. Authors who genuinely want a shared floor spell it
  `[acc byref: True value: V]`.

This split is what keeps the motivating case terse. `&acc: #[]` is the
accumulator/transposer signature, at the spelling it already has, without the
guard preamble.

## Why no marker in default position

An earlier sketch used s-expression markers in the default slot: `col: (new
list)`, `col: (copy #[])`. Rejected.

Formals already recognize marker forms structurally -- `formal_param_pattern`
in [matcher/forms.py](../../koskari/matcher/forms.py) matches `(compile/ref <sym>)` and
`(ref <sym>)` -- but that precedent does not transfer, because of a position
asymmetry. The *binding* position can only ever name a variable, so a call form
there has no competing reading. The *default* position is an arbitrary
expression position, so every marker collides with a real call. `col: (copy
#[])` is legal today and means "call `copy`", and `copy` is a name people
import. Reinterpreting it would change existing valid code from copy-once to
copy-per-call, silently, with no diagnostic at either compile or run time.

`col: (compile/new list)` would be unambiguous, since `compile/` is the
established marker namespace, but it is longer than `[col new: list]` and so
fails at the only job a terse form has.

The deeper reason a terse form is not needed: `byref:` earned the `&` sigil
because it is a binding-mode modifier orthogonal to everything else, while
`value:` / `copy:` / `new:` are three alternatives filling one slot. The terse
`name: V` covers the common one, and reaching for a different fill mode is
exactly the moment an author wants to be explicit.

## Mechanism

The compiler side is a fold arm and a channel. The runtime side is a switch at
three call sites.

**Parse.** `_attrs_frames_to_ann`
([helpers.py:1033](../../koskari/compile/helpers.py:1033)) is already a
three-key fold over formal attribute frames (`value:`, `type:`/`::`, `byref:`).
`copy:` and `new:` are two more arms. Its return tuple grows a mode alongside
the default, threading through `gather_formals` into
[lambda_.py](../../koskari/compile/specials/lambda_.py).

**Codegen.** Unchanged. [lambda_.py:83](../../koskari/compile/specials/lambda_.py:83)
already compiles each default expression in the defining scope and leaves the
resulting object for the formals to hold. The operand for `copy:` / `new:` is
compiled identically; only the recorded mode differs.

**Carry.** A `default_modes` channel on `KosFormalsSpec` / `KosFormals`,
parallel to the existing `by_refs` tuple
([formals.c:68](../../koskari/lib/formals.c:68),
[formals.c:476](../../koskari/lib/formals.c:476)). Crucially, `defaults` keeps
holding the **plain operand**, so everything that introspects a function's
defaults -- `koskari.help`, the LSP, and the annotations / FastAPI
signature-driven conversion -- keeps reading real values with no wrapper to
unwrap.

(An earlier sketch made `Fresh` a boxing *type* stored in `defaults`. That
needed no compiler change at all, which was its appeal, but it put a wrapper in
front of every default consumer. A structural mode is known at compile time and
does not leak.)

**Apply.** Exactly three sites read a default value, all in the kwargs phase,
all cold (a supplied argument never reaches them):

| Site | Role |
|---|---|
| [formals.c:829](../../koskari/lib/formals.c:829) | floor write-through into a supplied empty `Ref` |
| [formals.c:921](../../koskari/lib/formals.c:921) | general default fill |
| [formals.c:958](../../koskari/lib/formals.c:958) | general default fill |

Each becomes a `resolve_default(name, defaults, modes)` call that switches on
the mode. The operation itself belongs to the mode implementation, not to
formals -- formals reads a slot and stores a value, as it does now.

## `new:` and the evaluator

`new: list` is a `PyObject_CallNoArgs` and is unproblematic. `new:` pointed at a
koskari callable is not: it would be called from C in the middle of formals
binding, where there is no evaluator to suspend into. Without `@demand` such a
callable hands back a coroutine, so every application would silently receive an
un-awaited coroutine as its default -- the same trap as passing a bare koskari
lambda into a Python higher-order function.

Two paths:

1. **MVP: enforce demand at invocation.** `new:` accepts a Python callable or a
   `@demand`-wrapped koskari callable; a bare `KosFunction` is rejected with a
   clear error at the binding site. Simple, and it makes the failure loud.
2. **Eventually: flatten formals application into the eval loop.** More
   involved, and arguably the most correct answer, since it naturally admits
   async suspension in a default and would also open the door to defaults that
   reference earlier formals.

The MVP is the recommended start. The constraint that keeps path 2 open: the
mode channel must record the **operation** (`CALL`), never a pre-resolved
callable or a pre-computed result. Then flattening changes only what happens at
the binding site, behind a formals representation that does not move.

## Copy semantics

`copy` is **shallow**, matching `copy.copy`. `[opts copy: #{"a": #[]}]` gives
each application its own dict over one shared inner list. This is the same
footgun `copy.copy` has, but it sits inside a feature whose stated purpose is
fixing an aliasing trap, so it needs to be documented prominently or given a
deep variant.

List, dict, and set operands should fast-path to `PyList_GetSlice` /
`PyDict_Copy` / `PySet_New` in C. Beyond speed, this keeps the common cases away
from `copy.copy`'s `__reduce_ex__` fallback, which can reach arbitrary Python
and -- via a koskari-defined `__copy__` -- arbitrary koskari, re-opening the
evaluator problem through a side door.

## Lint

Once the modes exist, a by-value formal whose default is a **literal collection
display** (`#[]`, `#{}`, a set/dict literal) becomes a lint target with a
concrete fix: say `copy:` or `new:` if you meant a fresh one, `value:` if you
meant a shared one.

The literal-display trigger is what keeps this a good lint rather than a noisy
one. A default written as an inline constructor is a *formula* the author spelled
out; a default written as a name (`x: x`, `handler: obj`) is a *reference* to an
object living elsewhere, and the late-binding capture idiom depends on that
staying shared. The lint fires only on the former, so it has essentially no false
positives.

This is the piece that makes the feature discoverable, and it hangs on the
existing lint-pragma infrastructure. See
[koskari-lint.md](../koskari-lint.md).

## Open questions

- **Deep copy.** Ship shallow-only and document, or offer `deep-copy:` / a
  `deep: True` attribute from the start?
- **`new:` and zero-arg-ness.** Reject a callable that cannot be called with no
  arguments at closure-build time, or let it fail at the first application that
  omits the argument?
- **`*` / `**` formals.** By-ref is forbidden on them
  ([archive/by-ref-formals.md](../archive/by-ref-formals.md) §Goal). Default
  modes are meaningless there too, so the attribute fold should reject them
  rather than silently ignore.
- **Introspection surface.** Should `Formals` expose the mode to Python readers
  (help, LSP, FastAPI conversion), or is the plain operand the whole contract?
  The signature-driven conversion path is the one that might want to know.
- **Migration.** By-ref floors changing from `value` to `copy` is a behavior
  change for existing code. Given that every affected case is a bug (§Default
  mode per formal kind), a silent fix is defensible, but it should be called out
  in release notes rather than slipped in.

## Motivating shape

The transposer -- a function whose primary result is its return value and whose
secondary result is written into a caller-supplied sink -- is the use-case this
is meant to make comfortable. With by-ref defaulting to `copy`, the sink is
elidable and correct with no body ceremony:

```koskari
define _stage_read [case source &warnings: #[]]:
    """
    Produce the read-stage encoding for a source form

    :param warnings: optional by-ref sink for advisory warnings
    """

    define form (case.read_source source)
    define text (repr form)
    define reread (case.parse_expected_form text)

    unless (forms_equal form reread):
        warnings.append "read string did not round-trip; verify by hand"

    return text
```

Three call shapes, all correct:

```koskari
(_stage_read case source)          ; sink elided; private fresh list, discarded
(_stage_read case source &mine)    ; aliases the caller's list
(_stage_read case source &fresh)   ; unbound cell; the floor goes into the caller
```

Against today's build the same function needs a `when (None? warnings) (set!
warnings #[])` preamble, repeated at every stage function, plus a block comment
explaining why. That delta, three times over in one file, is the argument for
implementing this and judging it by feel.
