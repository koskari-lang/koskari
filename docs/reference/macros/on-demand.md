# on-demand?

```{admonition} Reference status
:class: note

Adapted from design/MACROS.md. Stability: evolving unless noted otherwise.
```

Not a macro: a **`define`** function in `_conditionals.ki` that probes whether the
current evaluation path forbids **`await`** (catches **`KoskariSyncViolation`** from
bare **`(await)`**).


## koskari/bootstrap/_functions.ki

---

Source: [design/MACROS.md](../../design/MACROS.md)
