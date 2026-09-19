# layout/match
**Symbol:** `layout/match`

**Use-case:** Structural pattern matching in block syntax. Lowers to the
delimiter clauses consumed by
[`compile/match`](../specials/compile-match.md). Pattern syntax is
documented in [Match patterns](../../guide/match-patterns.md).

**Behaviour:** Does not accept peers. Header must be exactly
`match SUBJECT:` (one subject expression). A raw layout: inner `case` and
`else` children arrive as block sources with header and body structurally
separate, so a case header may span multiple tokens (`case x :: int:`).
The whole case header tail becomes the clause's pattern delimiter, and
nested layout blocks in each body are expanded, emitting:

`(match SUBJECT [[PATTERN...] BODY...] ... [else: BODY...])`

That is the same clause shape an s-expression author writes directly.

A `when` / `unless` marker in a case header splits it into pattern and
guard: the tokens before the marker are the pattern, and exactly one form
after it is the guard expression. `case PATTERN when COND:` emits
`[[PATTERN] when: COND BODY...]`.

Rules:

- body entries must be `case` or `else` blocks only
- `case` requires a pattern and a non-empty body
- a `when` / `unless` marker requires a pattern before it and exactly one
  guard expression after it
- `else` takes no arguments and requires a non-empty body
- only one `else` is allowed
- `case` cannot appear after `else`
- an empty body is allowed and lowers to `(match SUBJECT)`

**Syntax:**

```
match SUBJECT:
  case PATTERN:
    BODY
  case PATTERN when COND:
    BODY
  else:
    BODY
```

**Examples:**

```koskari
match v:
  case #[a b]:
    "pair"
  case n :: int:
    "int"
  else:
    "other"
;; -> (match v [[#[a b]] "pair"] [[n :: int] "int"] [else: "other"])

match v:
  case #[a b] when (> a b):
    "descending"
  case #[a b]:
    "ascending-or-equal"
;; -> (match v
;;      [[#[a b]] when: (> a b) "descending"]
;;      [[#[a b]] "ascending-or-equal"])

match msg:
  case #{"kind": "job" "url": u}:
    (fetch u)
  case Point[x y]:
    (plot x y)
  else:
    (ignore msg)
```
