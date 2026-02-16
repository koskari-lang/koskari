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
   BUILD_FUNCTION: arg = N (number of keyword default values).
   Stack: ... fspec [, val_1 ... val_N, names_tuple ]. Pop fspec, then if
   N > 0 pop N values and names tuple into a defaults dict. Build function
   via KosFunction_New(fspec, defaults, frame_ns) and push result.
*/

KOS_OP_HANDLER(BUILD_FUNCTION) {
  unsigned opa = KosOp_ARG(insn);
  KosStackBlock **stack = &CURRENT_STACK(ctx);

  PyObject *fspec = KosStackBlock_PopRef(stack);
  if (! fspec)
    return -1;

  PyObject *defaults = NULL;
  if (opa > 0) {
    defaults = KosStackBlock_PopTailDictNames(stack, (size_t) opa);
    if (! defaults) {
      Py_DECREF(fspec);
      return -1;
    }
  }

  PyObject *parent_ns = CURRENT_NS(ctx);
  PyObject *fn = KosFunction_New(fspec, defaults, parent_ns);
  Py_DECREF(fspec);
  Py_XDECREF(defaults);
  if (! fn)
    return -1;

  if (KosStackBlock_PushRef(stack, fn) < 0) {
    Py_DECREF(fn);
    return -1;
  }
  Py_DECREF(fn);

  (void) out_ev;
  return 0;
}
