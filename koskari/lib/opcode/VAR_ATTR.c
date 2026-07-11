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
   VAR_ATTR: arg = extra index. Schema VAR (obj name), CONST (attr name).
   Push getattr(obj, attr) where obj = frame_ns_deref(var_name).
*/

KOS_OP_HANDLER(VAR_ATTR) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));
  PyObject *var_name = KosCode_GetConstant((PyObject *) code, row.a0);
  PyObject *attr_name = KosCode_GetConstant((PyObject *) code, row.a1);
  if (! var_name || ! attr_name)
    return EVAL_RAISE();
  PyObject *obj = frame_ns_deref(es, var_name);
  if (! obj)
    return EVAL_RAISE();

  /* Per-site storage-location cache: a SLOT hit reads the __slots__ member
     directly; GENERIC (and unset slots) fall through to PyObject_GetAttr. */
  uint16_t cache_ix = KosOp_CACHE(insn);
  if (cache_ix != KOS_CACHE_UNSET) {
    KosCacheSlot *slot = &KosCode_CACHE(code)[cache_ix];
    uint32_t off;
    if (kos_resolve_attr_location(obj, attr_name, slot, &off) == KOS_FIELD_SLOT) {
      PyObject *hit = field_slot_load(obj, off);
      if (hit) {
        Py_INCREF(hit);
        Py_DECREF(obj);
        return kos_op_result_PUSH(es, hit);
      }
      /* unset slot: fall through (AttributeError or __getattr__). */
    }
  }

  PyObject *val = PyObject_GetAttr(obj, attr_name);
  Py_DECREF(obj);
  if (! val)
    return EVAL_RAISE();
  return kos_op_result_PUSH(es, val);
}


// The end.
