# KOS_OP_PUSH_COLLECT

```{admonition} Reference status
:class: note

Adapted from design/OPCODES.md. Stability: evolving unless noted otherwise.
```

- **Style**: CONST. Arg = constant pool index of NamespaceSpec.
- **Status**: Implemented.
- **Stack**: Does not consume the stack. Loads NamespaceSpec from constant pool at
  arg, creates CollectorNamespace, installs as frame ns, pushes KOS_DYN_COLLECT,
  then pushes a stack block. The body runs in this block; expression results (e.g.
  None from compile/define) stay in the block and are discarded on POP_COLLECT.
- **Pushes:** KOS_DYN_COLLECT (static extent KOS_EXT_COLLECT; stores saved ns for
  restore on POP_COLLECT), then a stack block for the body.
- **Invariants**: Extent kind KOS_EXT_COLLECT. Frame namespace must be KosNamespace.

---

---

Source: [design/OPCODES.md](../../../design/OPCODES.md)
