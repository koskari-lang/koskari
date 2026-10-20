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
   PUSH_PARAM: arg = constant index of names tuple.
   Stack: [..., val_0, val_1, ..., val_{n-1}] -> [].
   Pops values, builds dict name -> value, pushes KOS_DYN_PARAM extent.
*/

KOS_OP_HANDLER(PUSH_PARAM) {
  KOS_SYNC_PC(ctx, insn);
  PyObject *names = KosOp_OBJ(insn);

  PyObject *bindings = KosEvalState_StackPopTailDictFromNames(ctx, names);
  if (! bindings)
    RESULT_RAISE();

  /* the node wraps each value in a fresh cell; bindings stays ours */
  KosDynParam *node = kos_dyn_push_param_dict(ctx, bindings);
  Py_DECREF(bindings);
  if (! node)
    RESULT_RAISE();

  RESULT_NEXT(ctx);
}


// The end.
