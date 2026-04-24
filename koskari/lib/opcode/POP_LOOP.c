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
   POP_LOOP: arg unused. Pop LOOP region, drop stack block, push
   KosDynLoop_RESULT to the operand stack as the loop expression result.
*/

KOS_OP_HANDLER(POP_LOOP) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  KosDynNode *region = (KosDynNode *) KosContext_DYN_TOP(ctx);
  if (! region || ! KosDynNode_IS_LOOP(region)) {
    PyErr_SetString(PyExc_RuntimeError,
                    "POP_LOOP: top is not a LOOP region");
    return KOS_OP_POST_ERR();
  }

  KosDynLoop *loop = (KosDynLoop *) region;
  PyObject *result = KosDynLoop_RESULT(loop);
  Py_INCREF(result);

  kos_dyn_pop_loop(es, loop);

  if (unlikely(KosEvalState_StackPushRef(es, result) < 0)) {
    Py_DECREF(result);
    return KOS_OP_POST_ERR();
  }
  Py_DECREF(result);
  return KOS_OP_RUN();
}


// The end.
