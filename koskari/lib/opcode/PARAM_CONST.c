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
  PARAM_CONST: arg = extra index. Schema CONST (parameter name), CONST
  (default value).
  Stack [] -> [value].
  As PARAM, with the default coming from the constant pool rather than
  the stack. Creates nothing in either store.
*/
KOS_OP_HANDLER(PARAM_CONST) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);

  PyObject *name = KosExtra_OBJ(row, 0);

  PyObject *cell = kos_param_lookup(KosEvalState_CONTEXT(es), name);
  PyObject *value = cell ? KosSymbolicRef_VALUE(cell) : NULL;

  if (! value) {
    value = KosExtra_OBJ(row, 1);
  }

  RESULT_PUSH(es, value);
}


// The end.
