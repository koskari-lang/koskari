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
   FAST_STORE: arg = slot index. Store TOS into fast slot.
*/

KOS_OP_HANDLER(FAST_STORE) {
  PyObject *ns = CURRENT_NS(ctx);
  if (! KosNamespace_SupportsFast(ns)) {
    PyErr_Format(PyExc_RuntimeError, "FAST_STORE invoked on %R", ns);
    return -1;
  }
  size_t idx = (size_t) KosOp_ARG(insn);
  PyObject *val = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! val)
    return -1;
  int res = KosNamespace_FastStore(ns, idx, val);
  Py_DECREF(val);
  if (res < 0)
    return -1;
  (void) out_ev;
  return 0;
}
