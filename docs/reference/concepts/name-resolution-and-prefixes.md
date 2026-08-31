# Name resolution: runtime, compile-time, and layout

A single name in Koskari can mean up to three different things, chosen by a
**prefix** and by **where the name appears**. This is what lets `(+ 1 2)`
compile to fast arithmetic opcodes while `+` is still an ordinary function you
can pass around. The prefixes
(`compile/`, `layout/`) trip people up.

## The three slots

A name such as `foo` can occupy up to three independent **slots**:

| Slot | Written as | Consulted when… | Holds |
|------|-----------|-----------------|-------|
| **Runtime** | `foo` | `foo` is used as a *value*, or called through `compile/call` | an ordinary value (usually a function) |
| **Compile-time** | `compile/foo` | `(foo …)` appears in *operator position* | a *syntax* — a special form or macro that runs at compile time |
| **Layout** | `layout/foo` | `foo` heads an indented *block* (`foo …:`) | a layout handler that turns the block into a form |

The slots are independent: a name can fill one, two, or all three. Which slots
are filled is decided by whoever *defines* the name — see the *Filling the
slots* section below.

## How `(foo …)` resolves in operator position

When the compiler sees a list whose head is a symbol, `(foo a b)`, it resolves
`foo` in this exact order:

1. Is `compile/foo` bound to a **syntax**? Use it. A syntax whose only arm
   is a *bare form* (see below) acts as an **alias** here: the head expands
   through the bare arm and the arguments ride along, so `(foo a b)`
   becomes `(EXPANSION a b)` and resolves again from the top.
2. Otherwise, rewrite the call to `(call foo a b)` — a **runtime
   function call** of whatever value `foo` holds.

The short name is the *only* spelling that resolves as a use. Writing the
prefixed spelling in operator position — `(compile/foo a b)` — does **not**
invoke the syntax: it is a runtime call of the syntax *object*, which fails.
The prefixed spelling always denotes the object itself (see below).

When the head is not a symbol but a **form**, the compiler first attempts
to lower it — see *Expansion in operator position* below.

### The two `+`

`+` fills both the runtime and compile-time slots:

```koskari
(+ 1 2)          ; step 1 finds compile/+ -> emits arithmetic opcodes
(define p +)     ; + as a value: the runtime function
(p 3 4)          ; -> 7
```

`(+ 1 2)` never calls the `+` function — the `compile/+` syntax turns it into
opcodes. The function `+` is a *separate* thing in the runtime slot, reached only
when you use `+` as a value (or force a call; see below).

## Using a name as a value

Outside operator position, a bare name resolves to its **runtime** slot:

```koskari
(map + xs ys)    ; + here is the runtime function passed to map
```

If a name fills *only* its compile-time slot, using it as a value is an error —
there is nothing in the runtime slot:

```koskari
(if True 1 2)    ; fine: compile/if is a syntax
(define x if)    ; NameError: Empty reference: if  -- there is no runtime `if`
```

