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
   CHECK_EXCEPTION: arg = target PC (label index). Pop TOS (exception type).
   If current exception matches that type, leave PC unchanged; else set PC to
   arg and continue (branch to handler).
*/

KOS_OP_HANDLER(CHECK_EXCEPTION) {
  PyObject *match = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! match)
    return -1;

  int ret = 0;
  PyObject *typ = NULL, *val = NULL, *tb = NULL;
  PyErr_GetExcInfo(&typ, &val, &tb);

  if (typ)
    ret = PyErr_GivenExceptionMatches(typ, match);

  Py_XDECREF(typ);
  Py_XDECREF(val);
  Py_XDECREF(tb);
  Py_DECREF(match);

  if (ret) {
    (void) out_ev;
    return 0;
  }
  CURRENT_PC(ctx) = KosOp_ARG(insn);
  (void) out_ev;
  return 0;
}
