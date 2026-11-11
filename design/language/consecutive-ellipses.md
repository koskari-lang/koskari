# Consecutive template ellipses (investigate)

R7RS `syntax-rules` lets a template write `x ... ...`: a variable captured
under two pattern ellipses is spliced flat, every inner element in one
sequence, instead of one sub-list per outer iteration. Koskari's template
transcription peels exactly one ellipsis dimension per `...`, so the same
template raises `ellipsis variable used at insufficient ellipsis depth`.
Found by the 2026-11 pre-public review (D11, ruling R19); recorded for a
later investigation, not for now.

---

## Today

`koskari/hygiene.py`:

- A repeat capture is a bare Python `list`/`tuple` column, one entry per
  iteration of the enclosing pattern `...`; a rank-N variable nests N
  columns (`_repeat_seq`).
- `expand_template_with_hygiene` walks a template spine; an element
  followed by `Ellipsis` is one repetition level. `_controlling_seqs`
  collects the symbols in the element that still carry a column,
  `_ellipsis_count` requires one common length, and `_slice_bindings`
  narrows each controlling symbol by one dimension per iteration.
- A symbol reached with a column still unconsumed raises the depth
  error. A second `...` after `x ...` is read as an element on its own:
  the spine loop sees `items[i + 1] is Ellipsis` once, transcribes
  `x ...`, then meets the bare `Ellipsis` as the next element, where the
  template checker refuses it as an ellipsis following no pattern
  variable.

`docs/reference/specials/compile-syntax-rules.md` states the contract:
depth-aware, N nested pattern ellipses need N nested template ellipses,
fewer is a compile-time error. The error the review saw is that rule as
written, so nothing is misdocumented.

## The R7RS rule

R7RS 4.3.2: "Pattern variables that occur in subpatterns followed by one
or more instances of the identifier `<ellipsis>` are allowed only in
subtemplates that are followed by as many instances of `<ellipsis>`."
Consecutive ellipses count toward that number, and each consecutive
`...` beyond the first flattens one level:

```scheme
(define-syntax flatten
  (syntax-rules ()
    ((_ (a ...) ...) '(a ... ...))))
(flatten (1 2) (3) ())   ; => (1 2 3)
```

Chibi, Chez and Racket implement it; Guile too. It is the idiomatic way
to turn a list of binding groups into one flat argument list, which is
what `flet`/`labels`-shaped macros do by hand today with a helper macro
or a nested `begin`.

## What a change would touch

- **Template parse.** The spine loop in `expand_template_with_hygiene`
  counts a run of `Ellipsis` after an element instead of testing one.
  `check_template_ellipses` (the compile-time template validator) counts
  the same run and requires the element's variables to carry at least
  that many dimensions.
- **Transcription.** For a run of K ellipses over element E, the
  controlling variables are sliced one dimension for the outer
  iteration as now; the inner K-1 levels are flattened: the K-1 inner
  columns of each controlling variable are concatenated before the
  element is transcribed per leaf. Variables of rank less than K in the
  element are repeated with the element (the R7RS rule for variables of
  lower depth, which Koskari already applies at one level).
- **Mismatched lengths.** `_ellipsis_count` requires one length per
  level; flattening keeps that check per inner level, so
  `(a ...) ... (b ...) ...` with ragged groups still raises
  `incompatible ellipsis repetition lengths` where it would today.
- **The literal escape.** `(... TEMPLATE)` is unaffected: inside it
  every `...` is an element.
- **Pattern side.** Nothing. Consecutive ellipses in a *pattern* are not
  R7RS and stay refused.
- **Error message.** Whether or not flattening is implemented, the depth error
  can name the variable's captured depth and the depth it was used at.

## Invasiveness

The change is contained in `koskari/hygiene.py` (two functions and the
validator) plus tests under `tests/compile/specials/` for syntax-rules
and `macro-rules`, whose procedural arms receive the same bindings and
whose templates go through the same transcription. No reader change: the
reader already produces two `Ellipsis` atoms in a row. No matcher
change. The `.kio` format is untouched, since expansion happens before
codegen.

The open question is whether the flattening semantics are wanted at all
or whether a `splice`-style helper macro in the bootstrap would serve
the same need without widening the template language. R7RS compatibility
argues for the former; the small number of places in the bootstrap that
would use it argues for waiting until one does.

## Not in scope

- Scheme's custom ellipsis identifier `(syntax-rules ::: (lits) ...)`.
- Ellipsis depth greater than the pattern's in templates (an R7RS
  error too).
