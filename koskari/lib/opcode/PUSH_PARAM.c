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
  KosContext *ctx = KosEvalState_Context(es);
  PyObject *names = KosCode_GetConstant((PyObject *) CURRENT_CODE(ctx),
                                         KosOp_ARG(insn));
  if (! names) {
    return KOS_OP_POST_ERR();
  }

  PyObject *bindings = KosEvalState_StackPopTailDictFromNames(es, names);
  if (! bindings)
    return KOS_OP_POST_ERR();

  if (! kos_dyn_push_param_dict(ctx, bindings)) {
    Py_DECREF(bindings);
    return KOS_OP_POST_ERR();
  }

  (void) out_ev;
  return KOS_OP_RUN();
}


// The end.
