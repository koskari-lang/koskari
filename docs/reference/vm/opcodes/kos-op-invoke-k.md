# KOS_OP_INVOKE_K
- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: Pops value, then continuation (TOS); invokes continuation with value. Uses push model: grafts continuation frames onto current chain (caller stays below); winds PARAM nodes in cont if any; injects value. When continuation hits POP_PROMPT, control returns to the call site with the result. Prompt need not be in the dynamic chain.
- **Invariants**: Continuation must be valid (same evaluator token, usable state). Multi-shot: invocation clones state.

**Example:** Invoke continuation k with value 100. Stack before: [..., 100, k].

| index | opcode  | arg | arg view | comment |
|------:|---------|-----|----------|---------|
| 0     | …       | …   | …        | stack: [..., value, continuation] |
| 1     | INVOKE_K| 0   | 0        | pop value, pop k; invoke k(value) |

