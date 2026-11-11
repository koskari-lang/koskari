# KOS_OP_POP_PROMPT
- **Style**: NOARG.
- **Stack**: Normal case (no active continuation return): pops TOS as the reset result, drops the PROMPT's segment, unlinks the prompt from the chain, and pushes the result onto the parent stack. Invoked-continuation return case: the prompt at the top of the chain is the delimiter of an applied continuation, recognised by its parent being a frame that sits on a **`KOS_DYN_DELIM`** bookmark; it pops TOS as result, unwinds to the delim, delivers the result at the invoke site per the delim's return metadata, and resumes there.
- **Invariants**: Top of chain must be the matching prompt when POP_PROMPT executes. When returning from an invoked delimited continuation (return path recorded on **`KOS_DYN_DELIM`**), POP_PROMPT transfers control back to the call site with the result value instead of exiting normally. Exactly one value is consumed from the PROMPT's segment and one is injected to the outer stack.
- **Push-return model**: Delimited invoke uses PUSH (graft frames onto current chain). When the continuation runs and hits POP_PROMPT, it consults the **`KOS_DYN_DELIM`** marker installed for that invoke and resumes at the call site with the value on stack.
- **Exception propagation**: If an exception escapes past the PROMPT during unwinding, the unwind drops the segment and unlinks the PROMPT, then continues. No POP_PROMPT opcode executes in this path.

**Example:** End of reset body.

| index | opcode    | arg | arg view | comment |
|------:|-----------|-----|----------|---------|
| 6     | POP_PROMPT| 0   | 0        | pop result; drop segment; pop prompt; push result to outer stack |

