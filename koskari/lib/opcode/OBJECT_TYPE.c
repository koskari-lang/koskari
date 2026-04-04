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
   OBJECT_TYPE: arg unused. Pop TOS, push type(TOS).
*/

KOS_OP_HANDLER(OBJECT_TYPE) {
  PyObject *val = KosStackBlock_PopRef(&CURRENT_STACK(ctx));
  if (! val)
    return KOS_OP_POST_ERR();

  PyObject *res = (PyObject *) Py_TYPE(val);
  Py_INCREF(res);
  Py_DECREF(val);
  (void) insn;
  return kos_op_outcome_from_int_step(push_result(ctx, res));
}
