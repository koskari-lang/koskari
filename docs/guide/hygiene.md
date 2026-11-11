# Hygiene

A macro writes code on the caller's behalf. Hygiene is the rule that the
names the macro writes and the names the caller writes stay apart: a
binding the template introduces never captures a caller's name, and a
name the template uses without binding it resolves where the macro is
used, as a name the caller wrote would. Koskari keeps them apart by turning every symbol a template
introduces into a `gensym`, a symbol that remembers which expansion
wrote it, and by having the compiler resolve such a symbol against what
that expansion bound before it falls back to the plain name.

## The problem hygiene solves

Take a `swap!` macro that needs a temporary:

```koskari
syntax swap!:
  syntax-rules []:
    swap! a b:
      let [[tmp a]]:
        set! a b
        set! b tmp
```

Without hygiene, `(swap! tmp other)` expands to
`(let [[tmp tmp]] (set! tmp other) (set! other tmp))`, and the caller's
`tmp` is shadowed by the template's before it is read. That is capture:
the template's binder swallows the caller's name. Hygiene keeps the two
`tmp`s apart:

```koskari
var tmp 1
var other 2
(swap! tmp other)
;; tmp is 2, other is 1
```

A name the template uses but does not bind is free. It resolves at the
use site, as if the caller had written it:

```koskari
syntax twice:
  syntax-rules []:
    twice v:
      (double v)

def double [x] (* x 2)
(twice 4)
;; => 8
```

A macro whose free names must mean one binding wherever it is used
needs that binding in scope at every use site, typically by exporting
it from the macro's module.

## How Koskari achieves it

Each expansion of a macro draws a fresh mark, an integer no other
expansion in the process has drawn. As the expander transcribes the
template, every symbol the template itself wrote becomes a gensym: a
symbol whose spelling is the written name followed by `#` and the mark
in hex, `tmp#b23376a7`, and which remembers both parts. Material the
caller supplied, the pieces the pattern captured, is spliced in as it was
written and is never marked.

A gensym is a symbol. `symbol?` answers true for it, it sits in a pair
like any symbol, and it is interned under its spelling so two mentions of
`tmp#b23376a7` are one object. It is a different object from the plain
`tmp`, and equality is identity, so nothing that compares symbols confuses
the two. Here the mark is chosen so the spelling is fixed; a template's
mark comes from the process counter.

```koskari
>>> var g (gensym (symbol "tmp") marks: #(26))
>>> (str g)
"tmp#1a"
>>> g.base
tmp
>>> g.marks
#(26)
>>> (== g g.base)
False
>>> (is (symbol (str g)) g)
True
```

`base` is the symbol the template wrote. Every plain symbol and keyword
has a `base` as well, which is itself, so code that wants the written
spelling of an atom reads `.base` with no test for what kind of atom it
has.

Keywords carry no marks. A keyword is a label, the name a caller writes
at a call site, in a formals list, or as a `cond` clause tag, and a
template's keyword reaches the compiler as the plain keyword.

## What the expander does

A `syntax-rules` or `syntax-match` arm transcribes its template. Each
symbol that appears in the template and is not a pattern variable is
scoped with the expansion's mark; each pattern variable is replaced by
what it captured, unmarked. `macroexpand-1` shows the result by its
written names, since a printed form is source and the marks are not:

```koskari
>>> (macroexpand-1 '(swap! x y))
(let [[tmp x]] (set! x y) (set! y tmp))
```

The `tmp` in that output is a gensym; `x` and `y` are the caller's
symbols. `str` of the gensym itself shows the mark.

A `macro-rules` or `macro-match` arm is a procedure that builds its
output by hand, with quasiquote or `cons`. The expander scopes that
output after the handler returns: every symbol outside a captured piece
gets the expansion's mark, and a piece the caller supplied, which arrives
inside a `datum` box, is left alone. For a hand-written macro a
quasiquoted template therefore behaves like a `syntax-rules` one:

```koskari
syntax with-tmp:
  macro-rules []:
    with-tmp expr:
      `(let [[tmp 10]] (+ tmp ,expr))

