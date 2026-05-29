# compile/dyn-wind

```{admonition} Reference status
:class: note

Adapted from design/SPECIALS.md. Stability: evolving unless noted otherwise.
```

**Symbol:** `compile/dyn-wind`

**Syntax:**

- **Positional:** `(compile/dyn-wind BEFORE BODY AFTER)` — three elements in fixed order; `BEFORE` and/or `AFTER` may be `nil`.
- **Keyword:** `(compile/dyn-wind [before: BEFOREBODY...] [body: BODY...] [after: AFTERBODY...])` — top-level lists with marker keywords; omitted segments default to nil; order is arbitrary.

**Use-case:** Dynamic-wind extent: run before thunk on entry, body in the middle, after thunk on exit. Both thunks run on continuation transfer (multi-shot). Before and after are implicitly wrapped in null-arity lambdas; body is compiled as `(compile/begin ...)`.

**Behaviour:** The form accepts either positional or keyword syntax. Detection: if any top-level element is a pair whose `car` is keyword `before:`, `body:`, or `after:`, keyword mode is used; otherwise positional mode. In keyword mode, segments are identified by their marker; missing segments are nil. Body is required; at least one of before or after is required. Emits: push before thunk (or CONST nil), push after thunk (or CONST nil), PUSH_WIND (arg 1 or 2), compile body, POP_WIND. Stack layout for PUSH_WIND 2: `[..., before, after]` (before below, after above).

**Examples:**

```scheme
;; Positional: before, body, after
(compile/dyn-wind
  (compile/begin (compile/call setup))
  (compile/begin (compile/call work) result)
  (compile/begin (compile/call teardown)))

;; Positional: after only (before = nil)
(compile/dyn-wind nil
  (compile/begin 42)
  (compile/begin (compile/call cleanup)))

;; Keyword: before, body, after
(compile/dyn-wind
  (before: (compile/call setup))
  (body: (compile/call work) result)
  (after: (compile/call teardown)))

;; Keyword: body and after only (before omitted)
(compile/dyn-wind
  (body: 42)
  (after: (compile/call cleanup)))
```

**Tests:** `tests/compile/specials/test_dyn_wind.py`

---

Source: [design/SPECIALS.md](../../design/SPECIALS.md)
