# Koskari Design: the gensym atom subtype

## Status

Archived specification. Implemented on branch `hygiene-rework`
(`0aaebd75` through `1b64c70b`, 2026-09-23 and 24). `Mark` and the
`gensym` builtin are one type, a subtype of the interned symbol; the
matcher carries no hygiene; name resolution is unchanged. The rollups
are [MATCHER.md](MATCHER.md) and [SPECIALS.md](SPECIALS.md); the user
page is `docs/guide/hygiene.md`.

As built, against the text below:

- The type lives in `koskari/lib/atom.c` beside symbol and keyword and
  shares `intern_syms`; there is no `gensym.c/h` unit.
- One Python name: `gensym(name=None, predicate=None, *, marks=None)`
  mints a fresh mark or spells a base under given marks, raising
  `ValueError` when another atom holds the spelling; `gensym.next_mark()`
  is the process counter, seeded from `os.urandom`.
- Equality is identity, with no `tp_richcompare` or `tp_hash`; the
  base-equality rule was dropped for the reasons in Feedback.
- `base` is a getset on symbol and keyword as well, returning the atom.
- `strip_mark` and `strip_marks` stay in `koskari/types.py` for sites
  whose operand may be a pair; `is_mark` is gone, and a known atom reads
  `.base`.
- Keywords carry no marks. A keyword-named formal binds the caller's
  spelling and is not renamed; a template's scoped reference to it falls
  back to that name.
- `quote` and `quasiquote` project marks. `quote-syntax` stays for the
  transformer lowerings, and `quasiquote-syntax` was added after this
  note for a procedural macro a template defines, with the reader
  prefixes `#'` and `` #` `` and matching layout block forms.
- No pool kind carries marks. A gensym in a constant is written as the
  `SYMBOL` record of its scoped spelling; the Serialization section
  below described a `GENSYM` kind that was added and then removed.

## Summary

`Mark` is not a symbol, so every atom reader in the compiler, the
specials, the layouts, and the matcher peels the box before it can ask
what it is looking at. `KosGensym` is a symbol. `KosSymbol_Check` and
`is_symbol` accept it, and it compares equal to the interned symbol it
spells, so a template-introduced head, clause keyword, or literal
matches the pattern that names it with nothing peeled.

The type carries what `Mark` carries and spells itself the way
`gensym` already spells itself, so the binding key a name resolves
under does not change and nothing in the resolution path changes.

## The type

`KosGensym` subtypes `KosSymbolType`. One C unit, `gensym.c/h`,
replaces `mark.c/h`. `KosSymbolType` needs no new flags:
`PyType_Ready` does not require `Py_TPFLAGS_BASETYPE` for a static C
subtype, and the tree already relies on this, with `KosBracketPairType`
(`koskari/lib/pair.c:1701`) and `KosNilType` (`:1823`) subtyping
`KosPairType` (`:1580`), which carries only the default flags. Leaving
the flag off also prevents Python code from subclassing the type.

### Fields

Two beyond the interned-atom base:

- `base` -- the interned symbol it spells.
- `marks` -- tuple of int, outermost expansion first.

There is no third field for the key. `KosInternedAtom_NAME` holds the
derived name, `base` followed by one `#hex` group per mark, and that
name is the binding key. `str()`, `repr()`, and the sequence slots are
inherited and already correct.

### Construction

`KosGensym_New(base, marks)` builds the derived name, allocates with
`PyObject_New` against `KosGensymType`, sets the name, `base` and
`marks`, and interns the instance in `intern_syms` under that name. It
mirrors `atom_new` (`koskari/lib/atom.c:46`) rather than calling it,
because `atom_new` sets only the name and would leave the two new
fields unset. A later call with the same base and marks returns the
interned instance, so two references to one scoped name are one object.

The constructor is exposed to Python. The codex decoder and the import
binding site both build a gensym from a base and an existing marks
tuple.

`gensym(name=None, predicate=None)` returns
`KosGensym_New(symbol(name or "<gensym>"), (fresh,))` and keeps its
signature, its retry-on-predicate loop, and its spelling. `str(gensym("t"))`
is `t#hex` as it is today.

