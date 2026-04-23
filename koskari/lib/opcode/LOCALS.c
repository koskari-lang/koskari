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
   LOCALS: arg unused. Push the current frame's namespace (locals) onto
   the stack.
*/

KOS_OP_HANDLER(LOCALS) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  PyObject *ns = CURRENT_NS(ctx);
  Py_INCREF(ns);
  return kos_op_outcome_from_int_step(push_result(es, ns));
}
