# compile/abort-at
**Symbol:** `compile/abort-at`

**Syntax:** `(abort-at TAG BODY ...)`

**Use-case:** Leave everything up to the nearest
[`reset-at`](compile-reset-at.md) carrying `TAG` and give it a value. It
is [`abort`](compile-abort.md) with the delimiter chosen by tag, and
[`shift-at`](compile-shift-at.md) without a continuation.

**Behaviour:** Evaluates `TAG` and emits `ABORT 1` (the tag is taken from
the stack), then the handler `BODY` inline. The walk outward from the
`abort-at` stops at the nearest delimiter whose tag is the same object as
`TAG`; delimiters of other tags on the way are left with the rest of the
region. `None` is the default tag, so `(abort-at None BODY ...)` is
`(abort BODY ...)`.

Everything [`abort`](compile-abort.md) says of the handler holds here
unchanged.

**Errors:** As for [`abort`](compile-abort.md). With no `reset-at`
carrying `TAG` on the dynamic chain, `KoskariPromptNotFound` is raised
with the message `abort-at has no enclosing reset-at carrying its tag`,
and its `tag` attribute is the tag sought.

**Examples:**

```koskari
(var a (gensym "a"))

;; the inner reset is left with the rest of the region
(reset-at a
  (+ 100
     (reset
       (+ 10 (abort-at a 7)))))
;; => 7

;; a library's failure: give the search an empty result
def fail []:
    abort-at a #()
```