One counter serves both, lives in C beside the constructor, and is
seeded once per process from `randbits(32) + 1`, which is what
`EXPANSION_MARK_BASE` (`koskari/syntax.py:71`) does now. `syntax.py`
draws its expansion marks from it. The randomized seed keeps a mark
decoded from a `.kio` from colliding with a locally minted one.

### Equality and hash

`tp_hash` returns `PyObject_Hash(base)`, the hash of the base object,
not of its name string. A gensym and its base must land in the same
dict bucket or a lookup keyed by one misses an entry keyed by the
other.

`tp_richcompare` handles `Py_EQ` and `Py_NE` and returns
`Py_NotImplemented` otherwise. A gensym equals its base atom. Two
gensyms are equal when base and marks both match, which interning
reduces to identity. Everything else is unequal.

This rule is what frees the matcher. `TEST_LITERAL`
(`koskari/lib/matcher.c:1353`) is `lit == got` followed by
`PyObject_RichCompareBool`, so a literal named in a pattern matches a
template-introduced occurrence with no peel.

Reflected comparison resolves correctly in both directions.
`KosSymbolType` defines no `tp_richcompare`, and the subtype's slot is
tried first when either operand is a gensym, so `PySequence_Contains`
over a tuple of interned symbols answers correctly with the interned
symbol on the left.

### Deallocation

`tp_dealloc` is written out, and does not call `symbol_dealloc`
(`koskari/lib/atom.c:145`). It repeats that function's intern removal
against its own name, then clears `base`, `marks`, and the name, then
calls `tp_free`. Delegating would forge a refcount on the wrong object
and delete the wrong intern entry.

`tp_new` is written out. `symbol_new` (`koskari/lib/atom.c:156`)
ignores the type it is handed and returns a plain interned symbol, and
`tp_new` is inherited by a static subtype, so without its own slot the
type would construct the wrong thing from Python. Every static subtype
in the tree sets one.

No GC slots. The base type has no `Py_TPFLAGS_HAVE_GC`, the subtype
references a symbol and a tuple of ints, and no cycle is constructible.

## The base accessor

`base` is a getset on the interned-atom base type returning `self`,
overridden on the subtype to return the base symbol. Every site that
wants the bare spelling of an atom writes `x.base` with no type test,
whether or not the atom is scoped.

`strip_marks` in `koskari/types.py` stays. It projects every atom in a
form to its base and is what `check_rewrite_cycle`
(`koskari/compile/compiler.py:954`) compares, since two expansions of
one cyclic macro carry different marks and compare unequal
element-wise. `strip_mark` and `is_mark` go.

## Resolution

Unchanged. `resolve_marks`, `mark_candidates`, `_choose_key`, the
module-dictionary rung, the instruction-stream walk with its inline
scope stack, the `NamespaceSpec` rebuild, and `note_mark` all stay and
all keep their current behaviour. They read `base` and `marks` where
they read `datum` and `marks`, and they read the name where they call
`str()`.

`mark_candidates` formats its candidate strings from the base name and
successive mark prefixes directly, without allocating intermediate
gensyms.

Three behaviours the branch's reproducers hold and this design keeps:
a free template reference falls back to the use-site name even when a
sibling scope in the same expansion bound the same spelling
(`tests/reproducers/test_mark_hygiene.py:175`); an inner template
reaches the outer binder in a macro-generating macro (`:206`); and a
template's `define-global` binds under its marked spelling with the
bare name absent from the namespace (`:306`). The marks tuple, the
intermediate rungs of the ladder, and the per-row spelling decision
each exist for one of these.

## What the matcher loses

In C, all of it. `matcher_peel_datum` drops its `Mark` branch
(`koskari/lib/matcher/datum.c:444`), `matcher_peel_datum_keep_mark`
(`:459`) is deleted along with its two callers in
`koskari/lib/matcher/subject.c:134` and `:161`, and `pair_elem_str`
drops its `Mark` case (`koskari/lib/pair.c:420`).

