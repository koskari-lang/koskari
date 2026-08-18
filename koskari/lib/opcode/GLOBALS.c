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
  GLOBALS: arg unused. Push module globals: walk KosNamespace_PARENT from the
  current frame's namespace to the outermost parent (not dyn_top or let scope).
*/
KOS_OP_HANDLER(GLOBALS) {
  PyObject *ns = KosEvalState_GlobalsNamespace(es);
  Py_INCREF(ns);

  if (unlikely(! ns)) {
    PyErr_SetString(PyExc_RuntimeError, "GLOBALS: namespace is NULL");
    return EVAL_RAISE();
  }

  return kos_op_result_PUSH(es, ns);
}


// The end.
