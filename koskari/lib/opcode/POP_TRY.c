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
   dropping. Verify top of dynamic chain is a TRY region, then pop the
   region; the pop gives back the handler-visible exception the region
   displaced, if it consumed one.
*/

KOS_OP_HANDLER(POP_TRY) {

  KosDynNode *top = KosEvalState_CurrentNode(es);

  PyErr_Clear();

  PyObject *value = KosEvalState_StackPopRef(es);
  if (unlikely(! value))
    RESULT_RAISE();

  if (unlikely(kos_dyn_pop_try(es, (KosDynTry *) top) < 0)) {
    Py_DECREF(value);
    RESULT_RAISE();
  }

  RESULT_STEAL(es, value);
}


// The end.
