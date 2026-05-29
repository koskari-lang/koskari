# KOS_OP_BUILD_KWDS

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: PAIR_COUNT. Arg = number of keyword pairs.
- **Status**: Implemented.
- **Stack**: `[..., k_0, v_0, ..., k_{n-1}, v_{n-1}] → [..., dict]`. Pop 2*opa elements as key,value pairs and push a dict.

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
