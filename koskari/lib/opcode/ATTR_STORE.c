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
   ATTR_STORE: arg = constant index (name). setattr(TOS1, name, TOS); pop
   val then obj.

   Carries one per-site field cache, as FAST_ATTR_STORE does: a SLOT hit
   writes the __slots__ member, an INSTANCE_VALUE hit overwrites the
   inline attribute value, both directly, and the popped reference moves
   into the field. A GENERIC location and a first or materialized store
   take the setattr path.
*/

KOS_OP_HANDLER(ATTR_STORE) {
  KOS_SYNC_PC(es, insn);
  PyObject *name = KosOp_OBJ(insn);
  PyObject *val = KosEvalState_StackPopRef(es);
  if (! val)
    RESULT_RAISE();
  PyObject *const *tail = KosEvalState_StackTailPtr(es, 1);
  if (unlikely(! tail)) {
    Py_DECREF(val);
    RESULT_RAISE();
  }
  PyObject *obj = tail[0];
  KosCacheSlot *slot = KosOp_SLOT(insn);

  /* The resolve runs no user code, and the receiver is not touched
     after the store, so the displaced value's release cannot
     invalidate it. */
  if (slot) {
    uint32_t off;
    KosFieldMode m = kos_resolve_attr_location(obj, name, slot, &off);
    if (m == KOS_FIELD_SLOT) {
      field_slot_store(obj, off, val);      /* val stolen */
      KosEvalState_StackDropNoErr(es, 1);
      RESULT_NEXT(es);
    }
#if PY_MINOR_VERSION >= 11
    if (m == KOS_FIELD_INSTANCE_VALUE
        && field_instance_value_store(obj, off, val)) {
      KosEvalState_StackDropNoErr(es, 1);   /* val stolen */
      RESULT_NEXT(es);
    }
#endif
  }

  /* Generic path: setattr may run anything, so the receiver stays on
     the stack across it; a raise leaves it for the unwind to release. */
  int res = PyObject_SetAttr(obj, name, val);
  Py_DECREF(val);
  if (res < 0)
    RESULT_RAISE();
  KosEvalState_StackDropNoErr(es, 1);
  RESULT_NEXT(es);
}


// The end.
