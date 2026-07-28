# Koskari Design: Suffix Reader Macros

## Status

Proposal. Nothing here is implemented.

The durable part of this design is the **mechanism** -- a registry and
driver for reader macros that attach to a value already read, so that
postfix syntax is an extension point rather than a special case. The two
features specified below exist to exercise that mechanism and to make
everyday Koskari less cramped; either could be dropped without touching
the mechanism.

## Summary

Koskari already has one postfix reader feature: an `[ ... ]` group
attached to a just-read value desugars to `(item VALUE KEY)`. It is
implemented as the sole registrant of a general `post_value_hooks` chain,
and it consumes its own suffixes in an internal loop.

That arrangement does not compose. Two hooks that each loop to exhaustion
cannot interleave, so `a[0].b[1]` would read as `(item a 0)`, then an attr,
then leave `[1]` dangling as a separate form. Any second postfix feature
either merges into the first handler or breaks chaining.

This design replaces the hook chain with a **suffix macro registry** keyed
by trigger character, and a driver that applies at most one suffix per
pass and re-enters with the result. Handlers become small and independent,
chaining across different suffix kinds falls out of the driver, and third
parties can register their own.

It then specifies two suffix macros built on it, each gated by its own
pragma:

* **dotted attributes** -- `.name` attaches to any value, so
  `("".join "a" "b" "c")` and `((get-fn x).baz)` work
* **string segments** -- a trailing `-` continues a string literal into
  the next quoted chunk, so a long string can be broken across lines

## Motivation

Both features address the same friction, from opposite directions.

The dotted accessor exists today only as an atom rule. `foo.bar` reads as
`(compile/attr foo bar)`, but the moment the receiver is a form rather
than a name, the postfix spelling is unavailable and the leading-dot form
is the only option:

```koskari
(.baz (get-fn x))     ; works
(get-fn x).baz        ; does not read as an attribute access
((get-fn x).baz)      ; and so this does not read as a method call
("".join "a" "b")     ; nor this
```

The dot is the access and the enclosing parens are the call, as everywhere
else: `foo.bar` reads an attribute, `(foo.bar)` calls what it read.

The index brackets already proved the postfix spelling is worth having;
`foo[5]` reads better than `(item foo 5)` in the same way `(x.baz)` reads
better than `(.baz x)`. Extending the rule from atoms to values closes a
gap that has no principled reason to be there.

String segments address line width. Koskari strings are multi-line, so a
long string *can* be broken, but the break captures the newline and the
following indentation. Today the workaround is explicit:

```koskari
(#str "This is one"
      " long string broken"
      " up across lines")
```

which is fine but heavy for what is conceptually one argument, and forces
the author to restructure the call to introduce it.

## Part 1: The suffix macro system

### Current state

* `post_value_hooks` is a plain list on the reader
  ([parser.py](../koskari/reader/parser.py)), with
  `add_post_value_hook`, `get_post_value_hooks`,
  `clear_post_value_hooks`, and a `temporary_post_value_hook` context
  manager.
* The driver is three lines in the read loop: on a `VALUE` event, every
  hook is called in registration order and its return replaces the value.
* `_apply_index_brackets` is the only registrant anywhere in the tree, and
  nothing in `tests/` registers a hook. The mechanism is therefore free to
  be redesigned rather than migrated.
* That handler self-gates on `self.index_brackets` (set from the
  `reader/index-brackets` pragma and mirrored onto `_inner`), then loops
  `while stream.peek(1) == "["`, reading each group with `read_pair` and
  wrapping into `(item VALUE KEY ...)`.

### Registry and driver

Replace the hook chain with a registry that mirrors `add_event_macro`, so
the reader has one dispatch model rather than two:

```python
add_suffix_macro(char, macro_fn, predicate=None)
```

Entries are **prepended** to that character's stack, newest first, exactly
as character event macros are. The driver, on a `VALUE` event:

1. `peek(1)` at the current stream position.
2. Look up that character's stack. If the character is unregistered, stop.
3. Try each entry in order. A `predicate(stream, char, value)` that
   returns false skips the entry without calling it.
4. A handler returns either a new value, or `DEFER` to decline. On
   `DEFER`, continue to the next entry; when the stack is exhausted, stop.
5. On a new value, **re-enter from step 1** with that value.

Chaining across different suffix kinds is then a property of the driver
rather than a rule any handler implements: `foo[0].bar[1].baz` works
because each pass consumes whichever suffix is adjacent.

### Handler contract

A suffix macro is `(stream, value, position) -> value | DEFER`, and must:

