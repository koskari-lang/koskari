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

KOS_OP_HANDLER(VAR_ATTR_STORE) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  PyObject *var_name = KosCode_GetConstant((PyObject *) code, row.a0);
  PyObject *attr_name = KosCode_GetConstant((PyObject *) code, row.a1);
  if (! var_name || ! attr_name)
    return EVAL_RAISE();
  PyObject *val = KosEvalState_StackPopRef(es);
  if (! val)
    return EVAL_RAISE();
  PyObject *obj = frame_ns_deref(es, var_name);
  if (! obj) {
    Py_DECREF(val);
    return EVAL_RAISE();
  }

  /* Per-site storage-location cache: a SLOT hit writes the __slots__ member,
     an INSTANCE_VALUE hit overwrites the inline attribute value, both
     directly; GENERIC (and a first/materialized store) fall through to
     PyObject_SetAttr. */
  uint16_t cache_ix = KosOp_CACHE(insn);
  if (cache_ix != KOS_CACHE_UNSET) {
    KosCacheSlot *slot = &KosCode_CACHE(code)[cache_ix];
    uint32_t off;
    KosFieldMode m = kos_resolve_attr_location(obj, attr_name, slot, &off);
    if (m == KOS_FIELD_SLOT) {
      field_slot_store(obj, off, val);
      Py_DECREF(obj);
      Py_DECREF(val);
      return EVAL_NORMAL();
    }
#if PY_MINOR_VERSION >= 12
    if (m == KOS_FIELD_INSTANCE_VALUE
        && field_instance_value_store(obj, off, val)) {
      Py_DECREF(obj);
      Py_DECREF(val);
      return EVAL_NORMAL();
    }
#endif
  }

  int res = PyObject_SetAttr(obj, attr_name, val);
  Py_DECREF(obj);
  Py_DECREF(val);
  if (res < 0)
    return EVAL_RAISE();
  return EVAL_NORMAL();
}


// The end.