var tmp 1
(with-tmp tmp)
;; => 11: the template's tmp is 10, the caller's tmp is 1
```

A procedural macro may also call `gensym` for a name it wants fresh in
its own right. `(gensym "t")` returns a gensym with a mark of its own,
distinct from every other gensym and from the plain `t`. It is a symbol
and goes into the output like one:

```koskari
syntax with-fresh:
  macro-rules []:
    with-fresh expr:
      var t (gensym "t")
      `(let [[,t 10]] (+ ,t ,expr))

(with-fresh tmp)
;; => 11
>>> (macroexpand-1 '(with-fresh tmp))
(let [[t 10]] (+ t tmp))
```

A backtick is data, so it projects any marks a symbol in the handler's
body already carries. A macro that a template defines needs the marks
kept; see [Data and source](#data-and-source).

## Data and source

A quoted form is data. `'(tmp)` in a template yields `(tmp)` holding the
plain symbol `tmp`, the same object as `'tmp` written by hand, and a
backtick does the same for its literal parts. So `(== kind 'ok)` in a
template is true for a caller's `'ok`, and a `case` literal a template
writes matches a plain symbol. The marks are the compiler's, for binding
and resolving; a value the program compares does not carry them.

One kind of macro wants the opposite: a macro that defines another
macro. The inner macro's patterns and templates are source the expander
will read back later, and the inner template must reach the binder the
outer expansion made:

```koskari
(syntax mk-getter
  (syntax-rules []
    [(mk-getter name val)
     (begin
       (var secret val)
       (global syntax name (syntax-rules [] [(name) secret])))]))

(var secret 100)
(mk-getter get-secret 7)
(get-secret)
;; => 7
```

The outer expansion writes `secret#outer` at both the `var` and the
inner template. When the inner macro expands, its template symbol gains
the inner mark as well, `secret#outer#inner`, and the compiler's fallback,
below, finds `secret#outer`. This works because `syntax` quotes
patterns and templates with `quote-syntax`, spelled `#'`, which keeps
marks; a plain `quote` would have projected `secret#outer` to `secret`
and the inner template would have found the module's `secret`.

A procedural inner macro has to say so itself, because its output is
built by a backtick in a body the compiler cannot tell from any other
code:

```koskari
(syntax mk-getter
  (syntax-rules []
    [(mk-getter name val)
     (begin
       (var secret val)
       (global syntax name (macro-rules [] [(name) #`secret])))]))

(var secret 100)
(mk-getter get-secret 8)
(get-secret)
;; => 8
```

`` #` `` is `quasiquote-syntax`: a backtick that keeps marks. With a plain
backtick, `(get-secret)` is 100: the handler's `secret` was written by the
outer template and carried `#outer`, the backtick treated it as data and
projected it to `secret`, and the inner expansion's `secret#inner` fell
through to the module. The two spellings exist so the author can name
which one they mean; nothing in the surrounding form does it for them.
Data takes `'` and `` ` ``; source handed to a later
expansion takes `#'` and `` #` ``. Outside a template the four are the
same, since hand-written source carries no marks.

## Writing a name the use site shares

A name a template binds is private to its expansion. Macros that
cooperate through one module-level name, where one defines it and
another reads it, need that name written raw. In a `syntax-rules` or
`syntax-match` template, `(datumq name)`, or its reader shorthand
`-'name`, writes `name` as the caller would, unmarked:

```koskari
(syntax set-slot
  (syntax-rules [] [(set-slot v) (var (datumq shared-slot) v)]))
(syntax read-slot
  (syntax-rules [] [(read-slot) (datumq shared-slot)]))

(set-slot 7)
(read-slot)
;; => 7
shared-slot
;; => 7
```

`(datumq X)` takes exactly one form and emits it as written: nothing in
it is marked, and a pattern variable inside it is not substituted.
Errors in the form point at where the template wrote it.

A parameter a template introduces is private to the expansion too: its
name is the function's local binding, so a caller cannot pass it by
keyword. A template whose parameters callers name writes them raw:

```koskari
(syntax make-pair-fn
  (syntax-rules [] [(make-pair-fn name) (def name [-'x -'y] #(x y))]))

(make-pair-fn pair-of)
(pair-of y: 2 x: 1)
;; => (1, 2)
```

The expansion consumes the directive. A template that defines another
macro nests it once per level, `(datumq (datumq slot))` or `-'-'slot`,
so the inner template receives the raw `(datumq slot)` and its own
expansion writes `slot`:

```koskari
(syntax make-setter
  (syntax-rules []
    [(make-setter mname)
     (global syntax mname
       (syntax-rules [] [(mname v) (var (datumq (datumq slot)) v)]))]))

(make-setter set-slot)
(set-slot 11)
slot
;; => 11
```

