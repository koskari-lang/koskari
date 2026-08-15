# KOS_OP_PUSH_COLLECT
- **Style**: CONST. Arg = constant pool index of NamespaceSpec.
- **Status**: Implemented.
- **Stack**: Does not consume the stack. Loads NamespaceSpec from constant pool at
  arg, creates CollectorNamespace, installs as frame ns, pushes KOS_DYN_COLLECT,
  and owns an operand segment; the body runs in it; expression results (e.g.
  None from compile/define) stay in the block and are discarded on POP_COLLECT.
- **Pushes:** KOS_DYN_COLLECT (static extent KOS_EXT_COLLECT; stores saved ns for
  restore on POP_COLLECT), with its owned segment for the body.
- **Invariants**: Extent kind KOS_EXT_COLLECT. Frame namespace must be KosNamespace.

