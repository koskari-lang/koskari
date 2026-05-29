# VM and opcodes (contributors)

When adding or changing an opcode:

1. Add `OP(NAME, STYLE)` to `koskari/lib/opcodes.incl` (order matters)
2. Regenerate handlers: `make handlers` or `tox -e handlers`
3. Implement the handler under `koskari/lib/opcode/`
4. Document semantics in `design/OPCODES.md`
5. Add or update the user page under `docs/reference/vm/opcodes/`
6. Add tests under `tests/lib/opcode/` or relevant compile tests

User-facing opcode pages are one file per opcode for git-friendly updates.
Regenerate the initial tree with `tox -e regen-docs-ref` only when
re-bootstrapping from design docs; routine edits are per-file.

See [Opcode overview](../reference/vm/index.md) and
[Opcode families](../reference/vm/families.md).
