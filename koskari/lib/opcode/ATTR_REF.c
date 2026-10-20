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
   ATTR_REF: arg = constant index (name). Push attribute reference.
*/

KOS_OP_HANDLER(ATTR_REF) {
  KOS_SYNC_PC(ctx, insn);
  PyObject *name = KosOp_OBJ(insn);
  PyObject *obj = KosEvalState_StackPopRef(ctx);
  if (! obj)
    RESULT_RAISE();
  PyObject *ref = KosAttributeRef_New(name, obj, name);
  Py_DECREF(obj);
  if (! ref)
    RESULT_RAISE();
  RESULT_STEAL(ctx, ref);
}


// The end.
