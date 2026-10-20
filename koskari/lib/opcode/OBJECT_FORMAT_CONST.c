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
   OBJECT_FORMAT_CONST: arg = cpool index of rules string.
   Pops object and calls PyObject_Format(obj, rules_const).
*/

KOS_OP_HANDLER(OBJECT_FORMAT_CONST) {
  KOS_SYNC_PC(ctx, insn);
  PyObject *rules = KosOp_OBJ(insn);
  PyObject *obj;
  PyObject *res;

#if KOS_EVAL_SAFETYNET
  /* emit fills every CONST operand from the pool, which never holds
     NULL; a NULL here is an internal misfire */
  if (unlikely(! rules)) {
    PyErr_SetString(PyExc_SystemError,
                    "OBJECT_FORMAT_CONST: rules constant is NULL");
    RESULT_RAISE();
  }
#endif

  obj = KosContext_StackPopRef(ctx);
  if (! obj)
    RESULT_RAISE();

  res = PyObject_Format(obj, rules);
  Py_DECREF(obj);
  if (! res)
    RESULT_RAISE();

  RESULT_STEAL(ctx, res);
}


// The end.
