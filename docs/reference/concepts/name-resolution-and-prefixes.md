# Name resolution: runtime, compile-time, and layout

A single name in Koskari can mean up to three different things, chosen by a
**prefix** and by **where the name appears**. This is what lets `(+ 1 2)`
compile to fast arithmetic opcodes while `+` is still an ordinary function you
can pass around. The mechanism is powerful and used everywhere, but the prefixes
(`compile/`, `layout/`) trip people up. This page is the full model.

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

1. Is `foo` (the **bare** name) bound to a **syntax**? Use it.
2. Otherwise, is `compile/foo` bound to a syntax? Use it.
3. Otherwise, rewrite the call to `(call foo a b)` — a **runtime
   function call** of whatever value `foo` holds.

So the compile-time slot wins when it exists; only when there is no syntax for
`foo` does the call fall back to calling `foo` as a runtime value.

### The two `+`

`+` fills both the runtime and compile-time slots:

```koskari
(+ 1 2)          ; step 2 finds compile/+ -> emits arithmetic opcodes
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

## Forcing a runtime call: `call`

`(call foo a b)` **forces** step 3 — it compiles through `compile/call`,
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
`(globals)` or `(locals)`. If that expansion wrote `(foo …)`, resolution step 1
or 2 would find the macro again and it would expand forever. Writing `(call foo
…)` reaches the runtime function directly and breaks the loop. This is the main
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

When you define a name, you choose which slots it fills. The four common cases:

| You define… | Result | Example |
|-------------|--------|---------|
| a runtime value **and** `compile/foo` | `(foo …)` uses the compile-time form; `foo` as a value / `(call foo …)` uses the function | `+` |
| a syntax named plainly `foo` (no prefix) | fills **both** slots at once: step 1 finds it in operator position, and `foo` as a value is the syntax object | `create-instance-check`, most user `define-syntax` macros |
| a syntax named `compile/foo` only | `(foo …)` expands, but `foo` has **no** runtime value | `if`, `while`, `begin` |
| any of the above **plus** `layout/foo` | additionally handles `foo …:` block headers | `while`, `try`, `define-class` |

Note the difference between the middle two rows. `define-syntax foo:` binds the
name **literally**: write `define-syntax compile/foo:` to fill only the
compile-time slot (as the core forms do), or `define-syntax foo:` to fill the
bare slot (compile *and* runtime at once). Core Koskari conventionally prefixes
its special forms with `compile/` so their bare names stay free.

## Names a macro emits resolve at the use site

A macro template names other identifiers -- a class it constructs, a helper it
calls. Those names resolve where the macro is **used**, not where it is defined.
A macro that expands to `(basicConfig …)` compiles fine at its definition, then
fails with `Empty reference: basicConfig` at every call site that has not
imported `basicConfig`.

So a macro that ships in a library must only emit names the caller can reach.
The reliable way is to route through the library's own **exported** helpers:
have the expansion call an exported `(configure-logging …)` rather than
`basicConfig` directly, so `import-all-from your-lib` brings both the macro and
every name its expansion needs into the caller's module together.

This is the single rule behind a working macro library: **export everything a
macro expands into.** A quick audit is to read the expansion and confirm each
free identifier in it is exported.

## In practice

- **Write surface forms.** `(+ 1 2)`, `(if …)`, `(while …)`, `(await x)`. The
  compiler finds the compile-time form for you — you almost never type a
  `compile/` prefix.
- **Reach for `call` only** to force a runtime function call past a compile-time
  form, which in practice means inside a self-referential macro.
- **A `compile/foo` in your code** is a sign something was written in expanded
  form. It compiles, but it is not how the form is normally written.

## See also

- [compile/call](../specials/compile-call.md) — the default call form and its opcode.
- [Brackets and the call convention](brackets-and-call-convention.md) — what counts as operator position.
- [Demand, async, and defer](demand-async-and-defer.md) — a related compile-time vs runtime distinction (the two paths an evaluator can take).
