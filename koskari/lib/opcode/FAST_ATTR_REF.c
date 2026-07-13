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
   FAST_ATTR_REF: arg = extra index. Schema FASTVAR (slot), CONST (attr name).
   Push AttributeRef for obj.attr where obj = KosNamespace_FastDeref(ns, slot).
*/

KOS_OP_HANDLER(FAST_ATTR_REF) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  PyObject *ns = CURRENT_NS();
  KOS_OP_REQUIRE_NS(ns);
  PyObject *attr_name = KosEvalState_CONST(es, row.a1);
  if (! attr_name)
    return EVAL_RAISE();
  size_t idx = (size_t) row.a0;
  PyObject *obj = kos_ns_fast_deref(ns, idx);
  if (! obj)
    return EVAL_RAISE();
  PyObject *ref = KosAttributeRef_New(attr_name, obj, attr_name);
  Py_DECREF(obj);
  if (! ref)
    return EVAL_RAISE();
  return kos_op_result_PUSH(es, ref);
}


// The end.