In `koskari/matcher.py`, the eighteen `Mark` references reduce to five
`.base` reads, at the matcher-factory lookup, the two quoted-symbol
literals, the extern class name, and the bare literal form. The rest
are deletions: the type tests become plain `is_symbol` and
`is_keyword`, and the keyword option parsers need nothing because
keywords are no longer scoped.

Slot names keep the derived spelling. `CAPTURE(str(...))` at
`koskari/matcher.py:2341`, `:3134` and `:3164` carries no peel today
and continues to produce the scoped spelling, which is what
`koskari/compile/specials/match.py:145` turns into declared names at
`:173`, and what `koskari/syntax.py:719` relies on so a
template-introduced pattern variable and a use-site one stay distinct.

## Ordered type tests

A gensym satisfies `isinstance(x, symbol)`, so every ordered type test
with a `symbol` arm now catches one. Two must be reordered or
normalized; both are silent otherwise.

`CONST_TYPES` (`koskari/compile/compiler.py:55`) lists `symbol`, and
`_qq_expr` tests `_is_const` at `koskari/compile/specials/quasiquote.py:72`
before it peels at `:77`. Quasiquote projects to the base once at the
top of `_qq_expr` and of `_compile_element`, which also replaces the
four separate peels at the identity comparisons below them.
`special_quote` (`koskari/compile/specials/pairs.py:75`) projects the
same way. Without this a scoped atom is emitted as a constant and
folded into a tuple constant in the pool.

`encode_pool_value` (`koskari/serialize/codex.py:1067`) tests `symbol`
before the mark arm at `:1074`. The gensym arm moves above the symbol
arm.

## Keywords

Keywords are not subtyped and carry no marks. `mark_atom` returns a
keyword unchanged, and the keyword arm of
`expand_template_with_hygiene` disappears into the existing
non-pair return.

Every `is_keyword(strip_mark(x))` becomes `is_keyword(x)`, and every
layout test of `:block:` and `:splice:` keeps its `is` comparison
unchanged, because those keywords are interned again.

This removes a live defect. `gather_formals` passes an unstripped
keyword into `_strip_byref_kw` (`koskari/compile/helpers.py:1358`,
`:1382`), whose `str(kw)` on a `Mark` yields the mangled spelling, so a
template-written by-ref keyword formal binds under a keyword named
`acc#hex` and records that string in `by_refs`. With plain keywords
both are correct.

## Expansion

`mark_atom` (`koskari/hygiene.py:52`) appends a mark to an
already-scoped atom, as it does today. It does not rebase. The binder
and the reference to it do not always travel together: in a
macro-generating macro the binder sits in the outer template and the
reference in the inner one, and a procedural handler that rebuilds a
capture through a `Datum`'s raw accessors hands already-scoped
material back to `mark_expansion` from inside the scope that bound it.
The intermediate rungs of the candidate ladder resolve both.

`_binding_key` (`koskari/hygiene.py:69`) and its four call sites
collapse to the atom itself, because base equality answers the
bindings lookup. `_collect_symbols` drops both `is_mark` arms.

`peel_datums` (`koskari/hygiene.py:312`) is `Datum`-driven and never
inspects an atom's type. Only its docstring changes.

`_finalize_macro_output` keeps its order, layout before marking.

## Layouts

Seventeen sites across six files. The keyword tests need nothing. The
symbol clause heads convert from `is` to `==`, thirteen comparisons in
three files: `except`, `else` and `finally` at
`koskari/reader/layouts/exceptions.py:61`, `:89` and `:100`; `elif`
and `else` at `conditionals.py:76` and `:89`; `case` and `else` at
`conditionals.py:170`, `:185`, `:318` and `:338`; `when` and `unless`
at `conditionals.py:222` and `:223`; `before` and `after` at
`dyn_wind.py:69` and `:82`. Each strips into a local and then compares
against a module-level symbol constant. Left as `is`, a
template-written `try`, `if`, `match` or `dyn-wind` block falls to its
else arm and raises a layout error.

`_split_case_guard` (`koskari/reader/layouts/conditionals.py:219`)
appends the peeled element into the emitted pattern rather than only
testing it, so it projects with `.base`.

