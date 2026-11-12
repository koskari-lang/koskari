# compile/shift-at
**Symbol:** `compile/shift-at`

**Syntax:** `(shift-at TAG K BODY ...)`

**Use-case:** Capture the delimited continuation up to the nearest
[`reset-at`](compile-reset-at.md) carrying `TAG`, abort to it, and call
the handler -- a function of `K` whose body is `BODY` -- with the
continuation there. It is [`shift`](compile-shift.md) with the delimiter
chosen by tag.

**Behaviour:** Evaluates `TAG`, compiles the handler as a function of the
one parameter `K`, and emits `SHIFT 1` carrying it (the tag is taken from
the stack before the capture, so it is not part of the continuation). The
handler is called with the `reset-at`'s prompt still in place, so a
`shift-at` of the same tag in `BODY` captures up to the same `reset-at`.

The capture walks outward from the `shift-at` to the nearest delimiter
whose tag is the same object as `TAG`. Delimiters of other tags on the
way are crossed and become part of the continuation: each call of `K`
establishes them again. `None` is the default tag, so
`(shift-at None K BODY ...)` is `(shift K BODY ...)`.

Everything [`shift`](compile-shift.md) says of `K`, of the handler running
after the abort, and of `return`, `break`, `continue` and `yield` holds
here unchanged.

**Errors:** As for [`shift`](compile-shift.md). `KoskariPromptNotFound`
carries the tag sought on its `tag` attribute, so code that owns a tag
can tell a failure against its own delimiter from one against another's.

**Examples:**

```koskari
(var a (gensym "a"))
(var b (gensym "b"))

;; k is (lambda [v] (reset-at a (+ 100 (reset-at b (+ 10 v)))))
;; so (k 1) is 111, and that is the value of the outer reset-at
(reset-at a
  (+ 100
     (reset-at b
       (+ 10 (shift-at a k (k 1))))))

;; a function that captures up to a delimiter its caller established
def collect [choices]:
    shift-at a k:
        var out #[]
        for-each [c choices]:
            out.append (k c)
        out

;; => [2, 4, 6]
reset-at a:
    * 2 (collect #[1 2 3])
```
