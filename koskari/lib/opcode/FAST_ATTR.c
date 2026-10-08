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
   FAST_ATTR: arg = extra index. Schema FASTVAR (slot), CONST (attr name).
   Push getattr(obj, attr) where obj = KosNamespace_FastDeref(ns, slot).
*/

KOS_OP_HANDLER(FAST_ATTR) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *ns = CURRENT_NS();
  KOS_OP_REQUIRE_NS(ns);
  PyObject *attr_name = KosExtra_OBJ(row, 1);
  size_t idx = (size_t) KosExtra_IMM(row, 0);
  KosCacheSlot *slot = KosOp_SLOT(insn);

  /* Per-site storage-location cache on the receiver peeked borrowed: a
     SLOT hit reads the __slots__ member, an INSTANCE_VALUE hit the
     inline attribute value, both directly, and only the value's
     reference moves. The resolve, cold path included, runs type
     lookups and no user code, so nothing between the peek and the read
     can rebind the slot. A peek the slot cannot answer (empty, or a ref
     kind with its own deref), a GENERIC or unset location, and a
     materialized instance all take the owned path below. */
  if (slot) {
    PyObject *obj = kos_ns_fast_peek(ns, idx);
    if (obj) {
      uint32_t off;
      KosFieldMode m = kos_resolve_attr_location(obj, attr_name, slot, &off);
      PyObject *hit = NULL;
      if (m == KOS_FIELD_SLOT)
        hit = field_slot_load(obj, off);
#if PY_MINOR_VERSION >= 11
      else if (m == KOS_FIELD_INSTANCE_VALUE)
        hit = field_instance_value_load(obj, off);
#endif
      if (hit) {
        Py_INCREF(hit);
        RESULT_STEAL(es, hit);
      }
    }
  }

  /* Owned path: the deref raises for an empty slot and runs a ref's own
     deref; getattr may run anything (AttributeError, __getattr__, a
     materialized dict, a shadowed class attribute), so the receiver is
     held across it. */
  PyObject *obj = kos_ns_fast_deref(ns, idx);
  if (! obj)
    RESULT_RAISE();
  PyObject *val = PyObject_GetAttr(obj, attr_name);
  Py_DECREF(obj);
  if (! val)
    RESULT_RAISE();
  RESULT_STEAL(es, val);
}


// The end.