`_lower_block`'s `reader.find_layout(str(head))`
(`koskari/reader/layouts/__init__.py:174`) projects with `.base`,
since a layout is found by its bare name.

## Site conversions

`str(strip_mark(x))` becomes `str(x.base)` wherever the bare spelling
is wanted: the matcher-factory and type-name lookups, the attribute
name in `koskari/compile/specials/refs.py:76` and
`object.py:1079`, the slot name in `slots.py:57`, the method name in
`call.py:270`, and the annotation key in `helpers.py:293`.

`is_symbol(strip_mark(x))` and `is_keyword(strip_mark(x))` become the
bare check. `is_symbolish` (`koskari/compile/helpers.py:191`) and
`_is_source` (`koskari/compile/specials/fence.py:50`) each lose their
`is_mark` arm.

`strip_mark(x) is CONST` becomes `x == CONST`. The cost falls on every
negative test, not only on a scoped operand:
`PyObject_RichCompareBool` short-circuits on pointer identity for a
match, and a mismatch between two plain interned symbols runs the full
comparison. The sites are the head-dispatch tests in `conditionals`,
`match` and `quasiquote`, the `gensym-thunk` monitor, and the layout
clause heads above.

Three sites need more than a shape swap:

- `koskari/compile/specials/variables.py:337` de-scopes a `define` in a
  class body so the attribute binds bare. It becomes `str(namesym.base)`.
- `_marked_bind` (`koskari/compile/specials/imports.py:190`) builds a
  spelling over a new base carrying an existing marks tuple. It becomes
  `KosGensym_New(symbol(bind), atom.marks)`.
- `gensym-thunk` keys its placeholder map by the bare atom so a `%1`
  written by a template is the same slot as one written by hand
  (`koskari/compile/specials/thunk.py:81`). `_thunksym` matches
  `^%[1-9][0-9]*$` (`:39`) and `_build_formals` parses the index out of
  the spelling, so both read `.base`.

`frame_formal_name`'s `&` path (`koskari/matcher.py:1896`) needs no
change beyond dropping the `isinstance` arm: `str(n)[1:]` yields the
derived spelling without the ampersand, and `symbol()` of it keys the
same binding, as it does today.

`by_refs` (`koskari/compile/helpers.py:1339`) needs no change. It holds
`str(name)`, the formals name tuples hold the same spelling, and
`PySequence_Contains` in `koskari/lib/formals.c` matches them.

## Serialization

Pool kind `MARK` (24) becomes `GENSYM` (24): base index plus the marks
tuple, packed `<Q>` per mark as now, since a mark is a 32-bit random
base plus an unbounded counter. The gensym arm precedes the symbol arm
in `encode_pool_value`. `intern_for_pool`
(`koskari/serialize/codex.py:760`) gains an arm that interns `base` as
a dependency. The decoder at `:1176` builds through the exposed
constructor. The `MARK` note in `koskari/serialize/constants.py:66`
takes the new name.

## Tests

`tests/lib/test_mark.py` becomes `tests/lib/test_gensym.py`: subtype
membership, the derived spelling, hash and equality including
equality to the base and inequality between two mark tuples, that two
constructions with one base and one marks tuple yield one object, and
that deallocating a gensym leaves its base interned and its own entry
removed.

`tests/matcher/test_marked_subject.py` reduces to the two facts the
matcher still owns: a gensym satisfies `SYMBOL` and `TEST_LITERAL`
against its base, and is handed back as itself.

`tests/compile/specials/test_marked_forms.py` and
`tests/reproducers/test_mark_hygiene.py` keep their behaviours and
their assertions, spelled with gensyms.

`tests/serialize/test_kio_roundtrip.py` round-trips a gensym and
asserts the decoded object is a gensym with the same base and marks,
which the reordered encoder arm is what makes true.

`tests/lib/test_atom.py` needs no change: `gensym` keeps its spelling
and its uniqueness.

