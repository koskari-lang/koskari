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
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *ns = CURRENT_NS();
  KOS_OP_REQUIRE_NS(ns);
  PyObject *attr_name = KosExtra_OBJ(row, 1);
  if (! attr_name)
    RESULT_RAISE();
  size_t idx = (size_t) KosExtra_IMM(row, 0);
  PyObject *obj = kos_ns_fast_deref(ns, idx);
  if (! obj)
    RESULT_RAISE();

  /* Per-site storage-location cache: a SLOT hit reads the __slots__ member,
     an INSTANCE_VALUE hit the inline attribute value, both directly; GENERIC,
     unset, and materialized instances fall through to PyObject_GetAttr. */
  KosCacheSlot *slot = KosOp_SLOT(insn);
  if (slot) {
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
      Py_DECREF(obj);
      RESULT_STEAL(es, hit);
    }
    /* unset/materialized: fall through (AttributeError, __getattr__,
       materialized dict, or a shadowed class attribute). */
  }

  PyObject *val = PyObject_GetAttr(obj, attr_name);
  Py_DECREF(obj);
  if (! val)
    RESULT_RAISE();
  RESULT_STEAL(es, val);
}


// The end.
