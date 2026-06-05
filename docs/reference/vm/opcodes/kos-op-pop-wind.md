# KOS_OP_POP_WIND
- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: Pops wind from dynamic chain; runs after thunk (if entered and non-null) via kos_dyn_wind_unwind; may return KOS_DYN_UNWIND_ASYNC_STARTED when thunk pushes a frame.
- **Invariants**: Dyn top must be KOS_DYN_WIND; otherwise RuntimeError.


## 23. Parameterize extent

