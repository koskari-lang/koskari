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
   POP_TRY: NOARG. Promote one value (TOS) from try block to parent before
   dropping. Verify top of dynamic chain is a TRY region, clear exception
   state, then pop the region.
*/

KOS_OP_HANDLER(POP_TRY) {
  (void) out_ev;

  KosDynNode *top = KosEvalState_CurrentNode(es);

  PyErr_Clear();
  PyErr_SetExcInfo(NULL, NULL, NULL);

  PyObject *value = KosEvalState_StackPopRef(es);
  if (unlikely(! value))
    return KOS_OP_POST_ERR();

  if (unlikely(kos_dyn_pop_try(es, (KosDynTry *) top) < 0)) {
    Py_DECREF(value);
    return KOS_OP_POST_ERR();
  }

  if (unlikely(KosEvalState_StackPUSH(es, value) < 0))
    return KOS_OP_POST_ERR();
  return KOS_OP_RUN();
}


// The end.
