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
   FAST_BIND: arg = slot index. Bind name to fast slot.
*/

KOS_OP_HANDLER(FAST_BIND) {
  PyObject *ns = CURRENT_NS(ctx);
  if (! KosNamespace_SupportsFast(ns)) {
    PyErr_Format(PyExc_RuntimeError, "FAST_BIND invoked on %R", ns);
    return -1;
  }
  size_t idx = (size_t) insn->arg;
  if (idx >= KosNamespace_FAST_COUNT(ns)) {
    PyErr_Format(PyExc_NameError, "Namespace fast index out of bounds, %zu",
                 idx);
    return -1;
  }
  PyObject *val = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! val)
    return -1;
  KosNameBind *nb = KosNamespace_GET_FAST(ns, idx);
  PyObject *ref = KosSymbolicRef_New(nb->name, val);
  Py_DECREF(val);
  if (! ref)
    return -1;
  int res = KosNamespace_FastBind(ns, idx, ref);
  Py_DECREF(ref);
  if (res < 0)
    return -1;
  (void) out_ev;
  return 0;
}