* **Consume exactly one suffix.** No internal loop. The driver re-enters.
* **Advance the stream when it applies.** A handler that returns a value
  without consuming input spins the driver forever.
* **Not advance beyond peek when it declines.** Same contract the event
  macro chain already documents for `DEFER`.
* **Set a position on any form it builds**, as `_apply_index_brackets`
  does today.
* **Raise rather than `DEFER` once it has consumed input.** `DEFER` means
  "this is not mine," which is only answerable before committing. A
  handler that has consumed its trigger and then finds malformed input
  owes a read error, not a silent decline. Deciding whether a suffix is
  claimable belongs in the predicate, where the receiving value is
  available.

The termination requirement is the one worth enforcing rather than
documenting, because third-party handlers will get it wrong: the driver
records the stream position before dispatch and raises if a non-`DEFER`
return did not advance it. That converts a hang into a diagnosable error.

### Re-entrancy on nested reads

A handler that reads a nested value through the general value reader gets
the driver applied to that nested value. For index brackets this is
correct and desirable -- the group is read as a whole, and suffixes inside
it should bind inside it.

It is a hazard for any handler whose nested read should *not* take
suffixes of its own. The string segment handler is exactly that case: if
it reads the following segment through the general reader, then

```koskari
"a"- " b".join
```

comes back as `(compile/attr "b" join)` and the `.join` binds to the
segment instead of to the joined result. The handler must read the
following segment through the string readers directly (`_read_string`,
`_read_fstring`, `_read_rstring`, `_read_bstring`).

This belongs in the handler contract, because every future suffix macro
author meets it.

### Gating

Existing reader features gate two different ways: `reader/index-brackets`
and `reader/fences` self-gate on a mirrored flag, while `reader/pass-none`
installs and removes an atom pattern outright via `set_pass_none`.

For suffix macros, prefer the **predicate** slot. It is already in the
registration signature, it keeps a disabled feature from being entered at
all rather than entered and immediately returned from, and it leaves the
registration stable across pragma changes so ordering within a character's
stack does not shift when a pragma is toggled mid-module.

### API surface

`post_value_hooks` and its four methods are removed. `_apply_index_brackets`
is rewritten as a registered suffix macro on `[` that consumes one group.
A `temporary_suffix_macro` context manager replaces
`temporary_post_value_hook` if anything needs it; nothing does today.

## Part 2: Dotted attribute suffix

### Surface

A `.` immediately following a value, with no intervening whitespace,
reads an attribute name and wraps into `(compile/attr VALUE NAME)`:

```koskari
(get-fn x).baz               ; (compile/attr (get-fn x) baz)
foo[0].bar                   ; (compile/attr (item foo 0) bar)
obj.a.b                      ; left-associative, as the atom rule already is
```

The suffix reads an attribute and nothing more. A call is the enclosing
parens, unchanged:

```koskari
((get-fn x).baz)             ; method call, no arguments
("".join "a" "b" "c")        ; method call, three arguments
```

Whitespace breaks the attachment, so `.baz` after a space remains an
ordinary symbol and `(.baz (get-fn x))` is untouched.

### Relationship to the atom rule

`foo.bar` is read today by an atom *pattern*, not by any postfix
machinery: the whole atom is scanned, then matched against an ordered
pattern list, and `_dotted_attr_atom_re` converts it left-associatively to
nested `(compile/attr ...)`.

Unifying the two into a single parser path would require `.` to split
atoms, so that `foo` scans and the suffix handler reads `.bar`. Two
existing atom patterns own `.` and would have to be carved back out by
predicate: `FLOAT_RE` for `1.5`, and the ellipsis pattern for `...`.
Trading a clean split for two numeric special cases in a boundary
predicate is a bad exchange, so **the two scanners stay separate**.

What they share is a single chain-building helper and a single pragma. The
atom pattern is installed and removed from the pragma exactly as
`set_pass_none` does for `pass`, and the suffix macro's predicate consults
the same setting, so one switch governs both spellings and neither can
drift from the other.

The suffix handler must accept the same name characters the atom regex
does. That regex admits `/` and `-` inside a name, so `foo.bar-baz` and
`foo.a/b` are attribute accesses today and must stay so.

### A dot with no name is an error

With the pragma on, a dot not followed by a name is a read error, in both
spellings: `foo.` at end of line, `foo.[0]`, `foo..bar`, `(f x).` and so
on.

The suffix path reaches this directly -- the handler consumes `.` and
finds no name. The atom path does not, and this is the trap: `foo.` scans
as a *single atom*, `_dotted_attr_atom_re` declines to match it, and it
falls through the remaining patterns to become the symbol `foo.`, which is
what it reads as today.

