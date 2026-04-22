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
   EXCEPTION_MATCH: noarg. Pop TOS (exception type). If current exception
   matches that type, push True, else False.
*/

KOS_OP_HANDLER(EXCEPTION_MATCH) {
  (void) out_ev;

  PyObject *match = KosEvalState_StackPopRef(es);
  if (! match)
    return KOS_OP_POST_ERR();

  int ret = 0;
  PyObject *res = Py_False;
  PyObject *typ = NULL, *val = NULL, *tb = NULL;
  PyErr_GetExcInfo(&typ, &val, &tb);

  if (typ)
    ret = PyErr_GivenExceptionMatches(typ, match);

  Py_XDECREF(typ);
  Py_XDECREF(val);
  Py_XDECREF(tb);
  Py_DECREF(match);

  if (ret)
    res = Py_True;

  Py_INCREF(res);
  return kos_op_outcome_from_int_step(push_result(es, res));
}
