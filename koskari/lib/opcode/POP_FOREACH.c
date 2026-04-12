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
   POP_FOREACH: arg unused. Pop FOREACH region, drop stack block, push
   KosDynForeach_RESULT to the operand stack as the loop expression result.
*/

KOS_OP_HANDLER(POP_FOREACH) {
  KosDynNode *region = (KosDynNode *) KosContext_DYN_TOP(ctx);
  if (! region || ! KosDynNode_IS_FOREACH(region)) {
    PyErr_SetString(PyExc_RuntimeError,
                    "POP_FOREACH: top is not a FOREACH region");
    return KOS_OP_POST_ERR();
  }

  KosDynForeach *fe = (KosDynForeach *) region;
  PyObject *result = KosDynForeach_RESULT(fe);
  Py_INCREF(result);

  if (KosStackBlock_DropBlock(&CURRENT_STACK(ctx)) < 0) {
    Py_DECREF(result);
    return KOS_OP_POST_ERR();
  }

  kos_dyn_pop_foreach(ctx, fe);

  if (unlikely(KosStackBlock_PushRef(&CURRENT_STACK(ctx), result) < 0)) {
    return KOS_OP_POST_ERR();
  }
  return KOS_OP_RUN();
}


// The end.
