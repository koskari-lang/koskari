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
  MATCH_STORE_NAMES: arg = cpool index of a names tuple (compile-time
  metadata; the capture names the arm declared). Stack [..., caps] with
  the matched arm's capture dict = TOS. Pop the dict, reveal single-cell
  captures from their Datum boxes in place, and hand the whole dict to
  KosNamespace_StoreDict, which binds each capture into the current
  namespace and raises on an undeclared name. Repeat frames and rest
  slices arrive as plain lists/tuples and pass through. A name absent
  from the dict (an OPTIONAL that did not fire) is left unbound.
*/

KOS_OP_HANDLER_COLD(MATCH_STORE_NAMES) {
  PyObject *ns = CURRENT_NS();
  KOS_OP_REQUIRE_NS(ns);

  PyObject *caps = KosEvalState_StackPopRef(es);
  if (unlikely(! caps))
    return EVAL_RAISE();

  /* Reveal Datum boxes in place; the dict is freshly owned and value
     replacement of existing keys is iteration-safe. Only pair-spine
     subjects produce boxed captures. */
  Py_ssize_t pos = 0;
  PyObject *key = NULL;
  PyObject *val = NULL;
  while (PyDict_Next(caps, &pos, &key, &val)) {
    if (KosDatum_Check(val)) {
      PyObject *inner = ((KosDatum *) val)->datum;
      if (inner && unlikely(PyDict_SetItem(caps, key, inner))) {
        Py_DECREF(caps);
        return EVAL_RAISE();
      }
    }
  }

  int r = KosNamespace_StoreDict(ns, caps);
  Py_DECREF(caps);
  if (unlikely(r < 0))
    return EVAL_RAISE();
  return EVAL_NORMAL();
}


// The end.