Rather than bolt a separate check onto the atom path, **widen the regex to
match the malformed forms too** and let the conversion function reject
them. The current pattern is deliberately narrow, describing only the
known-good shape:

```python
^[a-zA-Z_][a-zA-Z0-9/_-]*(\.[a-zA-Z_][a-zA-Z0-9/_-]*)+$
```

Relaxing the requirement on each trailing segment -- so an empty or
otherwise malformed segment still matches -- brings `foo.`, `foo..bar`,
and `foo.9x` into the pattern, and `_as_dotted_attr_atom` inspects the
split parts and raises on any part that is not an identifier. The payoff
is a specific message: the conversion knows exactly which segment is bad,
so the error can name it instead of surfacing as a generic failure
somewhere downstream.

Two constraints on the widening:

* **The leading `[a-zA-Z_]` anchor must stay.** Atom patterns are tried in
  registration order and the dotted pattern is registered *before* both
  ellipsis and float, so only that anchor keeps `1.5` and `...` from
  reaching it.
* **The pattern is installed and removed with the pragma**, as the rest of
  this feature is. With `reader/dot-attrs` off, the pattern is absent and
  `foo.` falls through to the symbol it is today. The error exists only
  where the feature does.

Raising is enough to get a positioned error: `process_atom` is called
inside a `try` that converts a `ValueError` into a reader error carrying
the atom's position, so the conversion function needs no access to the
stream. Two other call sites -- dict-key re-reading and f-string
expression atoms -- sit outside that wrapper, and a malformed dot reached
through either would surface as a bare `ValueError` with no position.
Worth covering when the widening lands.

### Compiler impact: none

`compile/call` already pattern-matches a `(compile/attr obj sym)` head and
rewrites it to `(compile/call-attr obj attr args...)`, for both the `attr`
and `compile/attr` spellings ([call.py](../koskari/compile/specials/call.py)).
So `((get-fn x).baz a)` lowers through the ordinary method-call path and
picks up the `CALL_FAST_ATTR` family where the receiver qualifies. No
special or opcode work is required.

### Pragma

`reader/dot-attrs`, baseline **on**. Off, `.` is inert in both spellings:
attribute access is written `(compile/attr obj name)` or `(.name obj)`.

Baseline-on changes the reading of existing source in one case: a `.name`
written immediately after a closing delimiter, which today reads as two
adjacent forms. That spelling has no meaning today -- `("" .join)` is a
string followed by a symbol -- so the risk is accepted.

## Part 3: String segments

### Surface

A `-` immediately following a string literal's closing quote marks the
literal as continuing. The reader skips whitespace, including newlines,
reads the next string literal, and wraps the sequence:

```koskari
(print "foo bar"-
       f" {qux}")

; reads as
(print (#str "foo bar" f" {qux}"))
```

The hyphen is **strictly adjacent** to the closing quote. `"foo" -` with a
space intervening is a string followed by a minus, and stays that way.

### What may follow the marker

Between the marker and the next segment there may be any amount of
whitespace, including none. Segments need not be on separate lines:
`"foo"-" bar"` and a segment three lines down are the same construct.
Comments in that span are skipped as though absent from the stream
entirely.

What follows must be a **quoted string literal** -- plain, `f`, `r`, `b`,
or triple-quoted. Arbitrary expressions do not join, so there is no
`"foo"- (compute)`; that is what `#str` itself is for.

Anything else is a read error naming the unjoined segment. The marker is a
commitment: once `"foo"-` is written, a segment must follow.

### Declining versus raising

The two outcomes are distinguished by what the marker is attached to,
which matters because the wrong split would make meaningful text an error.

* The **receiver is not a string** -- the predicate declines and the
  marker is never consumed. `(foo)-` remains a form followed by a minus,
  exactly as it reads today. This is the predicate's job, not the
  handler's.
* The **receiver is a string** -- the handler commits, consumes the
  marker, and raises if no segment follows.

So a handler may raise once it has consumed input. `DEFER` is for
declining before committing; it is not an escape hatch from a
half-consumed suffix.

Each segment carries its own flavor prefix, exactly as Python's implicit
concatenation does. There is no inheritance rule: an `f` segment
interpolates, an `r` segment is raw, and a `b` segment is bytes,
regardless of what the opener was. Mixing `b` with the others is not a
reader error; it fails where `#str` fails.

The wrap is the same shape of transformation the other suffix macros
perform, which is the point: the segment handler builds a form and returns
it, and everything downstream is ordinary.

