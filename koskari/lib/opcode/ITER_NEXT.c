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
   ITER_NEXT: region-based loop step; arg = else PC. Same async-gen vs
   Python async vs sync iterator dispatch as NEXT. Branch to else_pc on
   exhaustion. Must be inside PUSH_FOREACH block. Auto-await.
*/

KOS_OP_HANDLER(ITER_NEXT) {
  KosDynRegion *region = kos_find_region_for_loop(ctx);
  if (! region || KosDynRegion_STYLE(region) != KOS_REGION_FOREACH) {
    PyErr_SetString(PyExc_RuntimeError,
                    "ITER_NEXT must be used inside a PUSH_FOREACH block");
    return -1;
  }

  PyObject *iter = KosDynRegion_ITER(region);
  if (KosPyAsyncGen_Check(iter)) {
    KosPyAsyncGen *agen = (KosPyAsyncGen *) iter;
    if (agen->in_progress) {
      PyErr_SetString(PyExc_RuntimeError,
                      "async generator already being iterated");
      return -1;
    }
    if (agen->closed || ! agen->ctx) {
      KosDynRegion_EXHAUSTED(region) = 1;
      CURRENT_PC(ctx) = insn->arg;
      return 0;
    }
    agen->in_progress = 1;
    KosContext_PENDING_AGEN(ctx) = (void *) agen;
    KosContext_PENDING_AGEN_DEFAULT(ctx) = NULL;
    KosContext_PENDING_AGEN_N(ctx) = 0;
    KosDynRegion_ITER_NEXT_ELSE_PC(region) = insn->arg;
    if (op_attach_koskari_agen_frame_for_iter_next(ctx, agen, region) < 0) {
      agen->in_progress = 0;
      KosContext_PENDING_AGEN(ctx) = NULL;
      return -1;
    }
    return 0;
  }

  PyObject *value = NULL;
  KosGetNextResult get_res = op_get_next_from_iter(ctx, out_ev, iter, &value);
  switch (get_res) {
    case GET_NEXT_VALUE:
      if (KosStackBlock_PushRef(&CURRENT_STACK(ctx), value) < 0) {
        Py_DECREF(value);
        return -1;
      }
      Py_DECREF(value);
      return 0;
    case GET_NEXT_EXHAUSTED:
      KosDynRegion_EXHAUSTED(region) = 1;
      CURRENT_PC(ctx) = insn->arg;
      return 0;
    case GET_NEXT_SUSPEND:
      return 1;
    case GET_NEXT_ERROR:
      return -1;
    default:
      return -1;
  }
}
