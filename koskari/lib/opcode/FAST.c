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
   FAST: arg = slot index. Push value from fast local slot.
*/

KOS_OP_HANDLER(FAST) {
  PyObject *ns = CURRENT_NS(ctx);
  if (! KosNamespace_SupportsFast(ns)) {
    PyErr_Format(PyExc_RuntimeError, "FAST invoked on %R", ns);
    return -1;
  }
  size_t idx = (size_t) KosOp_ARG(insn);
  PyObject *val = KosNamespace_FastDeref(ns, idx);
  if (! val)
    return -1;
  return push_result(ctx, val);
}
