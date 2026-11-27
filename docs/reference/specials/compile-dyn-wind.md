# compile/dyn-wind
**Symbol:** `compile/dyn-wind`

**Syntax:**

- **Positional:** `(dyn-wind BEFORE BODY AFTER)` -- three elements in fixed order; `BEFORE` and/or `AFTER` may be `nil`.
- **Keyword:** `(dyn-wind [before: BEFOREBODY...] [body: BODY...] [after: AFTERBODY...])` -- top-level lists with marker keywords; omitted segments default to nil; order is arbitrary.

**Use-case:** Dynamic-wind extent: run the before segment on entry, the body in the middle, the after segment on exit. Both segments run again on each continuation transfer across the extent (multi-shot). The body is compiled as `(begin ...)`.

**Segments:** The before and after segments are compiled in the scope the `dyn-wind` is written in. A segment reads and writes that scope's names, and a `var` written in a segment declares a name of that scope, visible after the `dyn-wind`. Each segment runs in a frame of its own: a `return` in a segment ends the segment, not the enclosing function, and a `break` or `continue` there does not reach a loop around the `dyn-wind`. A `yield` written in a segment is refused at compile time; one in a function defined within the segment belongs to that function. A segment's value is discarded.

**Transfers:** A continuation transfer runs the segments in the extent of the `dyn-wind` itself, as Scheme's `dynamic-wind` does. Leaving, it runs the after segments innermost first, each once the extents inside its body are gone. Re-entering, it runs the before segments outermost first, each with the extents outside its `dyn-wind` in place and nothing from its body yet. When a before segment raises on re-entry, its body was never entered: a `try` inside that body does not see the exception and its own after segment does not run. The extents already re-entered unwind as for any exception, their after segments innermost first, and the exception reaches the call of the continuation.

**Generators:** A `yield` in the body leaves the `dyn-wind`, and the generator's next step enters it again. The yield runs the after segments of the extents around it, innermost first, before the consumer has the value. The next step, whether a `next`, a send, a throw or a close, runs the before segments, outermost first, and then the body continues at the yield. Each segment runs in the extent of its own `dyn-wind`, as on a transfer: a `parameterize` written inside the `dyn-wind` is not in effect for it, one written around it in the generator is, and a parameter the generator has not bound around it reads the binding of the Koskari code making the step, or, for a step made from Python, the bindings in force at the generator's first step from Python. An after segment that raises at a yield raises from the `dyn-wind`: a `try` in the body does not see it, one around the `dyn-wind` does, the value is not presented, and the segment does not run a second time. A before segment that raises on re-entry behaves as it does on a transfer, and the exception reaches the step; when the step is a throw or a close, that exception is raised from the one thrown, which is its `__cause__`. A generator closed or dropped at such a yield is entered, takes `GeneratorExit` at the yield and is left, so each segment runs once more. An `await` is not a yield: a coroutine suspended in the body stays inside the extent, and closing it there runs only the after segment.

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

;; A yield leaves the extent; the next step enters it again
(def counted [log]
  (dyn-wind
    (before: (log.append "in"))
    (body: (yield 1) (yield 2))
    (after: (log.append "out"))))

(begin
  (var log #[])
  (for-each [x (counted log)]
    (log.append x))
  log)                           ; => ["in", "out", 1, "in", "out", 2, "in", "out"]
```

**Also:** layout handler [`dyn-wind`](../layouts/layout-dyn-wind.md); guide page [Dynamic extents](../../guide/dynamic-extents.md).
