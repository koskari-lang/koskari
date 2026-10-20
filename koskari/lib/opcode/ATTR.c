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
   ATTR: arg = constant index (name). Replace TOS with getattr(TOS, name).

   Carries one per-site field cache, as FAST_ATTR does: a SLOT hit reads
   the __slots__ member, an INSTANCE_VALUE hit the inline attribute
   value, both directly, and the value replaces the receiver in place.
   The receiver comes off the stack rather than a named local, so the
   site may be polymorphic; the field cache degrades to its megamorphic
   bypass when it is.
*/

KOS_OP_HANDLER(ATTR) {
  KOS_SYNC_PC(ctx, insn);
  PyObject *name = KosOp_OBJ(insn);
  KosSegment **bl = KosContext_BLOCK(ctx);
  PyObject *const *tail = kos_stack_tail_ptr_fast(*bl, 1);
  KosCacheSlot *slot = KosOp_SLOT(insn);

  if (unlikely(! tail))
    RESULT_RAISE();
  PyObject *obj = tail[0];

  /* The resolve runs type lookups and no user code, so nothing between
     the peek and the read can rebind the receiver. */
  if (slot) {
    uint32_t off;
    KosFieldMode m = kos_resolve_attr_location(obj, name, slot, &off);
    PyObject *hit = NULL;
    if (m == KOS_FIELD_SLOT)
      hit = field_slot_load(obj, off);
#if PY_MINOR_VERSION >= 11
    else if (m == KOS_FIELD_INSTANCE_VALUE)
      hit = field_instance_value_load(obj, off);
#endif
    if (hit) {
      Py_INCREF(hit);
      kos_elector_replace_top(bl, hit);
      RESULT_NEXT(ctx);
    }
  }

  /* Generic path: getattr may run anything, so the receiver stays on
     the stack across it; a raise leaves it for the unwind to release. */
  PyObject *val = PyObject_GetAttr(obj, name);
  if (! val)
    RESULT_RAISE();
  kos_elector_replace_top(bl, val);
  RESULT_NEXT(ctx);
}


// The end.
