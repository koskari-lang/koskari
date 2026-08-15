# KOS_OP_POP_WIND
- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: Pops TOS (the body result) into the wind node, pops wind from the dynamic chain, and runs the after thunk (if entered and non-null) via kos_dyn_wind_unwind; the body result is delivered to the revealed segment when the thunk completes (or immediately when there is none). May return KOS_DYN_UNWIND_ASYNC_STARTED when the thunk pushes a frame.
- **Invariants**: Dyn top must be KOS_DYN_WIND; otherwise RuntimeError.


## 23. Parameterize extent

