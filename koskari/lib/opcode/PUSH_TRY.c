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
   PUSH_TRY: arg = target label for exception handler. Push a TRY region
   onto the dynamic chain.
*/

KOS_OP_HANDLER(PUSH_TRY) {
  KosContext *ctx = KosEvalState_Context(es);
  KosDynNode *region = (KosDynNode *) kos_dyn_push_try(ctx, KosOp_ARG(insn));
  if (! region) {
    PyErr_NoMemory();
    return KOS_OP_POST_ERR();
  }
  KosDynNode_STACK(region) = KosEvalState_OperandNewEmpty();
  if (! KosDynNode_STACK(region)) {
    if (kos_dyn_abort_push_region(ctx, region) < 0) {
      PyErr_Clear();
      PyErr_SetString(PyExc_RuntimeError,
                      "PUSH_TRY: unexpected region kind during abort");
    }
    return KOS_OP_POST_ERR();
  }
  kos_ctx_refresh_dyn_caches(ctx);
  (void) out_ev;
  return KOS_OP_RUN();
}
