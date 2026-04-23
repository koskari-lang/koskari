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
   FAST_CLEAR: arg = slot index. Clear fast slot.
*/

KOS_OP_HANDLER(FAST_CLEAR) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  PyObject *ns = CURRENT_NS(ctx);
  if (! ns) {
    PyErr_SetString(PyExc_RuntimeError, "FAST_CLEAR invoked on NULL namespace");
    return KOS_OP_POST_ERR();
  }
  if (! KosNamespace_SupportsFast(ns)) {
    PyErr_Format(PyExc_RuntimeError, "FAST_CLEAR invoked on %R", ns);
    return KOS_OP_POST_ERR();
  }
  if (KosNamespace_FastClear(ns, (size_t) KosOp_ARG(insn)) < 0)
    return KOS_OP_POST_ERR();
  (void) out_ev;
  return KOS_OP_RUN();
}
