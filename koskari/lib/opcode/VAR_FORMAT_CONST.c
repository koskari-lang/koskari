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
  VAR_FORMAT_CONST: fused VAR + OBJECT_FORMAT_CONST. Arg = extra index.
  Schema VAR (obj name), CONST (rules string). Resolve the name in the
  current frame's namespace and push PyObject_Format(obj, rules).
*/
KOS_OP_HANDLER(VAR_FORMAT_CONST) {
  const KosExtra *row = KosOp_ROW(insn);

  PyObject *name = KosExtra_OBJ(row, 0);
  PyObject *rules = KosExtra_OBJ(row, 1);

  PyObject *val = frame_ns_deref(es, name);
  if (unlikely(! val))
    RESULT_RAISE();

  PyObject *res = PyObject_Format(val, rules);
  Py_DECREF(val);
  if (unlikely(! res))
    RESULT_RAISE();

  RESULT_STEAL(es, res);
}


// The end.
