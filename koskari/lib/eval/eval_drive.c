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
   koskari/lib/eval/eval_drive.c

   The drive begin step, the parent stack slot, the UNPACK sequence
   helper, and the evaluator token; included into eval.c.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
*/


void KosContext_EvalEnter(KosContext *ctx, PyThreadState *tstate) {
  const volatile KosBreakerWord *breaker;

  KOS_TRACE_EVAL_SYNC_BEGIN();

  if (! tstate)
    tstate = PyThreadState_Get();

  breaker = koskari_eval_breaker_locate_tstate(tstate);
  if (! breaker)
    breaker = &koskari_breaker_always;
  KosContext_BREAKER(ctx) = breaker;
  ctx->terminal = 0;

  KOS_TRACE_EVAL_SYNC_END(KOS_TEVS_init);
}


void KosContext_EvalExit(KosContext *ctx) {

  KosContext_FlushLiveRegsToFrame(ctx);

  /* A staged delivery outlives the drive only as storage on the frame
     it was staged for; the delivery step takes it back from the stack
     on the next trip. That storage is a code-less step frame's alone:
     a bytecode frame's stack holds its operands and the delivery step
     never pops it, so every site that stages for a bytecode frame
     delivers in the same trip and no drive exit can see one. */
  if (KosContext_PENDING_VALUE(ctx)) {
    PyObject *staged = KosContext_PENDING_VALUE(ctx);
    KosContext_PENDING_VALUE(ctx) = NULL;
    if (KOS_EVAL_SAFETYNET
        && KosContext_FRAME(ctx)
        && KosDynFrame_CODE(KosContext_FRAME(ctx))) {
      Py_DECREF(staged);
      PyErr_SetString(PyExc_SystemError,
                      "delivery staged for a bytecode frame across a"
                      " drive exit");
    } else if (KosContext_BLOCK(ctx))
      (void) KosContext_StackPUSH(ctx, staged);  /* consumes on failure */
    else
      Py_DECREF(staged);
  }
}


/* The frame depth limit, KOS_FRAME_DEPTH_DEFAULT until set and settable
   up to the KOS_FRAME_DEPTH_MAX cap, distinct from Python's recursion
   limit. */
unsigned kos_frame_depth_limit = KOS_FRAME_DEPTH_DEFAULT;


/* The message names the limit in force when the install was refused. */
int kos_eval_recursion_error(void) {
  PyErr_Format(PyExc_RecursionError,
               "Koskari frame depth exceeded (%u)",
               kos_frame_depth_limit);
  return -1;
}


/* The eval nesting limit, KOS_EVAL_NESTING_MAX until lowered. */
unsigned kos_eval_nesting_limit = KOS_EVAL_NESTING_MAX;


/* The message names the limit in force when the entry was refused. */
int kos_eval_nesting_error(void) {
  PyErr_Format(PyExc_RecursionError,
               "Koskari eval nesting exceeded (%u)",
               kos_eval_nesting_limit);
  return -1;
}


/* Exactly need items from an iterable into a new tuple. One pull past
   need refuses an over-long iterable at its extra item instead of
   draining it. */
static PyObject *unpack_exact_pull(PyObject *raw, Py_ssize_t need) {

  PyObject *it = PyObject_GetIter(raw);
  if (unlikely(! it)) {
    if (PyErr_ExceptionMatches(PyExc_TypeError)) {
      PyErr_Format(PyExc_TypeError,
                   "cannot unpack non-iterable %.100s object",
                   Py_TYPE(raw)->tp_name);
    }
    return NULL;
  }

  PyObject *seq = PyTuple_New(need);
  if (unlikely(! seq)) {
    Py_DECREF(it);
    return NULL;
  }

  Py_ssize_t got = 0;
  while (got < need) {
    PyObject *item = PyIter_Next(it);
    if (! item) {
      Py_DECREF(it);
      Py_DECREF(seq);
      if (! PyErr_Occurred()) {
        PyErr_Format(PyExc_ValueError,
                     "not enough values to unpack (expected %zd, got %zd)",
                     need, got);
      }
      return NULL;
    }
    PyTuple_SET_ITEM(seq, got, item);
    got++;
  }

  PyObject *extra = PyIter_Next(it);
  Py_DECREF(it);
  if (extra) {
    Py_DECREF(extra);
    Py_DECREF(seq);
    PyErr_Format(PyExc_ValueError,
                 "too many values to unpack (expected %zd)", need);
    return NULL;
  }
  if (unlikely(PyErr_Occurred())) {
    Py_DECREF(seq);
    return NULL;
  }
  return seq;
}


PyObject *KosContext_PopUnpackSequence(
    KosContext *ctx,
    Py_ssize_t need,
    int exact) {

  PyObject *raw = KosContext_StackPopRef(ctx);
  PyObject *seq;
  if (unlikely(! raw))
    return NULL;

  /* a tuple or list is already the fast sequence: keep the popped
     reference instead of asking libpython to hand it back */
  if (PyTuple_CheckExact(raw) || PyList_CheckExact(raw)) {
    seq = raw;
  } else if (exact) {
    seq = unpack_exact_pull(raw, need);
    Py_DECREF(raw);
    return seq;
  } else {
    seq = PySequence_Fast(raw, "");
    if (unlikely(! seq)) {
      if (PyErr_ExceptionMatches(PyExc_TypeError)) {
        PyErr_Format(PyExc_TypeError,
                     "cannot unpack non-iterable %.100s object",
                     Py_TYPE(raw)->tp_name);
      }
      Py_DECREF(raw);
      return NULL;
    }
    Py_DECREF(raw);
  }

  Py_ssize_t slen = PySequence_Fast_GET_SIZE(seq);
  if (unlikely(exact ? (slen != need) : (slen < need))) {
    if (slen > need)
      PyErr_Format(PyExc_ValueError,
                   "too many values to unpack (expected %zd, got %zd)",
                   need, slen);
    else
      PyErr_Format(PyExc_ValueError,
                   "not enough values to unpack (expected %s%zd, got %zd)",
                   exact ? "" : "at least ", need, slen);
    Py_DECREF(seq);
    return NULL;
  }

  return seq;
}


/* === KosEvaluatorToken type === */


static uint64_t token_cookie_counter = 0;


static PyObject *token_repr(PyObject *self) {
  KosEvaluatorToken *t = (KosEvaluatorToken *) self;
  return PyUnicode_FromFormat("<KosEvaluatorToken cookie=%llu live_depth=%d>",
                              (unsigned long long) t->cookie,
                              t->live_depth);
}


PyTypeObject KosEvaluatorTokenType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.EvaluatorToken",
  .tp_basicsize = sizeof(KosEvaluatorToken),
  .tp_itemsize = 0,
  .tp_repr = token_repr,
  .tp_flags = Py_TPFLAGS_DEFAULT,
};


static KosEvaluatorToken *evaluator_token_new(void) {

  KosEvaluatorToken *token = PyObject_New(
    KosEvaluatorToken,
    &KosEvaluatorTokenType);

  if (! token)
    return NULL;

  token->cookie = (uint64_t) __sync_add_and_fetch(&token_cookie_counter, 1);
  token->live_depth = 0;
  return token;
}


void KosEvaluatorToken_IncrementLiveDepth(KosEvaluatorToken *token) {
  if (token)
    token->live_depth++;
}


void KosEvaluatorToken_DecrementLiveDepth(KosEvaluatorToken *token) {
  if (token && token->live_depth > 0)
    token->live_depth--;
}


// The end.
