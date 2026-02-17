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
   PARAM: arg = constant index of name (ParamRef key).
   Loads ParamRef for that key and pushes it onto the stack.
*/

KOS_OP_HANDLER(PARAM) {
  PyObject *key = KosCode_GetConstant((PyObject *) CURRENT_CODE(ctx),
                                       KosOp_ARG(insn));
  if (! key)
    return -1;

  PyObject *param_ref = KosParamRef_New(key, Py_None);
  if (! param_ref)
    return -1;

  return push_result(ctx, param_ref);
}


// The end.
