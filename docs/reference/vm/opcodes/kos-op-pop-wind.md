# KOS_OP_POP_WIND
- **Style**: NOARG.
- **Stack**: Pops the body result into the wind node and leaves the node. An entered node with an after aside pushes a frame that runs it above the node; when that frame returns, its value is dropped, the node unlinks, and the body result is pushed on the stack of the frame POP_WIND ran in. Otherwise the node unlinks at once and the body result is pushed.
- **Invariants**: Dyn top must be KOS_DYN_WIND; otherwise RuntimeError.
