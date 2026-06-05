# compile/gensym-partial
**Symbol:** `compile/gensym-thunk`

**Syntax:** `(gensym-thunk BODY ...)`

**Reader — thunk syntax:** In s-expression mode, `{BODY ...}` is **thunk syntax**
(also called **the thunk form**). It reads as `(gensym-thunk BODY ...)`
— the same special with no extra macro. Nested `{...}` inside `BODY` read as
nested `(gensym-thunk ...)`. The head `gensym-partial` also resolves
to this special via the usual `compile/` prefix. See the [Glossary](../glossary.md)
entry for **thunk**.

**Use-case:** Short anonymous callables with inferred parameters: positional
placeholders `%1`, `%2`, … (contiguous from `%1`), optional rest from `...`
(Python `Ellipsis` at the call sites, lowered to `*` and a rest binding), and
zero-argument thunks when no placeholders appear. Handy for callbacks and
point-free-style partial application without writing `compile/lambda` formals by
hand.

**Behaviour:** Walks the body forms (rewrite everywhere; no quote boundary).
Symbols matching `%` + a positive integer (no leading zeros) are replaced by
fresh gensyms; repeated `%n` shares one binding. `...` is replaced by `*:`
list segments (`keyword("*")` and the rest gensym) for interop with
`compile/call`. Subtrees whose head is `gensym-thunk` or
`compile/gensym-thunk` are left unchanged (no recursion into the cdr).
After rewriting, placeholders must be contiguous `%1` … `%N`; otherwise
`SpecialError`. Builds inferred **formals** for `compile/lambda` (positionals in
order, then `*` and rest if `...` appeared) and returns the source form
`(lambda FORMALS . BODY)` for the compiler to compile (same lowering
pattern as other macros that expand to a lambda).

**Examples:**

```koskari
;; Unary: { (+ %1 1) }  =>  thunk taking one argument
(gensym-thunk (+ %1 1))

;; Thunk syntax in source
{ (print "hello") }

;; Thunk returning 99
{ 99 }

;; Variadic rest: ... in the body becomes * and a rest formal
(gensym-thunk (f ...))
```


