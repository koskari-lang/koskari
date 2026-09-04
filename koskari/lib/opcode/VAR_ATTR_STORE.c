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
   VAR_ATTR_STORE: arg = extra index. Schema VAR (obj name), CONST (attr name).
   setattr(obj, attr, TOS) where obj = frame_ns_deref(var_name). Pop val.
*/

KOS_OP_HANDLER_COLD(VAR_ATTR_STORE) {
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *var_name = KosExtra_OBJ(row, 0);
  PyObject *attr_name = KosExtra_OBJ(row, 1);
  PyObject *val = KosEvalState_StackPopRef(es);
  if (! val)
    RESULT_RAISE();
  PyObject *obj = frame_ns_deref(es, var_name);
  if (! obj) {
    Py_DECREF(val);
    RESULT_RAISE();
  }

  /* Per-site storage-location cache: a SLOT hit writes the __slots__ member,
     an INSTANCE_VALUE hit overwrites the inline attribute value, both
     directly; GENERIC (and a first/materialized store) fall through to
     PyObject_SetAttr. */
  KosCacheSlot *slot = KosOp_SLOT(insn);
  if (slot) {
    uint32_t off;
    KosFieldMode m = kos_resolve_attr_location(obj, attr_name, slot, &off);
    if (m == KOS_FIELD_SLOT) {
      field_slot_store(obj, off, val);
      Py_DECREF(obj);
      Py_DECREF(val);
      RESULT_NEXT(es);
    }
#if PY_MINOR_VERSION >= 11
    if (m == KOS_FIELD_INSTANCE_VALUE
        && field_instance_value_store(obj, off, val)) {
      Py_DECREF(obj);
      Py_DECREF(val);
      RESULT_NEXT(es);
    }
#endif
  }

  int res = PyObject_SetAttr(obj, attr_name, val);
  Py_DECREF(obj);
  Py_DECREF(val);
  if (res < 0)
    RESULT_RAISE();
  RESULT_NEXT(es);
}


// The end.
