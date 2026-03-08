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
   VAR_OBJECT_ADD: arg = constant index (name). Pop TOS (rhs), fetch obj
   from name in frame, PyNumber_InPlaceAdd(obj, rhs), store result back.
   Does not push.
*/

KOS_OP_HANDLER(VAR_OBJECT_ADD) {
  PyObject *name = get_name_from_insn(CURRENT_CODE(ctx), insn);
  if (! name)
    return -1;
  PyObject *rhs = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! rhs)
    return -1;
  PyObject *obj = frame_ns_deref(ctx, name);
  if (! obj) {
    Py_DECREF(rhs);
    return -1;
  }
  PyObject *result = PyNumber_InPlaceAdd(obj, rhs);
  Py_DECREF(obj);
  Py_DECREF(rhs);
  if (! result)
    return -1;
  if (frame_ns_store(ctx, name, result) < 0) {
    Py_DECREF(result);
    return -1;
  }
  Py_DECREF(result);
  (void) out_ev;
  return 0;
}

// The end.
