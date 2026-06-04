# KOS_OP_BUILD_CLASS
- **Style**: NOARG. Argument unused.
- **Status**: Implemented.
- **Stack**: Expects `[..., pos, ns]` with TOS = ns, TOS-1 = pos. Pops ns, pos.
  pos = (name, bases) or (name, bases, metaclass). Builds the class via
  `kos_build_class` (namespace materialization, optional explicit metaclass,
  derived/composite metaclass, `__prepare__`, merge, metaclass call); pushes
  resulting class.
- **Compiler form:** Build pos with BUILD_ARGS; use with_collector_namespace for
  PUSH_COLLECT/body/POP_COLLECT; then BUILD_CLASS. See [compile/class](../../specials/compile-class.md).

**Example:** Empty class C. PUSH_COLLECT arg = constant pool index of nspec.

| index | opcode     | arg | arg view | comment |
|------:|------------|-----|----------|---------|
| 0     | CONST      | 0   | 0 ("C")  | name |
| 1     | CONST      | 1   | 1 (())   | bases |
| 2     | BUILD_ARGS | 2   | 2        | stack: [..., pos] |
| 3     | PUSH_COLLECT | 2 | 2 (nspec)| push collect scope; nspec from cpool |
| 4     | POP_COLLECT | 0 | 0        | drop block; pop; push ns |
| 5     | BUILD_CLASS| 0   | 0        | metaclass(name,bases,dict); push cls |
| 6     | RETURN     | 1   | 1        | |