One new reproducer covers quasiquote over a template-introduced
symbol, asserting the runtime value is the bare interned symbol by
exact type, since an assertion on `str` alone passes for a gensym.

## Feedback

Review against the `hygiene-rework` implementation. The subtype is
the right shape. Equating a gensym with the symbol it spells is not:
it breaks equality, and it undoes the mark distinction this branch
uses to keep a template binding apart from the use site.

### Equality

A gensym equals its base, two gensyms are equal only when base and
marks both match, and the hash is the hash of the base. Interning
makes one base and one marks tuple one object, so the only new true
result is "gensym equals base."

That relation is not transitive. Two results of `gensym("t")` both
equal `symbol("t")` and both hash as `symbol("t")`, and they are not
equal to each other. Dict and set lookup assume that if `a == b` and
`b == c` then `a` and `c` are the same key:

- `{symbol("t"): 1}[g1]` hits
- `{g1: 1}[symbol("t")]` hits
- `{g1: 1}[g2]` misses

`tests/lib/test_mark.py` asserts the opposite, inequality against
`symbol("t")`. `tests/lib/test_atom.py` checks object identity only,
so those tests stay green while `gensym("t")` stops being a distinct
key from `t`. The collision loop exists to keep the derived name
unique. Equality with the base drops uniqueness on the axis callers
use.

The same rule hits two lookups this branch made precise.

Pattern bindings are keyed by `symbol(slot_name)`
(`koskari/syntax.py:503`). For a template-introduced pattern variable
the slot name is the derived spelling, and because the gensym is
interned under that name, `symbol("t#hex")` is the gensym. Returning
the atom from `_binding_key` finds that entry by identity. Base
equality is not what makes the hit. It adds a second one: the bare
symbol `t` is an equal key, so an outer template's `t#hex` is treated
as the inner pattern's `t`. That is the collision the comment at
`koskari/syntax.py:719` is there to prevent.

The literals test is the same operator. `pat in literals` at
`koskari/syntax.py:674`, `:723`, and `:745`, and `expr in literals`
in `collect_pattern_variable_symbols`, is equality. A marked pattern
variable whose base sits in the literals set becomes a literal. A
`Mark` today is not equal to its datum, so a template-written `else`
stays a pattern variable when the use site listed `else`.

### Printing and quote-syntax

`str` of a form is source. `pair_elem_str` prints the atom under a
`Mark` (`koskari/lib/pair.c:420`), and `Mark.__kos_repr__` prints the
datum. `test_printed_form_shows_the_atom` expects `str` of
`(let#1 t#1 1)` to be `(let t 1)` while `str` of the mark itself
stays `t#1`.

Inherited symbol `str` and `__kos_repr__` both print the name field.
This design sets that field to the derived spelling and drops the
`Mark` case in `pair_elem_str`. Form `str` and `kos_repr` then show
`t#hex`. Keep a `__kos_repr__` that prints `.base`, and keep the pair
printer on that path. `str` of the gensym object stays the derived
name.

`quote` and quasiquote must project to `.base` before `_is_const`.
That part is right: `CONST_TYPES` includes `symbol`, and `_qq_expr`
tests `_is_const` at `koskari/compile/specials/quasiquote.py:72`
before the peel at `:77`, so a gensym would be emitted as itself and
folded into a pool tuple.

`quote-syntax` must not project. `special_quote_syntax`
(`koskari/compile/specials/pairs.py:84`) exists to keep marks on the
patterns and templates of a macro-generating macro. This design names
`special_quote` and not that one. Projecting inside `_qq_expr` is
safe only because `quote-syntax` does not go through it.

### Resolution is not a field rename

`resolve_marks`, `mark_candidates`, and `_choose_key` can keep their
behaviour. The entry points that feed them are `is_mark` tests, and
those are absent from the site list:

- `request_var`, `request_for_write`, `request_global`
  (`koskari/compile/compiler.py:180`, `:193`, `:206`)
- `compile_symbol` (`:1045`)
- `find_compiled` (`:844`)
- the keyword-mark unwrap at the top of `compile_expr` (`:1007`)

