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
   VAR_REF: arg = constant index (name). Push name reference (for closure).
*/

KOS_OP_HANDLER(VAR_REF) {
  PyObject *name = get_name_from_insn(KosEvalState_CurrentCode(es), insn);
  if (! name)
    return KOS_OP_POST_ERR();
  PyObject *ref = frame_ns_ref(es, name);
  if (! ref)
    return KOS_OP_POST_ERR();
  Py_INCREF(ref);
  return kos_op_outcome_from_int_step(push_result(es, ref));
}
