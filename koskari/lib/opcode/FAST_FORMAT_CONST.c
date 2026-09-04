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
  FAST_FORMAT_CONST: fused FAST + OBJECT_FORMAT_CONST. Arg = extra
  index. Schema FASTVAR (slot), CONST (rules string). Push
  PyObject_Format(obj, rules) where obj is the fast local's value.
*/
KOS_OP_HANDLER(FAST_FORMAT_CONST) {
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *ns = CURRENT_NS();
  KOS_OP_REQUIRE_NS(ns);

  PyObject *rules = KosExtra_OBJ(row, 1);
  if (unlikely(! rules))
    RESULT_RAISE();

  size_t idx = (size_t) KosExtra_IMM(row, 0);
  PyObject *val = kos_ns_fast_deref(ns, idx);
  if (unlikely(! val))
    RESULT_RAISE();

  PyObject *res = PyObject_Format(val, rules);
  Py_DECREF(val);
  if (unlikely(! res))
    RESULT_RAISE();

  RESULT_STEAL(es, res);
}


// The end.
