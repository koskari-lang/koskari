# KOS_OP_POP_PROMPT
- **Style**: NOARG.
- **Status**: Implemented.
- **Stack**: Normal case (no active continuation return): pops TOS as the reset result, drops the PROMPT's COW stack block, unlinks the prompt from the chain, and pushes the result onto the parent stack. Invoked-continuation return case: the walk down from the current node finds a **`KOS_DYN_DELIM`** bookmark (mode **`POP_PROMPT`** for a grafted re-invoke, **`INPLACE`** for an in-place first invoke), passing invocation prompts and stopping at a live prompt; it pops TOS as result, unwinds to the delim, delivers the result at the invoke site per the delim's return metadata (an INPLACE delim restores the displaced frame state and re-links the suspended handler first), and resumes there.
- **Invariants**: Top of chain must be the matching prompt when POP_PROMPT executes. When returning from an invoked delimited continuation (return path recorded on **`KOS_DYN_DELIM`**), POP_PROMPT transfers control back to the call site with the result value instead of exiting normally. Exactly one value is consumed from the PROMPT's stack block and one is injected to the outer stack.
- **Push-return model**: Delimited invoke uses PUSH (graft frames onto current chain). When the continuation runs and hits POP_PROMPT, it consults the **`KOS_DYN_DELIM`** marker installed for that invoke and resumes at the call site with the value on stack.
- **Exception propagation**: If an exception escapes past the PROMPT during unwinding, the `unwind_on_exception` handler (dispatched via `KOS_DYN_CAP_HANDLES_EXCEPTIONS`) drops the stack block and unlinks the PROMPT, then continues unwinding. No POP_PROMPT opcode executes in this path.

**Example:** End of reset body.

| index | opcode    | arg | arg view | comment |
|------:|-----------|-----|----------|---------|
| 6     | POP_PROMPT| 0   | 0        | pop result; drop stack block; pop prompt; push result to outer stack |

