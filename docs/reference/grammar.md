# Grammar

Koskari source combines **layout syntax** (indentation-based blocks and lines
in `.ki` files) with **s-expressions** (parenthesized forms). The reader turns
both into list structure; the compiler then expands macros and dispatches on
special forms.

---

## S-expression forms

<!-- content TBD: base s-expression grammar; see tutorial examples in guide and start tracks -->

Parenthesized calls, literals, and collection syntax work inside delimiters and
at the top level. See [Forms and combining](../guide/forms-and-combining.md)
for everyday shapes.

---

## Suffix syntax

Three pieces of syntax attach to the **value just read**, wrapping it rather
than starting something new. They read left to right and chain freely with each
other:

| Written | Reads as |
|---|---|
| `foo[0]` | `(item foo 0)` |
| `grid[y][x]` | `(item (item grid y) x)` |
| `obj.value` | `(compile/attr obj value)` |
| `(get-fn x).baz` | `(compile/attr (get-fn x) baz)` |
| `("hello".upper)` | a method call on a literal |
| `foo[0].bar` | `(compile/attr (item foo 0) bar)` |
| `"a"- " b"` | `(compile/#str "a" " b")` |

**Attachment is strict.** The suffix character must follow the value with no
space between them. A space ends the value instead, which is what keeps
`foo [a]` two separate forms and leaves the leading-dot method call
`(.baz obj)` alone.

**Index brackets** (`foo[k]`) read an item access, and a group may carry
several keys (`foo[1 2]`). An empty group is an error.

**Dots** read an attribute. This is the same expansion whether the dot sits
inside a single name (`obj.value`) or attaches to a form or literal
(`(get-fn x).baz`, `"".join`) — the dot is the attribute read, and enclosing
parentheses are what make it a call. A dot with no name after it is an error.

**A trailing hyphen on a string** continues that literal into the next quoted
segment, so a long string can be broken across lines without restructuring the
call around an explicit join:

```koskari
(print "This is one"-
       " long string broken"-
       " up across lines")
```

Segments may be separated by any whitespace, including newlines and blank
lines, and comments in between are skipped. Each segment carries its own
prefix, so an `f` segment interpolates and an `r` segment stays raw. All-literal
chains fold to a single constant, so the join costs nothing at run time.

Each of the three is governed by its own setting — `reader/index-brackets`,
`reader/dot-attrs`, and `reader/string-segments`. See
[Pragmas and feature settings](concepts/pragma.md) for turning them off and for
the exact rules at the edges.

---

## Layout forms

Layout is the default surface at the top level of a module. Rules for blocks,
lines, colon handling, and handler resolution live in dedicated reference pages:

- [Layout and s-expressions](concepts/layout-and-s-expressions.md) — mental model
  and three authoring surfaces
- [Reader and layout syntax](reader-layout.md) — normative syntax rules

---

## Layout handlers

Per-head read-time lowering (`layout/if`, `layout/block`, …) is documented in
the [Layout handler reference](layouts/index.md).
