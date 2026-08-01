# Koskari Design: Bracket suffixes in f-string interpolation slots

## Status

Resolved. The slot reader now applies suffix adjacency before the value/spec
split: the bare-atom branch of `_read_format_field` runs
`_dispatch_suffix_macros` on the head atom, so `{foo[0]}` reads as a single
subscript form while a space-separated or `:`-introduced format spec still
parses. Attachment stays gated on `reader/index-brackets`, matching normal
expression context, which also settles the first open question below (any
registered suffix, not just `[`, attaches in a slot). Tests in
`tests/reader/test_format_string.py`.

## Summary

A bracket suffix written bare in an f-string interpolation slot does not
become a subscript. The `[...]` is read as a *separate form* and lands in the
slot's second position, which is the format-spec slot, so `f"{foo[0]}"`
compiles to the equivalent of `format(foo, [0])`. The same suffix inside any
enclosing parenthesized form reads correctly.

## Reproduction

```
>>> 'f"{foo[0]}"
(compile/#str (format foo [0]))
>>> 'f"{(+ 1 foo[0])}"
(compile/#str (format (+ 1 (item foo 0))))
```

The second line shows the adjacency rule working as intended: inside `( )` the
reader is in normal expression context, `foo[0]` attaches, and the result is
`(item foo 0)`.

## Diagnosis

An interpolation slot reads its contents as a **sequence of forms**, not as a
single expression. That is deliberate: the second form is the format spec, and
that surface is in use today —

```
out.write f"""  {i "3d"}: {(_format_constant val)}\n"""
```

reads as value `i` plus spec `"3d"`. Because the slot splits on form
boundaries, `foo` and `[0]` arrive as two forms and the bracket-suffix
adjacency rule never gets a chance to fire. The `[...]` is then interpreted
positionally, as the spec.

So the two features collide at exactly one point: adjacency is a rule about
*juxtaposition without whitespace*, and the slot's value/spec split is a rule
about *form count*. Today the split wins.

## Impact

It fails silently. There is no read error and no compile error — the form is
well-shaped, it just means something else. The failure surfaces at runtime, as
whatever `format` does with a bracket-pair as its spec argument, or as a wrong
string.

This blocks `(item x i)` -> `x[i]` conversion inside f-strings. Two spots in
[../koskari/dis.ki](../koskari/dis.ki) are deliberately left on the `(item …)`
spelling for this reason and should not be "cleaned up" until this is fixed:

- `parts.append f"-> {(item ext 3)}"`
- `return f"{arg} ({(_compare_name sc[0])} x{(item sc 1)})"`

The workaround, meanwhile, is to wrap: `f"{(repr closure_names[idx])}"` and
`f"{(_format_constant cpool[val])}"` both work and are already in the tree.

## Direction

Adjacency should bind tighter than the value/spec split. The slot reader wants
to apply the same suffix-attachment rules as normal expression context *first*,
and only then treat whatever whitespace-separated forms remain as value and
spec. Under that ordering `{foo[0]}` is one form (the subscript) and
`{i "3d"}` is still two, because the space is what separates them.

## Open questions

- Do the other suffix forms have the same problem in a slot — attribute access
  (`foo.bar`), and a call suffix? Worth checking before fixing only the bracket
  case.
- Should a bracket-pair appearing as the spec be an error or a lint, once the
  ambiguity is resolved? A literal list is never a valid format spec, so it is
  a candidate diagnostic rather than a silent pass-through.
- Corpus: this wants both a valid record (`{foo[0]}` subscripting) and a
  wrong-usage record pinning the old misread, once the behavior is settled.
