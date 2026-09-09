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

/**
  Consume a pending KOS_INJECT_THROW: ownership of both refs transfers
  to the caller (slots are nulled without decref).

  :return: 1 when a throw was taken, 0 when none pending
*/
static inline int KosEvalState_TakeInjectThrow(
    KosEvalState *es,
    PyObject **type_out,
    PyObject **value_out) {

  KosContext *ctx = KosEvalState_CONTEXT(es);

  if (KosContext_INJECT_MODE(ctx) != KOS_INJECT_THROW)
    return 0;
  *value_out = KosContext_INJECT_VALUE(ctx);
  *type_out = KosContext_INJECT_TYPE(ctx);
  KosContext_INJECT_MODE(ctx) = KOS_INJECT_NONE;
  KosContext_INJECT_VALUE(ctx) = NULL;
  KosContext_INJECT_TYPE(ctx) = NULL;
  return 1;
}


int KosEvalState_SetFrameCodeAndNamespace(
    KosEvalState *es,
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
    KosDynNode_CAPS(frame) |= KOS_DYN_CAP_BINDS_NS;
  else
    KosDynNode_CAPS(frame) &= ~KOS_DYN_CAP_BINDS_NS;
  if (KosEvalState_FRAME(es) == frame) {
    KosEvalState_NAMESPACE(es) = ns;
    KosEvalState_CODE(es) = KosDynFrame_CODE(frame);
    KosEvalState_BLOCK(es) = &KosDynFrame_STACK(frame);
  }
  return 0;
}


void KosEvalState_PythonAwaitClearIter(KosEvalState *es) {
  KosContext *ctx = KosEvalState_CONTEXT(es);
  Py_CLEAR(KosContext_PY_AWAIT_ITER(ctx));
}


PyObject *KosEvalState_PythonAwaitIter(KosEvalState *es) {
  return KosContext_PY_AWAIT_ITER(KosEvalState_CONTEXT(es));
}


void KosEvalState_PythonResumeSet(KosEvalState *es, PyObject *val, uint8_t kind) {
  KosContext *ctx = KosEvalState_CONTEXT(es);

  Py_XDECREF(KosContext_PY_RESUME_INJECT(ctx));
  KosContext_PY_RESUME_INJECT(ctx) = val;
  KosContext_PY_RESUME_KIND(ctx) = kind;
}


static inline void KosEvalState_PythonAwaitSetIter(
    KosEvalState *es,
    PyObject *iter) {

  KosContext *ctx = KosEvalState_CONTEXT(es);

  Py_XDECREF(KosContext_PY_AWAIT_ITER(ctx));
  KosContext_PY_AWAIT_ITER(ctx) = iter;
}


static inline int KosEvalState_PythonResumeTake(
    KosEvalState *es,
    PyObject **out_val,
    uint8_t *out_kind) {

  KosContext *ctx = KosEvalState_CONTEXT(es);

  if (out_val)
    *out_val = KosContext_PY_RESUME_INJECT(ctx);
  if (out_kind)
    *out_kind = KosContext_PY_RESUME_KIND(ctx);
  KosContext_PY_RESUME_INJECT(ctx) = NULL;
  KosContext_PY_RESUME_KIND(ctx) = KOS_RESUME_NONE;
  return (out_val && *out_val != NULL) ? 1 : 0;
}


#define KosEvalState_EnterDomainScope(es) \
  kos_domain_push_scope(KosEvalState_CONTEXT(es))


#define KosEvalState_LeaveDomainScope(es) \
  kos_domain_pop_scope(KosEvalState_CONTEXT(es))


KosEvaluatorToken *KosEvalState_Token(const KosEvalState *es) {
  if (! es)
    return NULL;
  return KosContext_TOKEN(KosEvalState_CONTEXT(es));
}


/**
  Drive the armed transfer goal as far as the chain allows and route its
  answer: a step failure enters the raise ingress, an awaitable is tagged
  for the driver, and a foreign cc-unwind's boundary raise goes back as
  it stands.

  :return: EVAL_NORMAL when no goal remains or the goal is parked;
           EVAL_REPROLOGUE when a leave or enter installed a frame;
           EVAL_AWAIT / EVAL_RAISE / EVAL_FATAL otherwise
*/
static KosOpResult kos_eval_drain_goal(KosEvalState *es) {

  int boundary = 0;
  KosOpResult r = kos_xfer_goal_drive(es, &boundary);

  if (boundary)
    return r;
  return kos_eval_walk_outcome(es, r);
}


// The end.