A template that has to write a `datumq` call itself, as in the body of
a procedural macro it defines, writes the head raw:
`((datumq datumq) name)`. A `datum` call needs no escape; `datum` has no
meaning in a template. In a procedural body, `(datumq name)` is the
ordinary macro and returns the same raw form; inside a backtick it is
data like the rest, so it is unquoted: `` `(var ,(datumq slot) 0) ``.

## What the compiler does

The compiler treats a gensym as a symbol with a longer spelling. A
binder written as a gensym, in `var`, `def`, `let`, a lambda's formals,
`labels`, or `letrec`, declares the derived spelling, so the template's
`tmp` lives in the namespace as `tmp#b23376a7` beside the caller's `tmp`.

A reference written as a gensym is resolved when the enclosing function
body or module is complete and every declaration in it is known. The
compiler tries the candidate spellings longest first: the full spelling,
then the spelling with the newest mark removed, down to the plain name.
The first candidate that something visible declares wins: the inline
scopes open around the reference, the function body, the bodies that
enclose it, and finally the module. Failing all of those, the plain name
is used, and the runtime resolves it as it would any free name. That is
why a free name in a template reaches the use site's binding, and why
the inner template of a macro-generating macro
reaches the outer expansion's binder: `secret#outer#inner` is not
declared, `secret#outer` is.

Declarations in a sibling scope do not count. A template that writes
`(let [[tmp 1]] tmp)` and then `tmp` again outside the `let` resolves
the second `tmp` to the caller's binding, as written source would.

A head in call position resolves the same way: a macro or special the
same expansion defined under a scoped spelling is found from the template
that uses it, and every other scoped head falls through to the plain
macro or special.

Some positions read a name as a label rather than as a binding, and
there the compiler reads the base:

- an attribute name, in `(attr obj name)` and in `(.method obj ...)`;
- a member name in `import-from`, though the binding it goes into keeps
  the template's marks;
- a member a `class` body defines, so a template can add a method under
  the name a caller will call;
- a keyword, which is never scoped in the first place;
- a parameter name, in `parameterize`, `parameter`, and the forms that
  lower to them, since a parameter is addressed by its spelling;
- the operator in an inplace assignment;
- quoted data, as above.

`global` in a template binds the module under the scoped
spelling, so each expansion gets its own global and the caller's
same-named global is untouched:

```koskari
syntax counter:
  syntax-rules []:
    counter:
      begin:
        global var hits 0
        set! hits (+ hits 1)
        hits

var hits "mine"
(counter)
;; => 1
hits
;; => "mine"
```

And a formal named by a keyword binds under the name the caller writes,
because that name is the call-site interface:

```koskari
syntax kw-formal:
  syntax-rules []:
    kw-formal:
      lambda [count: 1]:
        count

var kf (kw-formal)
(kf)
;; => 1
(kf count: 5)
;; => 5
```

The template's `count` in the body is a gensym; it finds no
`count#mark` declared and falls back to the plain `count`, which the
formal bound. A caller-supplied form spliced into that body that
mentions `count` reaches the same parameter, so a template that binds a
keyword formal is the one place a template binder can shadow a caller's
name.

## Seeing the marks

Printing hides marks and `str` of a single gensym shows them. To see the
scoped spellings a module ended up with, look at its namespace:

```koskari
(counter)
(counter)
(list (filter (lambda [k] (k.startswith "hits")) (list (globals))))
;; => ['hits', 'hits#50e3cb5e', 'hits#50e3cb60', 'hits#50e3cb62']
```

Each `(counter)` was its own expansion with its own mark. A `.kio` file
holds compiled code, whose names are plain spellings such as
`hits#50e3cb5e`. A template the file carries, in a macro it defines or a
`#'` form, is stored with plain symbols and marked afresh when it
expands. Marks are drawn from a base chosen at random once per process,
so a scoped spelling compiled into a file is not minted again for a
different name by the process that loads it.

## See also

- [Macros and hygiene](macros-and-hygiene.md): defining macros, templates
  versus procedures, expansion order
- [compile/quote-syntax](../reference/specials/compile-quote-syntax.md)
  and [compile/quasiquote-syntax](../reference/specials/compile-quasiquote-syntax.md)
- [compile/datum](../reference/macros/compile-datum.md)
- [koskari.hygiene](../reference/api/koskari-hygiene.md): the expander's
  scoping and finalize functions
