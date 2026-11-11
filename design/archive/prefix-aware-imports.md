# Prefix-Aware Imports

**Status:** Completed and archived
**Author:** Christopher O'Brien <obriencj@preoccupied.net>

The work this document designed shipped as the `use` form and the
`IMPORT_USE` opcode, with `declare-exports` authoring feature stems into
`__features__` and `__all__` materialized at end of load. Reality
diverged from the design in places this document does not track: the
form is named `use` (no `require` ever shipped), candidates derive in
the opcode handler rather than being declared at compile time, the
module-level gate is a property of the `use` special rather than dual
emission, and star import stays at Python parity with the family
behavior belonging to `use` alone. The surviving documentation is
SPECIALS.md, OPCODES.md, api/import-system.md, and the user pages
(reference/specials/use.md and friends).

Definition-side injection is in place: `define-syntax`, `extend-syntax`,
and `let-syntax` bind (and overlay) under `compile/NAME` regardless of
the written spelling (`koskari/bootstrap/_define_syntax.ki`). The
resolver flip has happened: `find_compiled` and `find_layout` are
prefix-only, so the short name is the use and the prefixed spelling
denotes the object. An interim `IMPORT_FROM` probe that retried an
unprefixed member as `compile/{attr}` was tried and reverted -- it bound
the transformer under the consumer's bare name, wrong under this
design. Until the family form is implemented, a transformer is imported by its
explicit binding name, `(import-from mod compile/NAME)`, which composes
correctly with local definitions and `extend-syntax`. The family form
will be a **separate opcode or a mode switch on `IMPORT_FROM`**, not new
behavior inside the existing opcode, which keeps `import-from` at exact
Python parity.

---

## Background

Koskari keeps compile-time and runtime objects in one namespace and separates
them by a naming convention: a special or macro is bound as `compile/NAME`, a
block handler as `layout/NAME`. The prefix is now an implicit facet of
definition: a transformer is bound only under `compile/NAME`, `find_compiled`
resolves a bare `NAME` by prepending the prefix, and an already-prefixed
input never resolves as a use. The prefixed spelling names the transformer
object itself.

That leaves bare-member imports stranded. A module defining `tacos` binds
`compile/tacos`, so `(import-from mod tacos)` finds no attribute; the
explicit `(import-from mod compile/tacos)` works and is the current
spelling. This document specifies the family form that lets consumers
import by the written name again.

See [../api/import-system.md](../api/import-system.md) for the existing
machinery this builds on.

## Namespace prefixes

The recognized prefix set is a single shared constant. Today its members are:

| Prefix | Contents |
|---|---|
| `compile/` | specials, macros, and syntax transformers |
| `layout/` | reader block handlers |

`match/` is referenced by `koskari.help` but has no registrations; it is
reserved, not active. `reader/` is not a member: names like
`reader/pass-none` are internal atom-pattern keys, not importable bindings.

Two kinds of consumer read this set, and only one iterates it:

- **Resolution** never iterates. Position selects the namespace: a form head is
  a `compile/` lookup, a block head is a `layout/` lookup. Adding a prefix to
  the set costs nothing on the compile path.
- **Family operations** iterate. These ask "everything named X", and today that
  means `koskari.help` and, under this design, imports.

## Behavior

### 1. Member spellings

An `import-from` member is either **bare** or **prefixed**, decided by testing
the written symbol against the prefix set.

- A **bare** member names a feature. It expands to that feature's family.
- A **prefixed** member names one binding exactly. It is taken literally, with
  no expansion, at every scope depth.

The literal reading is what makes a single facet addressable. Without it there
would be no way to name `compile/tacos` on its own.

### 2. Family expansion

A bare member `NAME` expands to the candidate set:

```
NAME  compile/NAME  layout/NAME
```

The candidate set is derived at compile time from the written name and the
prefix set, so the compiler declares every candidate. Which candidates are
actually stored is decided at runtime, because the source module is not loaded
when the import form compiles.

A candidate that the source module does not define is skipped without error.
At least one candidate must resolve; if none do, the import fails (§5).

### 3. Scope gating

