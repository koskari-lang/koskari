# compile/dyn-wind
**Symbol:** `compile/dyn-wind`

**Syntax:**

- **Positional:** `(dyn-wind BEFORE BODY AFTER)` -- three elements in fixed order; `BEFORE` and/or `AFTER` may be `nil`.
- **Keyword:** `(dyn-wind [before: BEFOREBODY...] [body: BODY...] [after: AFTERBODY...])` -- top-level lists with marker keywords; omitted segments default to nil; order is arbitrary.

**Use-case:** Dynamic-wind extent: run the before segment on entry, the body in the middle, the after segment on exit. Both segments run again on each continuation transfer across the extent (multi-shot). The body is compiled as `(begin ...)`.

**Segments:** The before and after segments are compiled in the scope the `dyn-wind` is written in. A segment reads and writes that scope's names, and a `var` written in a segment declares a name of that scope, visible after the `dyn-wind`. Each segment runs in a frame of its own: a `return` in a segment ends the segment, not the enclosing function, and a `break` or `continue` there does not reach a loop around the `dyn-wind`. A `yield` written in a segment is refused at compile time; one in a function defined within the segment belongs to that function. A segment's value is discarded.

**Transfers:** A continuation transfer runs the segments in the extent of the `dyn-wind` itself, as Scheme's `dynamic-wind` does. Leaving, it runs the after segments innermost first, each once the extents inside its body are gone. Re-entering, it runs the before segments outermost first, each with the extents outside its `dyn-wind` in place and nothing from its body yet. When a before segment raises on re-entry, its body was never entered: a `try` inside that body does not see the exception and its own after segment does not run. The extents already re-entered unwind as for any exception, their after segments innermost first, and the exception reaches the call of the continuation.

**Behaviour:** The form accepts either positional or keyword syntax. Detection: if any top-level element is a pair whose `car` is keyword `before:`, `body:`, or `after:`, keyword mode is used; otherwise positional mode. In keyword mode, segments are identified by their marker; missing segments are nil. Body is required; at least one of before or after is required. A falsy literal such as `0` in a segment's place is a segment whose value is that literal, not an absent one.

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

;; A var in the before segment is the enclosing scope's name
(begin
  (dyn-wind (var seen 1) 0 nil)
  seen)                          ; => 1
```

**Also:** layout handler [`dyn-wind`](../layouts/layout-dyn-wind.md).
