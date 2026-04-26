/*
  This library is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This library is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this library; if not, see <http://www.gnu.org/licenses/>.
*/

/**
   IMPORT_FROM: arg = constant index (attr name str).

   PEEK-NOT-POP WARNING
   ====================
   This is the ONLY opcode in the koskari instruction set that reads TOS
   without consuming it.  The module object placed on the stack by a
   preceding IMPORT or IMPORT_REL instruction remains there after
   IMPORT_FROM executes, allowing multiple successive IMPORT_FROM
   instructions to extract different names from the same module.

   EVERY sequence of one or more IMPORT_FROM instructions MUST be
   followed by a POP 1 to remove the module from the stack.  Omitting
   the POP leaves a dangling reference that will corrupt the stack.

   The compiler enforces this invariant structurally: IMPORT_FROM is
   only emitted by helper_import_from_sequence() in
   koskari/compile/specials/imports.py, which always emits the
   compensating POP 1 as its final act.  Do not emit IMPORT_FROM
   anywhere else.

   Semantics
   =========
   Equivalent Python:
       val = getattr(TOS, name)
       PUSH(val)
       # TOS (module) is NOT removed

   Stack before:  [..., module]
   Stack after:   [..., module, val]
*/

KOS_OP_HANDLER(IMPORT_FROM) {
  PyObject *name = get_name_from_insn(KosEvalState_CurrentCode(es), insn);
  if (! name)
    return KOS_OP_POST_ERR();

  /* Borrow TOS without popping — module stays on the stack */
  PyObject *mod = KosEvalState_StackPeek(es);
  if (! mod)
    return KOS_OP_POST_ERR();

  PyObject *val = PyObject_GetAttr(mod, name);
  if (! val)
    return KOS_OP_POST_ERR();

  return kos_op_outcome_from_int_step(push_result(es, val));
}
