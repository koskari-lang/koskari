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
   PUSH_ASIDE: arg = constant pool index of a NamespaceSpec, or of
   None. Cache: the extent's close.

   The instructions from the next one up to their RETURN run in an
   aside: a frame of its own over this frame's code. With a spec, its
   namespace is built from the spec, whose closure names are resolved
   from the live namespace, and the live namespace is not held. With
   None, which is what the compiler writes for an aside that owns no
   name, it runs over the live namespace. This frame is left standing at the
   POP_ASIDE that closes the extent, and goes on from there with the
   aside's value when the aside returns to it. An aside that aborts
   (ABORT, SHIFT_VAR, SHIFT_FAST) returns to the frame under the
   prompt instead.
*/

KOS_OP_HANDLER(PUSH_ASIDE) {
  KOS_SYNC_PC(ctx, insn);
  KosOp *entry = KosContext_PC(ctx);
  KosOp *pop;
  PyObject *nspec = KosOp_OBJ(insn);
  PyObject *ns;
  KosDynFrame *aside;

  if (kos_site_extent_close(ctx, insn, KOS_EXT_ASIDE, "PUSH_ASIDE", &pop) < 0)
    RESULT_RAISE();

  if (nspec == Py_None) {
    /* the position this frame keeps while the aside runs */
    KosContext_SET_PC(ctx, pop);

    if (! kos_dyn_push_aside(ctx, entry, NULL, 0))
      RESULT_RAISE();
    RESULT_CONTINUE(ctx);
  }

  if (unlikely(! KosNamespaceSpec_CheckExact(nspec))) {
    PyErr_Format(PyExc_RuntimeError,
                 "PUSH_ASIDE spec must be a NamespaceSpec or None, not %R",
                 nspec);
    RESULT_RAISE();
  }

  ns = KosNamespace_FromSpec(nspec, CURRENT_NS());
  if (unlikely(! ns))
    RESULT_RAISE();

  KosContext_SET_PC(ctx, pop);

  /* a namespace built here is the aside's to report to the collector */
  aside = kos_dyn_push_aside(ctx, entry, ns, 1);
  Py_DECREF(ns);
  if (! aside)
    RESULT_RAISE();

  RESULT_CONTINUE(ctx);
}


// The end.
