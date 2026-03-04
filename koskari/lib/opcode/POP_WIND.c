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
   POP_WIND: arg unused. Pop the top wind node from the dynamic chain.
   Invokes the after thunk if entered and non-null; may return
   KOS_DYN_UNWIND_ASYNC_STARTED when the thunk pushes a frame.
*/

KOS_OP_HANDLER(POP_WIND) {
  if (! KosContext_DYN_TOP(ctx) || ! KosDynNode_IS_WIND(KosContext_DYN_TOP(ctx))) {
    PyErr_SetString(PyExc_RuntimeError, "POP_WIND: top is not a wind node");
    return -1;
  }

  int r = kos_dyn_pop_top(ctx, out_ev);
  if (r == KOS_DYN_UNWIND_ASYNC_STARTED) {
    KosDynNode *wind = KosDynNode_PARENT(CURRENT_FRAME(ctx));
    KosContext_UNWIND_TARGET_FRAME(ctx) = (KosDynFrame *) KosDynNode_PARENT(wind);
    return 0;  /* eval continues in thunk frame; RETURN will resume unwinding */
  }
  return r;
}