### Why the marker trails

A leading marker on the continuation line reads better -- each line
self-identifies, and the reader does not have to look up to know what it
is holding. It is nonetheless the wrong choice here.

With the marker at the end of the opening line, the reader learns that the
line continues *before* it processes the newline. With a leading marker,
the newline has already been processed, and in layout mode the block
structure has already been decided, by the time anything sees the marker.
This is the same reason explicit line continuation is conventionally a
trailing token rather than a leading one.

### Why a hyphen

A trailing hyphen is the typographic convention for a **word** broken
across lines. That association is already scoped to text, so it says "this
text continues" rather than "this line continues," which is exactly the
boundary the marker needs to respect.

A trailing backslash was the obvious candidate and is deliberately
rejected. Every language that uses it uses it as a *general* newline
escape, so spelling a string-only feature that way invites the reader to
expect

```koskari
foo bar \
    baz
```

to mean something. Koskari already contends with the belief that an
indented following line continues the preceding form; a marker that
appears to generalize would reinforce precisely that misconception. The
familiarity that makes backslash attractive is the same property that
makes it wrong here.

Strict adjacency follows from the choice. `"foo"-` cannot be read as
arithmetic, because a minus in that position would need a left operand and
Koskari has no infix subtraction; `"foo" -` with a space can be, and so is
left alone. Requiring adjacency also keeps the driver's dispatch a plain
`peek(1)`, with no provision for markers behind horizontal whitespace.

### Cost: none at runtime

`#str` is bound in bootstrap and compiles through the `compile/#str`
special, which reduces via `_reduce_op` with a Python fold. That fold
consumes leading constant arguments into a single `CONST`, and a lone
folded constant is emitted as a bare `CONST` with no `OBJECT_STR`
instruction at all ([object.py](../koskari/compile/specials/object.py)).

So an all-constant segment chain compiles to exactly the single string
constant that writing it on one line would have produced. Folding in the
reader would duplicate a fold that already exists, so the suffix always
wraps, with no special case.

Note that the fold consumes *leading* constants only, so a chain whose
first segment interpolates does not fold its later constant segments. That
matches the existing behavior of hand-written `#str` and f-strings, and is
out of scope here.

### Docstrings

Out of scope. Docstring detection is a read-time `isinstance(head, str)`
on the body head, so a wrapped form is not a docstring. This feature is
for string arguments at width, and a long docstring uses the triple-quote
spelling, which already works.

### Pragma

`reader/string-segments`, baseline **on**. Off, `-` after a string is
inert and the string ends there.

## Pragma summary

| Setting | Baseline | Governs |
|---|---|---|
| `reader/dot-attrs` | on | `.name` as an atom rule and as a value suffix |
| `reader/string-segments` | on | trailing `-` continuing a string literal |

Both register in `koskari/reader/pragma.py` alongside the existing
`reader/*` settings, and both are applied in `reload_pragma`, mirrored
onto `_inner` as the others are.

## Accepted risk

**Layout interaction.** The segment handler consumes newlines inside what
the outer reader sees as a single value, so the layout reader must derive
its column and block state from the stream position after the value
completes, rather than from a line counter the handler would bypass.
Multi-line string literals and multi-line s-expressions already require
this, so the expectation is that it holds unchanged. It is not verified.
The decision is to proceed and backfill a fix if step 3 finds otherwise,
rather than to gate the design on it.

## Open questions

**Pragma naming.** `reader/dot-attrs` and `reader/string-segments` are
accepted as written. Both name the effect rather than the syntax, which
makes them inconsistent with `reader/index-brackets`. Renaming a setting
is cheap, and the intent is a single uniformity pass over every pragma
name before beta -- effect versus syntax, noun-verb versus verb-noun --
rather than settling it one setting at a time. Recorded here so that pass
has the pointer.

## Implementation order

1. Suffix macro registry, driver, and termination guard. Rewrite
   `_apply_index_brackets` onto it. No behavior change, existing tests
   hold.
2. Dotted attribute suffix plus `reader/dot-attrs`, sharing a chain
   builder with the atom pattern and gating both.
3. String segments plus `reader/string-segments`.

Each step is independently landable, and step 1 is worth landing on its
own merits regardless of whether either feature follows.

## See also

* [PRAGMA.md](PRAGMA.md) -- pragma registry, watchers, and the reader
  settings these join
* [SYNTAX.md](SYNTAX.md) -- reader surface syntax
* [fenced-foreign-literal.md](fenced-foreign-literal.md) -- the other
  reader feature that lowers to an ordinary compiler form
