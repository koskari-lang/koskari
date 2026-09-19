# compile/dyn-wind
**Symbol:** `compile/dyn-wind`

**Syntax:**

- **Positional:** `(dyn-wind BEFORE BODY AFTER)` -- three elements in fixed order; `BEFORE` and/or `AFTER` may be `nil`.
- **Keyword:** `(dyn-wind [before: BEFOREBODY...] [body: BODY...] [after: AFTERBODY...])` -- top-level lists with marker keywords; omitted segments default to nil; order is arbitrary.

**Use-case:** Dynamic-wind extent: run before thunk on entry, body in the middle, after thunk on exit. Both thunks run on continuation transfer (multi-shot). Before and after are implicitly wrapped in null-arity lambdas; body is compiled as `(begin ...)`.

**Behaviour:** The form accepts either positional or keyword syntax. Detection: if any top-level element is a pair whose `car` is keyword `before:`, `body:`, or `after:`, keyword mode is used; otherwise positional mode. In keyword mode, segments are identified by their marker; missing segments are nil. Body is required; at least one of before or after is required. Emits: push before thunk (or CONST nil), push after thunk (or CONST nil), PUSH_WIND (arg 1 or 2), compile body, POP_WIND. Stack layout for PUSH_WIND 2: `[..., before, after]` (before below, after above).

**Examples:**

```koskari
;; Positional: before, body, after
(dyn-wind
  (begin (setup))
  (begin (work) result)
  (begin (teardown)))

;; Positional: after only (before = nil)
(dyn-wind nil
  (begin 42)
  (begin (cleanup)))

;; Keyword: before, body, after
(dyn-wind
  (before: (setup))
  (body: (work) result)
  (after: (teardown)))

;; Keyword: body and after only (before omitted)
(dyn-wind
  (body: 42)
  (after: (cleanup)))
```


