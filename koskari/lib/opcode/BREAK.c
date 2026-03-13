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
   BREAK: arg = result count (0 = None, 1 = single value, N = tuple of N).
   Exit innermost loop/foreach; jump to its break target. Pops the loop
   region and pushes the result value to the parent stack as the loop
   expression value. Raises RuntimeError if no LOOP/FOREACH region on chain.
*/

KOS_OP_HANDLER(BREAK) {
  unsigned opa = KosOp_ARG(insn);
  PyObject *result = NULL;

  if (opa == 0) {
    result = Py_None;
    Py_INCREF(result);
  } else if (opa == 1) {
    result = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
    if (! result) {
      PyErr_SetString(PyExc_RuntimeError, "BREAK pop underrun");
      return -1;
    }
  } else {
    result = KosStackBlock_PopTail(&CURRENT_STACK(ctx), (size_t) opa);
    if (! result) {
      PyErr_SetString(PyExc_RuntimeError, "BREAK PopTail underrun");
      return -1;
    }
  }

  return kos_dyn_transfer_to_loop(ctx, KOS_XFER_EVENT_BREAK, result, out_ev);
}