One class of names never reaches value resolution at all: a syntax with a
**bare form** expands its lone symbol before the runtime slot is ever
consulted. The statement words carry bootstrap bare forms, so a lone
`return`, `continue`, `break`, `raise`, `yield`, or `pass` expands to its call form
— the Python-style spelling works, and none of these six can ever name a
variable. The [`compiler/bare-control-words`](pragma.md#compiler) pragma
selects whether that conversion is silent, warned, or refused. Any syntax
can declare a bare form with the `bare-syntax` transformer kind — see
[compile/define-syntax](../macros/compile-define-syntax.md).

## Forcing a runtime call: `call`

`(call foo a b)` **forces** step 2 — it compiles through `compile/call`,
bypassing any `compile/foo` syntax, and calls the runtime value of `foo`:

```koskari
(+ 1 2)          ; compile/+  -> opcodes
(call + 1 2)     ; compile/call -> a real function call of the runtime +  -> 3
```

Both yield `3`, but by different paths: the first is inlined arithmetic, the
second is an actual function invocation.

`call` earns its keep in **self-referential syntax**. A macro named `foo` often
expands into code that must *call the underlying function* `foo` — for example a
wrapper that supplies default arguments pulled from the caller's environment with
`(globals)` or `(locals)`. If that expansion wrote `(foo …)`, resolution would
find the macro again and it would expand forever. Writing `(call foo …)`
reaches the runtime function directly and breaks the loop. This is the main
reason to write `call` explicitly; in ordinary code you rarely do.

## The layout slot

An indented block whose header is `foo` consults `layout/foo`, a handler that
rewrites the block into an ordinary form before the rules above apply. `while`
fills both a layout and a compile-time slot:

```koskari
while (> n 0):        ; layout/while turns the block into (while …)
  (setq n (- n 1))

(while (> n 0) (setq n (- n 1)))   ; the same, written directly via compile/while
```

The layout slot only affects *how the block is read*; the resulting form still
resolves through the compile-time/runtime rules above.

## Filling the slots

When you define a name, you choose which slots it fills. The three common
cases:

| You define… | Result | Example |
|-------------|--------|---------|
| a runtime value **and** a syntax | `(foo …)` uses the compile-time form; `foo` as a value / `(call foo …)` uses the function | `+` |
| a syntax only | `(foo …)` expands, but `foo` has **no** runtime value | `if`, `while`, `begin`, most user `define-syntax` macros |
| either of the above **plus** `layout/foo` | additionally handles `foo …:` block headers | `while`, `try`, `define-class` |

`define-syntax foo:` and `define-syntax compile/foo:` are the **same
definition**: the transformer is always bound under `compile/foo`, whichever
spelling you write. The short spelling is preferred — the written name is the
transformer's *identity*, feeding help text and match errors, and the short
name is what users write at every use site.

## Expansion in operator position

A head does not have to be a symbol. When the head of a form is itself a
*form*, the compiler attempts to **lower** it before falling back to a
call: if the head's own (innermost) head names a syntax whose expansion
yields source, the head is replaced by its expansion and the whole form is
resolved again from the top.

```koskari
(define-syntax sel (syntax-rules [] [(sel x) +]))

((sel 1) 2 3)      ; head (sel 1) expands to the symbol +
                   ; -> (+ 2 3) -> 5, compiled as the arithmetic special
```

The expansion landing on a *symbol* is the powerful case: the rebuilt form
is a fresh syntax use, with the tail as that syntax's arguments. This goes
beyond Scheme, where an application's operator is expanded as an expression
and never re-examined as syntax — in Koskari, a macro can *compute* the
syntax at the head of a form.

Attempts nest through arbitrary depth and are all-or-nothing: a head whose
innermost head is not a syntax, or whose expansion compiles to bytecode
rather than source, fails the whole attempt, and the original form lowers
to an ordinary call of the head's value — which is why
`((make-adder 1) 2)` with a plain function still means exactly what it
always did, and why a method call's `(attr obj name)` head still reaches
the call machinery intact. Nothing from a failed attempt is kept.

```koskari
(define-syntax foo2 (syntax-rules [] [(foo2 x) bar2]))
(define-syntax bar2 (syntax-rules [] [(bar2 x) +]))

(((foo2 1) 2) 3)   ; -> ((bar2 2) 3) -> (+ 3) -> 3
```

## Referencing the syntax object

Because the prefixed spelling never resolves as a use, it is free to mean the
**object**: `compile/foo` in value position is an ordinary variable read of the
transformer itself.

```koskari
(define x if)              ; NameError -- there is no runtime `if`
(define x compile/if)      ; the compile/if Syntax object, as a value
(help compile/if)          ; describe the transformer
```

This is how tooling and meta-programs hold a transformer: the short name is
the use, the prefixed name is the thing.

## Names a macro emits resolve at the use site

A macro template names other identifiers -- a class it constructs, a helper it
calls. Those names resolve where the macro is **used**, not where it is defined.
A macro that expands to `(basicConfig …)` compiles fine at its definition, then
fails with `Empty reference: basicConfig` at every call site that has not
imported `basicConfig`.

So a macro that ships in a library must only emit names the caller can reach.
The reliable way is to route through the library's own **exported** helpers:
have the expansion call an exported `(configure-logging …)` rather than
`basicConfig` directly, and export both. Import the macro with
[`use`](../specials/use.md), which brings a name's facets together:
`(use your-lib your-macro)` carries the transformer and any runtime
sibling in one member.

This is the single rule behind a working macro library: **export everything a
macro expands into.** A quick audit is to read the expansion and confirm each
free identifier in it is exported.

## In practice

- **Write surface forms.** `(+ 1 2)`, `(if …)`, `(while …)`, `(await x)`. The
  compiler finds the compile-time form for you — you almost never type a
  `compile/` prefix.
- **Reach for `call` only** to force a runtime function call past a compile-time
  form, which in practice means inside a self-referential macro.
- **A `compile/foo` head in your code** is a mistake: it calls the syntax
  object as a runtime value and fails. In *value* position it is deliberate —
  a reference to the transformer itself.

## See also

- [compile/call](../specials/compile-call.md) — the default call form and its opcode.
- [Brackets and the call convention](brackets-and-call-convention.md) — what counts as operator position.
- [Demand, async, and defer](demand-async-and-defer.md) — a related compile-time vs runtime distinction (the two paths an evaluator can take).