Left as `is_mark`, nothing is noted and the candidate ladder never
runs. Once `.base` on a plain symbol returns `self`, the test is
`sym.base is not sym`.

`mark_candidates` building strings from the base name and mark
prefixes, without allocating intermediate gensyms, is a direct
improvement on the current `Mark(datum, marks[:n])` loop.

### Mismatches with the code

There is no interned-atom base type. `KosSymbolType` and
`KosKeywordType` are sibling types. A getset on the symbol type does
not appear on keywords, and `strip_mark` accepts both. `.base` is not
a drop-in for every `strip_mark` call. Numbers and keywords have no
such attribute. `special_quote` and the non-pair arm of quasiquote
peel those too.

`symbol_dealloc` sets `ob_refcnt = 3` before `PyDict_DelItem` so the
dict's decref does not re-enter dealloc (`koskari/lib/atom.c:147`).
A gensym dealloc has to do that, then clear `base` and `marks`.
Calling `symbol_dealloc` deletes the derived-name intern entry, which
is the right entry, and leaks `base` and `marks`. It does not delete
the wrong entry.

`KosPairType` carries `Py_TPFLAGS_HAVE_GC`, not only the default
flags (`koskari/lib/pair.c:1580`). Static subtypes still do not need
`Py_TPFLAGS_BASETYPE`; `KosNilType` shows that. The flag conclusion
holds.

The matcher's five `.base` reads miss places that emit the peeled
value. `lower_anon_constraint` (`koskari/matcher.py:2439`) turns the
payload into a `TYPE` name or a `LITERAL`. The class-pattern field
name is `str(tag)` after `tag = strip_mark(elem)` (`:3118`) while
`CAPTURE(str(elem))` at `:3134` keeps the derived spelling. Deleting
those peels changes field names and type names. The `CAPTURE` lines
themselves can stay.

`_PYCONS_DIRECT` lists `symbol` and `Mark`
(`koskari/compile/helpers.py:178`). A gensym passes
`isinstance(..., symbol)`, so the `Mark` entry is redundant. The
try-in-template reproducer keeps working.

Leaving keywords unmarked is sound, and it is independent of the
equality rule. `_strip_byref_kw` uses `str(kw)` with no peel
(`koskari/compile/helpers.py:1206`), so a marked `&acc` becomes the
keyword `acc#hex`. A keyword formal has to stay the name the caller
writes. `test_formals_keyword_headed_cell_keeps_mark` and the
`Mark(keyword(...))` half of the kio roundtrip encode the old
behaviour and have to be retired.

The current gensym counter is the bouncing `gen_counter` in
`koskari/lib/atom.c`, not `EXPANSION_MARK_BASE`. One monotonic
counter still spells `t#hex`. The predicate loop has to reject a
derived name that is already interned before it is published, and a
rejected candidate has to be dropped so dealloc removes that intern
slot. Weak interning makes a held reject permanent.

### A smaller version of the same type

`KosGensym` subtypes symbol, with `base`, `marks`, and the derived
name in the inherited name field. Intern it under that name. Leave
`tp_richcompare` unset, so equality stays identity, as it does for
symbols today.

- `is_symbol` and `KosSymbol_Check` accept it, and the `Mark` peels
  in the type tests go away.
- `_binding_key` returns the atom. `symbol(slot)` is that interned
  object, and a bare `t` is a different key.
- `pat in literals` stays false for a gensym whose base is listed.
- Clause heads and other `strip_mark(x) is CONST` sites become
  `x.base is CONST`, still a pointer compare. This design already
  touches each of those sites.
- `TEST_LITERAL` compares bases by pointer. That replaces the `Mark`
  arm of `matcher_peel_datum`. `Datum` peeling stays. Captures
  already return the subject, so a gensym comes back as itself.
- Quasiquote and `quote` project `.base` before the const check.
  `quote-syntax` stores the gensym. The encoder's gensym arm stays
  in front of the symbol arm.
- `note_mark` runs when `sym.base is not sym`.
- `__kos_repr__` and the pair printer use `.base`.

Same type, same spelling, same resolution ladder, without a second
equality for symbols.
