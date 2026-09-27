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
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *ns = CURRENT_NS();
  KOS_OP_REQUIRE_NS(ns);
  PyObject *attr_name = KosExtra_OBJ(row, 1);
  size_t idx = (size_t) KosExtra_IMM(row, 0);
  PyObject *obj, *obj_ref;

  /* The receiver is borrowed from its slot: the ref's constructor
     takes its own reference and runs no user code. */
  KOS_NS_FAST_BORROW(obj, obj_ref, ns, idx, { RESULT_RAISE(); });

  PyObject *ref = KosAttributeRef_New(attr_name, obj, attr_name);
  Py_XDECREF(obj_ref);
  if (! ref)
    RESULT_RAISE();
  RESULT_STEAL(es, ref);
}


// The end.
