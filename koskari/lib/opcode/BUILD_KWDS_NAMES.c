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
   BUILD_KWDS_NAMES: arg = constant pool index of names tuple.
   Stack: [..., v_0, ..., v_{n-1}] -> [..., dict].
   Pop n values and map names[i] -> value[i]; push the resulting dict.
*/

KOS_OP_HANDLER(BUILD_KWDS_NAMES) {
  PyObject *names = KosCode_GetConstant((PyObject *) CURRENT_CODE(ctx),
                                         KosOp_ARG(insn));

  if (! names)
    return -1;

  if (! PyTuple_CheckExact(names)) {
    PyErr_Format(PyExc_TypeError, "BUILD_KWDS_NAMES names must be tuple, not %R",
                 names);
    return -1;
  }

  PyObject *kwds = KosStackBlock_PopTailDictFromNames(&CURRENT_STACK(ctx),
                                                       names);

  if (! kwds)
    return -1;

  if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), kwds) < 0) {
    Py_DECREF(kwds);
    return -1;
  }
  Py_DECREF(kwds);

  (void) out_ev;
  return 0;
}
