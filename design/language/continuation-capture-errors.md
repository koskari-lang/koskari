# Continuation capture errors

**Status:** proposal — two decisions open (see [Open decisions](#open-decisions)).

Replace the bare `RuntimeError: prompt not found` raised by the delimited
continuation capture walk with a proper exception family, and give
`koskari.solve` a translation layer so its users never see VM vocabulary.

---

## Why this is worth doing before launch

`koskari.solve` is the strongest demonstration Koskari has: a capability
structurally unavailable in Python, in ~13 lines of user-space Koskari. Calling
`amb` with no enclosing `solve` is the single most likely first mistake anyone
makes against it — and today that produces a bare Python `RuntimeError` whose
message is internal continuation vocabulary the caller never wrote.

The string is also already a contract. It is documented verbatim in
[`docs/reference/runtime/koskari-solve.md`](#documentation-to-update) and in a
corpus record, so it is not merely an internal detail that happens to leak; it
is published behaviour.

---

## The failure is two failures

The capture walk from `dyn_top` locates the nearest matching prompt while
offering `handle(SHIFT)` to each node crossed. It can fail in two structurally
different ways, which currently collapse into one message:

| Mode | Cause | What the user must do |
|---|---|---|
| **No delimiter** | No matching prompt anywhere on the chain. | Establish one — `reset`, or `solve` / `solve-first`. |
| **Unreachable delimiter** | A matching prompt exists, but a crossed node denied `handle(SHIFT)` — the generator boundary. | Restructure. Adding a `reset` will not help. |

Emitting one message for both is a defect in the diagnostic, not only in its
wording: the two fixes are disjoint, and the second is the more confusing
failure precisely because the user *did* the thing the first message would tell
them to do.

The generator case is not hypothetical. It is the documented reason lazy,
one-solution-at-a-time enumeration is a non-goal for `koskari.solve` — "a
`shift` cannot cross a generator boundary" — so any user who reaches for
generator-flavoured laziness over a search lands here.

---

## Tier 1 — VM exception classes

```
KoskariException
└── KoskariContinuationError        ; the catchable category
    ├── KoskariPromptNotFound       ; no matching delimiter on the chain
    └── KoskariPromptUnreachable    ; delimiter exists; a crossed boundary denied capture
```

`KoskariContinuationError` is what user code catches. The two leaves are what
gets raised. The base earns its place as the catch target; each leaf earns its
place because the corrective action differs.

"Prompt" is the right vocabulary **at this tier**. It is the word the
`compile/reset` and `compile/shift` reference pages already use, and a caller
who wrote `shift` by hand knows it. The problem is only that `amb` callers
inherit it — which tier 2 solves.

### Attributes

Both leaves carry:

- **`prompt_id`** — the prompt identifier the capture was seeking. This is the
  load-bearing one: it is what lets a library recognise a failure against *its
  own* delimiter and leave foreign failures alone.
- **`operation`** — the surface construct that triggered capture (`shift`), so
  the message can name it.

`KoskariPromptUnreachable` additionally carries what denied the capture (the
node kind, or a description of the boundary), so the message can say
*generator boundary* rather than *something refused*.

### Message shape

Name the construct the user wrote, state the condition, state the fix. Do not
describe chain topology.

- `KoskariPromptNotFound: shift has no enclosing reset`
- `KoskariPromptUnreachable: shift cannot capture across a generator boundary`

---

## Tier 2 — `koskari.solve` translates

This is the part that actually repairs the hero demo.

Someone calling `amb` never wrote `reset` and should never be told about
prompts. `solve` and `solve-first` establish their own delimiter, so they know
their own `prompt_id`. `amb` (and `fail`, and anything else in the module that
shifts) catches `KoskariPromptNotFound`, checks the id is its own, and re-raises
in the module's own vocabulary — naming `solve` / `solve-first`. A failure
against a *foreign* prompt propagates untouched, because the module has no
standing to interpret it.

Suggested surface, subject to the module's own naming taste:

```
KoskariContinuationError
└── SearchError                     ; koskari.solve's own category
    └── NoActiveSearch              ; amb/fail/require called outside solve
```

Rooting `SearchError` under `KoskariContinuationError` keeps one catch target
for the whole category while letting the module speak its own language.

`KoskariPromptUnreachable` should **not** be translated. The generator-boundary
constraint is a real property of the mechanism, it is documented as such in the
module reference, and dressing it up in search vocabulary would obscure the only
fact that helps.

This pattern — a user-space library establishing a delimiter and translating
capture failures into its own terms — is what any third-party delimited
continuation library will need. `solve` demonstrating it correctly is worth more
than `solve` merely not being confusing.

---

## Open decisions

### 1. Do the classes also subclass `RuntimeError`?

**Recommendation: no.**

Multiple inheritance from `RuntimeError` is free compatibility, and it matches
Python's habit of using `RuntimeError` for "wrong control point". But it makes a
stray `except [RuntimeError]` swallow a search failure — precisely the
overbroad catch one is most likely to find wrapped around a `solve` call. There
is nothing pre-launch to stay compatible with, so the cost is real and the
benefit is not.

Requires confirming what `KoskariException` itself derives from before this is
settled.

### 2. Searchable identifier — class name, or a code?

**Recommendation: class name, for now.**

The complaint being answered is that the current failure has no searchable
identifier. A `KOS-E-NNNN` scheme is the obvious answer and the wrong one to
reach for today: `koskari.lint` is already committed to unifying its codes with
the `error-codes.md` namespace *from the start*, and minting a numbering scheme
here commits lint to it sight-unseen.

A class name is unique, greppable, and searchable the day it ships. A numeric
catalogue can key off class names later without rework. Ship the class; leave
the namespace question to the pass that designs it with lint in the room.

### 3. Scope — sweep the adjacent bare `RuntimeError`?

`defer` awaited a second time raises a bare `RuntimeError`, and that too is
documented contract (`docs/reference/concepts/demand-async-and-defer.md`). Same
category of defect, cheap to fix in the same pass, easy to defer to its own.
Caller's choice.

---

## Implementation notes

### Raise sites

The capture walk is the primary site — the PUSH_SHIFT opcode implementation and
the capture validation helper it calls (`kos_validate_capture` is named in the
VM reference; confirm against the current source, since the VM reference and
`design/OPCODES.md` have drifted on this point — see below). Locate **every**
raise of the current message rather than the first; the undelimited
(`PUSH_CC` / `CAPTURE_CC`) family should be audited in the same pass even if it
turns out to raise nothing comparable.

Also worth a look while in the area: what happens when a `DelimitedContinuation`
is called from Python, which the `compile/shift` reference says is not
permitted. If that surfaces as an unhelpful `TypeError`, it belongs to the same
family.

### A latent doc problem to resolve, not propagate

The PUSH_SHIFT description exists in **three** places and they have already
drifted:

- `design/OPCODES.md` §20 — "Does **not** peel nodes above the prompt via a
  transfer plan."
- `docs/reference/vm/opcodes/kos-op-push-shift.md` — "no unwinding happens at
  capture."
- `docs/reference/vm/index.md` — "builds an unwind transfer plan to unwind from
  the current position to the matching prompt (exclusive); runs the plan."

The third contradicts the first two and appears to describe a superseded design.
Whoever touches these to add the error semantics should reconcile them rather
than editing three copies into three new states. This is a small instance of the
opcode-doc drift already logged in `TODO.md` as having no CI guard.

### Corpus

Record `module-solve-amb-outside-solve-invalid-001` in
`docs/corpus/records/solve-invalid.yaml` carries the old string in
`verify.invalid` prose.

Before rewriting it, resolve a schema question: `verify.error` is documented as
covering read/compile-time failure, and `verify.invalid` entries are **not
executed**. If there is genuinely no slot for asserting a *runtime* exception
type, then this record cannot be made to enforce the new class — and a stable
runtime exception class is exactly the thing that most wants a regression
assertion. Confirm against `docs/corpus/SCHEMA.md` and the `tests/corpus`
runner; if the gap is real, note it rather than quietly leaving the record as
unenforced prose.

---

## Documentation to update

| File | What changes |
|---|---|
| `docs/reference/runtime/koskari-solve.md` | The literal `RuntimeError: prompt not found` in the `amb` section; add the tier-2 class and the generator-boundary failure. |
| `docs/corpus/records/solve-invalid.yaml` | Record `module-solve-amb-outside-solve-invalid-001`; see the schema question above. Consider a sibling record for the generator-boundary case. |
| `docs/reference/error-codes.md` | New rows in the runtime table for `KoskariContinuationError` and its leaves. |
| `docs/reference/specials/compile-shift.md` | An **Errors** section — both failure modes. |
| `docs/reference/specials/compile-reset.md` | Cross-reference from the delimiter side. |
| `docs/reference/vm/opcodes/kos-op-push-shift.md` | Invariants: name the exceptions. |
| `design/OPCODES.md` §20 | Same, in the duplicate copy. |
| `docs/reference/vm/index.md` §20 | Same — and reconcile the drift noted above. |
| `design/TODO.md` | The `reference/error-codes.md` gap is partially addressed; record what remains (the catalogue itself, still pending the lint-namespace decision). |

---

## Tests

- `tests/exceptions/` — hierarchy and attribute surface.
- The reset/shift special tests — both failure modes raise the right leaf, and
  `prompt_id` is populated correctly.
- A generator-boundary reproducer, since that path has no user-facing coverage
  today and is the one the new class exists to disambiguate.
- `koskari.solve` — `amb` outside a search raises the module's class; a capture
  failure against a *foreign* prompt propagates untranslated. The second is the
  one that proves the `prompt_id` check is doing real work.

---

# The end.
