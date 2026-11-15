# KOS_OP_POP_ASIDE
- **Style**: NOARG.
- **Behavior**: Closes the instructions of an aside declared by [PUSH_ASIDE](kos-op-push-aside.md). No frame runs it: the frame the aside is declared in passes from the PUSH_ASIDE to the instruction after POP_ASIDE, and the aside's own frame leaves by its RETURN.
- **Errors**: A frame that reaches POP_ASIDE ran an aside past its end, and raises `RuntimeError`.
- **Compiler form:** Emitted after the `RETURN 1` that ends the aside's instructions.
