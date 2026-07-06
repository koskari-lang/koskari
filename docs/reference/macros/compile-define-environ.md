# compile/define-environ
**Syntax:** **`(define-environ NAME [DEFAULT])`**.

**Behaviour:** **`compile/define`** + **`environ-ref`** binding. Declares `NAME`
as a local whose ref reads and writes the process environment variable named
`NAME` (via `os.environ`).

Without a `DEFAULT`, the ref is an `ItemRef` into `os.environ`, so a deref of an
unset variable raises `KeyError`. With a `DEFAULT`, the ref returns `DEFAULT`
when the variable is unset (`environ.get`), stores into `os.environ[NAME]`, and
deletes the variable on clear.

The same ref is available anonymously through
[`compile/ref`](compile-ref.md) and [`compile/bind!`](compile-bind.md) with an
explicit key: **`(ref (environ KEY [DEFAULT]))`** and
**`(bind! NAME (environ KEY [DEFAULT]))`**. An empty `KEY` raises `ValueError`.
