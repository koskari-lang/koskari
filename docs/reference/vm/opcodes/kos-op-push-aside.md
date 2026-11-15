# KOS_OP_PUSH_ASIDE
- **Style**: EXTRA (`CONST`, `ASIDE`, `NOARG`, `NOARG`). Instruction arg = index into the code object's extra table; that row holds `a0` = a NamespaceSpec, or `None`, and `a1` = the aside this instruction declares.
- **Stack**: Unchanged. PUSH_ASIDE declares an aside: the instructions from the next one up to their RETURN, closed by the matching [POP_ASIDE](kos-op-pop-aside.md). The frame PUSH_ASIDE runs in runs none of them and goes on at the instruction after the POP_ASIDE. A [RESET](kos-op-reset.md), [ABORT](kos-op-abort.md) or [SHIFT](kos-op-shift.md) that names the aside runs it in a frame of its own over the same code object. With a spec in `a0`, that frame's namespace is built from the spec, its closure names resolved from the namespace live at the instruction that runs the aside. With `None`, the frame runs over that live namespace itself.
- **Pushes:** Nothing. PUSH_ASIDE and POP_ASIDE delimit a static extent of kind KOS_EXT_ASIDE, which has no node on the dynamic chain.
- **Invariants**: An aside is declared once in its code object. An instruction that names an aside sits in the block the aside is declared in: not in a block around it, and not inside the aside. The namespace live at that instruction is then the one the aside's instructions were compiled against. Inside an aside with a spec, a FAST operand indexes the namespace built from that spec. A jump neither enters nor leaves an aside. A tail call inside an aside replaces the aside's frame.
- **Errors**: Refused when the code is built: an aside name that is not a str (`TypeError`); a name declared twice, a name no PUSH_ASIDE declares, and an instruction that names an aside from outside the block it is declared in (`ValueError`).
- **Compiler form:** Pre-compile extra operand is a 4-tuple `(spec, name, 0, 0)`, the name a str; `NOARG` slots are ignored by the assembler. Finalize resolves the name, here and in each instruction that names the aside, to the aside's entry in the extent table. `Code.soften` names each aside `ASIDE_<n>`, `n` being the index of its extent. The peephole pass moves a label that leads to a PUSH_ASIDE to after its POP_ASIDE, so a jump to it does not run the PUSH_ASIDE.

**Example:** `(+ 1 (reset (* x 2)))` as the body of a function of `x`. The PUSH_ASIDE's extra row: `(None, aside 0, 0, 0)`.

| index | opcode             | arg | arg view                  | comment |
|------:|--------------------|-----|---------------------------|---------|
| 0     | CONST              | 0   | 0 (1)                     | stack: [1] |
| 1     | PUSH_ASIDE         | 0   | 0 (None, aside 0)         | declare the body; go on at 5 |
| 2     | FAST_OPERAND_CONST | 1   | MULTIPLY, fast 0 ("x"), 2 | the body, reading the function's `x` |
| 3     | RETURN             | 1   | 1                         | the body's value |
| 4     | POP_ASIDE          | 0   | 0                         | never run |
| 5     | RESET              | 2   | 2 (0, aside 0)            | push prompt; run the aside in a frame above it |
| 6     | OBJECT_OPERAND     | 2   | ADD x2                    | the function goes on with the reset's value |
| 7     | RETURN             | 1   | 1                         | |
