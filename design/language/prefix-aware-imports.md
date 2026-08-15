# Prefix-Aware Imports

**Status:** Planned
**Author:** Christopher O'Brien <obriencj@preoccupied.net>

---

## Background

Koskari keeps compile-time and runtime objects in one namespace and separates
them by a naming convention: a special or macro is bound as `compile/NAME`, a
block handler as `layout/NAME`. Today `find_compiled` accepts either spelling,
resolving `NAME` by falling through to `compile/NAME`, and accepting
`compile/NAME` directly.

Once the prefix becomes an implicit facet of definition, that changes. A
transformer is bound only under `compile/NAME`, `find_compiled` resolves a bare
`NAME` by prepending the prefix, and it no longer accepts an already-prefixed
input. The prefixed spelling stops being a way to invoke the form and becomes
the way to name the transformer object itself.

That leaves imports stranded. A module defining `tacos` binds `compile/tacos`,
so `(import-from mod tacos)` finds no attribute, and a hypothetical
`(import-from mod compile/tacos)` binds a name the resolver would refuse to
look up. This document specifies how imports bridge that gap.

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

`__all__` holds short names. `declare-exports` builds it from the symbols it is
given, and it commonly appears near the top of a file before any definition has
run, so it cannot know which of its names are transformers. Making it
prefix-aware is not possible; the translation belongs to the consumer.

`IMPORT_STAR` therefore applies the same family expansion to each `__all__`
entry. Without this it binds the runtime facet and silently drops the macro.

The existing module-level restriction on `import-all-from` is unchanged.

### 7. Unchanged

- `import` (whole-module import) is unaffected; it binds a module object.
- Python-side registrations keep writing the prefix: `@builtin("compile/foo")`
  states the binding key directly.
- `__all__` contents stay in short names.
- Patterns that match prefixed spellings in user source are unaffected; this
  document covers binding and resolution only.

## Implementation notes

**Prefix set constant.** Currently a literal in `koskari/help/tools.ki`. It
moves somewhere shared and is read by `help`, `special_import_from`, and the
`IMPORT_STAR` path.

**`special_import_from`.** Classifies each member as bare or prefixed, expands
bare members when `scope_depth == 0`, declares every candidate, and emits the
expanded attr/bind pairs. The existing payload already carries parallel
`attrs`/`binds` tuples, so renames and expansion need no new operand shape.

**`IMPORT_FROM` grouping.** This is the one piece of new plumbing. A missing
attribute is currently a hard error per pair; with families a per-pair miss is
normal and only an all-missed family is an error. The flat parallel tuples
cannot express which pairs belong to the same member, so the payload needs
either a third parallel array of group ids or a restructure into per-member
groups. The error text in §5 is only constructible once the handler knows the
grouping, so this should be designed before the special is written.

**`IMPORT_STAR`.** Applies family expansion per `__all__` entry.

## Open questions

- Whether `match/` becomes an active prefix for `syntax-match`, `match/case`,
  and other match-language builders. Deferring is safe as long as the set stays
  centralized; adding it later is then a one-line change that `help` and
  imports both pick up.
- Whether a nested bare import that resolves nothing should fail with the
  directed message of §5 or with a plain missing-attribute error.

## See also

- [compile-time-objects.md](compile-time-objects.md)
- [../api/import-system.md](../api/import-system.md)
