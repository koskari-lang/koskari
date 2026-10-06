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
   POP_LOOP: arg unused. Pop LOOP region, drop its segment, push the loop
   expression result to the operand stack. An enabled region delivers
   KosDynLoop_RESULT; a region CLEAR_LOOP disabled ran its else clause,
   whose value is the top of the region's stack.
*/

KOS_OP_HANDLER(POP_LOOP) {
  KosDynNode *region = KosEvalState_CurrentNode(es);
  if (! region || ! KosDynNode_IS_LOOP(region)) {
    PyErr_SetString(PyExc_RuntimeError,
                    "POP_LOOP: top is not a LOOP region");
    RESULT_RAISE();
  }

  KosDynLoop *loop = (KosDynLoop *) region;
  PyObject *result;
  if (KosDynLoop_ENABLED(loop)) {
    result = KosDynLoop_RESULT(loop);
    Py_INCREF(result);
  } else {
    result = KosEvalState_StackPopRef(es);
    if (unlikely(! result)) {
      PyErr_SetString(PyExc_RuntimeError, "POP_LOOP pop underrun");
      RESULT_RAISE();
    }
  }

  if (unlikely(kos_dyn_pop_loop(es, loop) < 0)) {
    Py_DECREF(result);
    RESULT_RAISE();
  }

  RESULT_STEAL(es, result);
}


// The end.
