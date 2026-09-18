# KOS_OP_PUSH_WIND
- **Style**: COUNT. Arg = 1 (before only) or 2 (both before and after).
- **Stack**: For arg=2: `[..., before, after]` (before below, after above). Pops thunks; each must be callable or nil. For arg=1: `[..., before]`. Pushes KOS_DYN_WIND extent; runs before thunk on entry (via normal call path; may return KOS_DYN_UNWIND_ASYNC_STARTED).
- **Pushes:** KOS_DYN_WIND (static extent KOS_EXT_WIND).
- **Example:** See extent-wind-unwind.md; thunks are invoked via kos_callable_call.

