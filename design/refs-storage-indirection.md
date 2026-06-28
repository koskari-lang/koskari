# Koskari Design: Refs as Storage-Location Indirection (docs + corpus + tests)

## Status

Proposed. Single combined work item: a focused user-doc page on refs, a
relocation of the by-ref material out of the functions tutorial, supporting
corpus entries, and unit tests that pin the example cases.

## Summary

Koskari names do not hold values; each binding maps a name to a **ref** (a
storage-location handle). This indirection is the substrate under assignment,
`deref`, parameters, and **by-ref formals** — yet the user docs only mention it
in passing. We will give refs a dedicated, pedagogical home that builds the
mental model from the ground up ("like pointers, but safer"), move the by-ref
function example there, and back the whole thing with corpus records and unit
tests so the examples are verified and drift-proof.

The mental model to teach: a ref is *indirection to a storage location*. You can
obtain one (`&name`, `&(item coll k)`, `(ref …)`), read through it (`deref` /
`*name`), write through it (`set!`), and hand it to another form so that form
writes back to the original location (by-ref formals). Unlike a C pointer there
is no arithmetic, no dangling, and no null deref hazard — a ref is a typed
capability over exactly one slot.

## Motivation

- The concept is foundational but currently scattered: the reference page
  [docs/reference/concepts/bindings-and-refs.md](../docs/reference/concepts/bindings-and-refs.md)
  is a vocabulary summary, and the by-ref example lives inside the functions
  tutorial ([docs/guide/03-functions-deep.md](../docs/guide/03-functions-deep.md)),
  where it competes for attention with defaults, recursion, and closures.
- Programmers arriving from Python (no by-ref params) or C/C++ (raw pointers)
  both need a bridge. A single page that develops "storage indirection" answers
  both audiences and gives by-ref formals a natural lead-up.
- Examples that are not corpus-linked or tested rot. Tying the new page's code
  to corpus records and unit tests keeps it honest.

## Scope (deliverables)

1. **New user-doc page** — a dedicated refs page in the guide track that
   develops storage indirection in **Q&A format**, mirroring the from-python
   style. Outline:
   - What is a ref? (name → slot; the indirection)
   - Getting a ref: `&name`, `&obj.attr`, `&(item coll k)`, explicit `(ref …)`.
   - Reading: `deref` and the `*name` shorthand.
   - Writing: `set!` through a ref vs rebinding a name.
   - Bind vs store (reuse the distinction already in the concept page).
   - Passing refs to functions: **by-ref formals** (`[a &result]`), including
     why the call site needs `&value`.
   - The "pointers, but safer" framing: no arithmetic, no dangling, one slot.
   - Pitfall: passing a *value* where a ref is required (error case).
2. **Relocate by-ref examples** — remove the "By-ref parameters" section from
   [03-functions-deep.md](../docs/guide/03-functions-deep.md) and leave a short
   pointer/cross-link to the new refs page. Keep the functions tutorial focused
   on functions. The reference concept page stays as the terse vocabulary
   summary and links to the new page for depth.
3. **Corpus entries** — new records (likely `docs/corpus/records/refs.yaml`,
   or extend `variables.yaml`) derived from the example, demonstrating:
   - obtaining a ref to a variable and to an item (`&v`, `&(item xs 0)`);
   - passing a ref into a by-ref function and observing write-through;
   - **error variations**: passing a plain value where a ref is required (and
     any other malformed call), with the expected diagnostic at the appropriate
     phase. Follow the per-feature mandate: basic, variant, and
     invalid+corrective entries.
4. **Unit tests** — tests that exercise the exact example snippets end-to-end
   (read → compile → eval), asserting the write-through result (`value` becomes
   `101`, list slot mutated, etc.) and asserting the failure mode when a value
   is passed instead of a ref. Likely alongside
   [tests/importer/test_byref_formals.py](../tests/importer/test_byref_formals.py)
   or a new `tests/reproducers/` scenario so the documented examples are pinned.

## Design notes

- **Single source of truth.** The new page is the pedagogical narrative; it
  links down to the normative references ([compile/ref](../docs/reference/macros/compile-ref.md),
  [compile/deref](../docs/reference/specials/compile-deref.md), the bindings
  concept page) and up from the tutorials. Avoid duplicating normative grammar —
  that lives in [SYNTAX.md](SYNTAX.md) and the reference pages.
- **Audience bridges.** Explicitly contrast with Python (returns/shared-object
  mutation only) and C/C++ (pointer with arithmetic and null/dangling hazards),
  so each audience can map prior knowledge onto the safer ref model.
- **Two `&` surfaces.** The page must keep disambiguating the formal-name `&`
  marker from the call-site ref reader form, since both appear together in the
  by-ref example.
- **Verification coupling.** Every code block on the page should correspond to a
  corpus record and/or a unit test so the examples cannot silently break.

## Verification

- `make docs` builds clean (no new warnings; all intra/inter-page links
  resolve).
- `tox -qe py -- tests/importer/test_byref_formals.py <new tests>` passes,
  including the value-instead-of-ref error case.
- `make test` `phase_corpus` validates the new corpus records (read / compile /
  eval oracles), including the invalid+corrective entries.

## Out of scope

- Any change to ref runtime semantics or the `compile/ref` lowering.
- A full stdlib reference for refs (tracked separately under the User
  documentation gaps in [TODO.md](TODO.md)).
