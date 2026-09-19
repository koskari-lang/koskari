# compile/decorate-subject
**Syntax:**

- `(decorate-subject (let BINDINGS) SUBJECT)` — **`(let BINDINGS SUBJECT)`**
- `(decorate-subject (with BINDING) SUBJECT)` — **`(with BINDING SUBJECT)`**
- `(decorate-subject (parameterize PARAMS) SUBJECT)` —
  **`(parameterize PARAMS SUBJECT)`**
- `(decorate-subject (call-with BINDING) SUBJECT)` — a forwarding function
  that enters `(with BINDING ...)` per invocation around a call to SUBJECT
- `(decorate-subject (call-parameterize PARAMS) SUBJECT)` — a forwarding
  function that enters `(parameterize PARAMS ...)` per invocation
- `(decorate-subject DECO SUBJECT)` — **`(DECO SUBJECT)`**

**Behaviour:** Applies **one** decorator expression to a subject form. A
body-less extent form (`let`, `with`, `parameterize`) is completed by
grafting the subject in as its body, so the extent spans the subject's
evaluation; the `call-` variants instead wrap the subject in a function that
establishes the extent per invocation, forwards the arguments, and delivers
the value, copying callable metadata via the `decorate-wraps` builtin
(`functools.update_wrapper`). Any other decorator expression is evaluated
and invoked on the subject.

Further shape arms can be registered with
[`extend-syntax`](compile-extend-syntax.md); registered arms are tried before
the invocation arm. See [Decorators](../../guide/decorators.md).
