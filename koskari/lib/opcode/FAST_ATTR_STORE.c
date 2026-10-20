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
   FAST_ATTR_STORE: arg = extra index. Schema FASTVAR (slot), CONST (attr name).
   setattr(obj, attr, TOS) where obj = KosNamespace_FastDeref(ns, slot). Pop val.
*/

KOS_OP_HANDLER(FAST_ATTR_STORE) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *ns = CURRENT_NS();
  KOS_OP_REQUIRE_NS(ns);
  PyObject *attr_name = KosExtra_OBJ(row, 1);
  PyObject *val = KosContext_StackPopRef(ctx);
  if (! val)
    RESULT_RAISE();
  size_t idx = (size_t) KosExtra_IMM(row, 0);
  KosCacheSlot *slot = KosOp_SLOT(insn);

  /* Per-site storage-location cache on the receiver peeked borrowed: a
     SLOT hit writes the __slots__ member, an INSTANCE_VALUE hit
     overwrites the inline attribute value, both directly, and the
     popped reference moves into the field. The resolve runs no user
     code, and the receiver is not touched after the store, so the
     displaced value's release cannot invalidate it. A peek the slot
     cannot answer, a GENERIC location, and a first or materialized
     store take the owned path below. */
  if (slot) {
    PyObject *obj = kos_ns_fast_peek(ns, idx);
    if (obj) {
      uint32_t off;
      KosFieldMode m = kos_resolve_attr_location(obj, attr_name, slot, &off);
      if (m == KOS_FIELD_SLOT) {
        field_slot_store(obj, off, val);      /* val stolen */
        RESULT_NEXT(ctx);
      }
#if PY_MINOR_VERSION >= 11
      if (m == KOS_FIELD_INSTANCE_VALUE
          && field_instance_value_store(obj, off, val)) {
        RESULT_NEXT(ctx);                      /* val stolen */
      }
#endif
    }
  }

  /* Owned path: the deref raises for an empty slot and runs a ref's own
     deref; setattr may run anything, so the receiver is held across it. */
  PyObject *obj = kos_ns_fast_deref(ns, idx);
  if (! obj) {
    Py_DECREF(val);
    RESULT_RAISE();
  }

  int res = PyObject_SetAttr(obj, attr_name, val);
  Py_DECREF(obj);
  Py_DECREF(val);
  if (res < 0)
    RESULT_RAISE();
  RESULT_NEXT(ctx);
}


// The end.