Family expansion applies only at module level (`scope_depth == 0`). Inside a
function, `let`, or any other nested scope, a bare member binds the runtime
name alone, which is the present behavior.

This is a semantic restriction rather than a mechanical one. `find_compiled`
reads the module namespace and the macro overlay; `find_layout` takes an
explicit namespace. Neither consults local slots, so a `compile/NAME` bound
into a `let` is invisible to resolution. The enclosing function body has also
already been compiled by the time the import runs, so a compile-time facet
imported there could not have affected it. Importing those facets into a nested
scope is inert by construction.

A prefixed member is still permitted in a nested scope. It binds the
transformer as an ordinary value, which is the correct reading when the intent
is to inspect or manipulate the object rather than use it as syntax.

### 4. Renames

The `[ATTR as: BIND]` form renames the whole family. Each candidate keeps its
prefix and takes the new stem:

```
[tacos as: burritos]
  tacos          -> burritos
  compile/tacos  -> compile/burritos
  layout/tacos   -> layout/burritos
```

Renaming a feature therefore renames its block syntax and its form syntax
together. Under per-binding renames the two could drift, leaving a feature
whose block syntax still answers to the old name.

A prefixed member renames as written, producing a plain value binding:

```
[compile/tacos as: burritos]
  compile/tacos  -> burritos
```

### 5. Errors

Two failures need distinct messages.

**No candidate resolved.** The message names the member and the spellings
tried:

```
module mod has no tacos (tried tacos, compile/tacos, layout/tacos)
```

**Nested scope, compile-time-only feature.** A bare member in a nested scope
looks for the runtime name alone. When that misses, the error path probes the
prefixed spellings to produce a directed message:

```
module mod has no tacos; it defines compile/tacos, which is only
importable at module level or by explicit name
```

The probe runs only on the error path, so it costs nothing in the common case.

### 6. `import-all-from`

Star import stays at Python parity and gains no family translation:
exporting a transformer means naming its binding explicitly, so a module
that wants its macros star-importable lists `compile/x` alongside `x` in
its exports. `declare-exports` keeps building `__all__` from exactly the
symbols it is given. The family behavior belongs to the `require` form
alone.

The existing module-level restriction on `import-all-from` is unchanged.

### 7. Unchanged

- `import` (whole-module import) is unaffected; it binds a module object.
- `import-from` is unaffected: exact Python attribute semantics, with the
  prefixed member spelling as the explicit path to a transformer.
- Python-side registrations keep writing the prefix: `@builtin("compile/foo")`
  states the binding key directly.

## Implementation notes

**Prefix set.** No shared importable constant: the prefix spellings are
each consumer's own literal (`koskari/help/tools.ki` carries its own).

**The family form.** A separate opcode (working name `IMPORT_FAMILY`) or a
mode switch on `IMPORT_FROM`, emitted by the family special; the existing
`IMPORT_FROM` keeps exact Python semantics either way. The payload is one
**stem per member** with a parallel binds tuple -- the handler derives each
stem's candidate set itself, so one stem is one group and no group-id
plumbing is needed; a prefixed stem degenerates to a candidate set of one.
The compiler emits the family opcode at module level and plain
`IMPORT_FROM` in nested scopes, which is §3's gate expressed structurally.

**`IMPORT_STAR`.** Unchanged; see §6.

## Open questions

- The consumer surface for family import. The working name has been
  `require` (Elixir's analogue for making compile-time facets available),
  with `use` reserved for the bundle install hook. The two may be implemented
  together, and `use` may subsume `require` entirely -- bundles as the
  one model, leaving only `import` (Python parity) and `use`.
- Whether `match/` becomes an active prefix for `syntax-match`, `match/case`,
  and other match-language builders. Deferring is safe as long as the set stays
  centralized; adding it later is then a one-line change that `help` and
  imports both pick up.
- Whether a nested bare import that resolves nothing should fail with the
  directed message of §5 or with a plain missing-attribute error.

## See also

- [compile-time-objects.md](compile-time-objects.md)
- [../api/import-system.md](../api/import-system.md)
