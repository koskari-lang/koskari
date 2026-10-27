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
  koskari/lib/eval/eval_chain.c

  Transfer plan and delimited-return paths; included into eval.c.

  :author: Christopher O'Brien <obriencj@preoccupied.net>
  :license: GNU General Public License v3
*/


/* --- Transfer plan and delimited return --- */

int KosContext_ThrowIntoResume(KosContext *ctx) {

  KosDynFrame *frame = KosContext_FRAME(ctx);
  const KosResumeKind *kind = frame ? KosDynFrame_RESUME_KIND(frame) : NULL;

  if (! kind || ! kind->throw_kind)
    return 0;
  if (PyErr_ExceptionMatches(PyExc_GeneratorExit))
    return 0;

  /* The throw is the delivery: the throw kind takes the exception
     object as its value, over the same state. */
  KosDynFrame_RESUME_KIND(frame) = kind->throw_kind;
  KosContext_StageDelivery(ctx, PyErr_GetRaisedException());
  return 1;
}


int KosContext_SetFrameCodeAndNamespace(
    KosContext *ctx,
    KosDynFrame *frame,
    KosCode *code,
    PyObject *ns) {

  Py_XDECREF(KosDynFrame_NS(frame));
  KosDynFrame_NS(frame) = ns;
  Py_XINCREF(code);
  Py_XDECREF((PyObject *) KosDynFrame_CODE(frame));
  KosDynFrame_CODE(frame) = code;
  /* A placeholder became a code frame introducing ns, or a tail reset
     is being refilled: the cap follows the code, as at construction. */
  if (code)
    KosDynNode_CAPS(frame) |= KOS_DYN_CAP_OWNS_NS;
  else
    KosDynNode_CAPS(frame) &= ~KOS_DYN_CAP_OWNS_NS;
  if (KosContext_FRAME(ctx) == frame) {
    KosContext_NAMESPACE(ctx) = ns;
    KosContext_BLOCK(ctx) = &KosDynFrame_STACK(frame);
  }
  return 0;
}


void KosContext_PythonAwaitClearIter(KosContext *ctx) {
  Py_CLEAR(KosContext_PY_AWAIT_ITER(ctx));
}


PyObject *KosContext_PythonAwaitIter(KosContext *ctx) {
  return KosContext_PY_AWAIT_ITER(ctx);
}


void KosContext_PythonResumeSet(KosContext *ctx, PyObject *val) {

  Py_XDECREF(KosContext_PY_RESUME_INJECT(ctx));
  KosContext_PY_RESUME_INJECT(ctx) = val;
}


static inline int KosContext_PythonResumeTake(
    KosContext *ctx,
    PyObject **out_val) {

  if (out_val)
    *out_val = KosContext_PY_RESUME_INJECT(ctx);
  KosContext_PY_RESUME_INJECT(ctx) = NULL;
  return (out_val && *out_val != NULL) ? 1 : 0;
}


KosEvaluatorToken *KosContext_Token(const KosContext *ctx) {
  if (! ctx)
    return NULL;
  return KosContext_TOKEN(ctx);
}


/**
  Drive the active transfer goal as far as the chain allows and route its
  answer: a step failure enters the raise ingress, an awaitable is tagged
  for the driver, and a foreign cc-unwind's boundary raise goes back as
  it stands.

  :return: EVAL_NORMAL when no goal remains or the goal is waiting;
           EVAL_REPROLOGUE when a leave or enter installed a frame;
           EVAL_AWAIT / EVAL_RAISE / EVAL_FATAL otherwise
*/
static KosOpResult kos_eval_drain_goal(KosContext *ctx) {

  int boundary = 0;
  KosOpResult r = kos_xfer_goal_drive(ctx, &boundary);

  if (boundary)
    return r;
  return kos_eval_walk_outcome(ctx, r);
}


// The end.
