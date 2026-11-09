# compile/switch
**Symbol:** `compile/switch`

**Syntax:** `(switch VALUE [COMPARE-LIST BODY...] ... [else: BODY...])`

**Use-case:** Multi-way branching on one value against lists of candidates.

**Behaviour:** `VALUE` is evaluated once. Each clause's `COMPARE-LIST` is a
list of candidate expressions; the list itself is not evaluated. Clauses are
tried in order, and a clause's candidates left to right, each evaluated only
after every earlier candidate failed to match. A candidate matches when it is
the same object as `VALUE` or when `VALUE == candidate` is true -- the test
Python's `in` applies to list items. An identical object matches without its
`__eq__` being called, so a NaN matches itself as a candidate although
`(== nan nan)` is `False`. An exception raised by `__eq__` propagates.

The first match runs that clause's body, and the body's last value is the
result; no later candidate is evaluated. `else:` matches anything and must be
the last clause. With no match and no `else:`, the result is `None`.

A clause test that is not a non-empty list raises `SpecialError`
(`"switch compare must be a list of values"`); an `else:` before the last
clause raises `SpecialError` (`"else: must be last clause"`).

The head may be written as `switch` or `compile/switch`.

**Expected lowered shape from layout/switch:**

```koskari
(switch VALUE
  ((COND1 COND2 ...) (begin BODY...))
  (else: (begin BODY...)))
```

Each clause body is a sequence of forms; a single `begin` form sequences them
itself.

**Examples:**

```koskari
;; Direct s-expression form
(switch 5
  [[1] "one"]
  [[2] "two"]
  [[5 6] "five-or-six"]
  [else: "meh"])
;; -> "five-or-six"

;; No match with else
(switch 5
  [[1] "one"]
  [[2 3] "two-or-three"]
  [else: "meh"])
;; -> "meh"

;; No match and no else
(switch 5
  [[1] "one"]
  [[2] "two"])
;; -> None

;; A NaN matches itself as a candidate
(let [[n (float "nan")]]
  (switch n
    [[n] "same"]
    [else: "other"]))
;; -> "same"
```

**Also:** layout handler [`switch`](../layouts/layout-switch.md).
