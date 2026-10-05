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
  koskari/lib/cont.c

  KosPyDelimitedContinuation and KosPyContinuation. Python wrappers
  for captured continuations (shift and call/cc).

  :author: Christopher O'Brien <obriencj@preoccupied.net>
  :license: GNU General Public License v3
*/


#include "lib.h"


/* KoskariContinuationApply payload attributes (read in eval.c
   handle_continuation_apply) */
static PyObject *str_args = NULL;
static PyObject *str_continuation = NULL;
static PyObject *str_target = NULL;


/* === KosPyDelimitedContinuation type === */


static void delimited_continuation_dealloc(PyObject *self) {
  KosPyDelimitedContinuation *cont = (KosPyDelimitedContinuation *) self;

  KosContinuation *k = KosPyDelimitedContinuation_CAPTURED(cont);

  /* the captured chain's teardown can run arbitrary Python; leave the
     collector's lists and unlink the field before it starts */
  PyObject_GC_UnTrack(self);
  KosPyDelimitedContinuation_CAPTURED(cont) = NULL;
  kos_continuation_free(k);

  Py_TYPE(self)->tp_free(self);
}


static int delimited_continuation_traverse(
    PyObject *self,
    visitproc visit,
    void *arg) {

  KosPyDelimitedContinuation *cont = (KosPyDelimitedContinuation *) self;

  if (KosPyDelimitedContinuation_CAPTURED(cont)) {
    return kos_continuation_traverse(KosPyDelimitedContinuation_CAPTURED(cont), visit, arg);
  }

  return 0;
}


static int delimited_continuation_clear(PyObject *self) {

  KosPyDelimitedContinuation *cont = (KosPyDelimitedContinuation *) self;
  KosContinuation *k = KosPyDelimitedContinuation_CAPTURED(cont);

  /* unlink before the free: its teardown can run Python that reaches
     this wrapper */
  KosPyDelimitedContinuation_CAPTURED(cont) = NULL;
  kos_continuation_free(k);

  return 0;
}


static PyObject *delimited_continuation_call(
    PyObject *self,
    PyObject *args,
    PyObject *kwds) {

  (void) args;
  (void) kwds;
  (void) self;

  PyErr_SetString(PyExc_TypeError,
                  "DelimitedContinuation cannot be called from Python");
  return NULL;
}


static PyObject *delimited_continuation_repr(PyObject *self) {

  return PyUnicode_FromFormat("<DelimitedContinuation at %p>", self);
}


/* === KosContinuation install slot === */


/**
   Raise KoskariContinuationApply carrying the owner token to deliver
   to, the continuation, and the one-item args tuple holding the resume
   value. Every step is checked: a failure leaves that step's own
   exception raised instead of a half-built apply.

   :return: -1 always, with an exception set
*/
static int continuation_raise_apply(
    PyObject *target,
    PyObject *continuation,
    PyObject *resume_val) {

  PyObject *exc;
  PyObject *args_tup;

  if (! KoskariContinuationApply) {
    PyErr_SetString(PyExc_RuntimeError,
                    "KoskariContinuationApply not initialized");
    return -1;
  }

  exc = PyObject_CallNoArgs(KoskariContinuationApply);
  if (! exc)
    return -1;

  if (PyObject_SetAttr(exc, str_target, target) < 0
      || PyObject_SetAttr(exc, str_continuation, continuation) < 0) {
    Py_DECREF(exc);
    return -1;
  }

  args_tup = PyTuple_Pack(1, resume_val);
  if (! args_tup) {
    Py_DECREF(exc);
    return -1;
  }
  if (PyObject_SetAttr(exc, str_args, args_tup) < 0) {
    Py_DECREF(args_tup);
    Py_DECREF(exc);
    return -1;
  }
  Py_DECREF(args_tup);

  PyErr_SetObject(KoskariContinuationApply, exc);
  Py_DECREF(exc);
  return -1;
}


/**
   call_call_continuation: callable and resume_val are borrowed (we
   INCREF resume_val; we own that ref only).  Plan INCREFs callable on
   same-evaluator path; handler releases callable and args.
*/
static KosOpResult call_call_continuation(
    KosEvalState *es,
    PyObject *callable,
    PyObject *resume_val) {

  KosContinuation *k = KosPyDelimitedContinuation_Check(callable)
      ? KosPyDelimitedContinuation_CAPTURED((KosPyDelimitedContinuation *) callable)
      : KosPyContinuation_CAPTURED((KosPyContinuation *) callable);

  Py_INCREF(resume_val);

  /* Defensive: wrappers are only created with non-NULL k at capture time. */
  if (! k) {
    PyErr_SetString(PyExc_RuntimeError,
                    "continuation has no captured state");
    Py_DECREF(resume_val);
    return EVAL_RAISE();
  }

  /* Undelimited: check liveness before apply or raise. */
  if (KosContinuation_PROMPT_ID(k) == KOS_PROMPT_ID_UNDELIMITED) {
    PyObject *token_ref = KosContinuation_TOKEN_REF(k);
    int target_dead = 0;
    if (! token_ref) {
      target_dead = 1;
    } else if (PyObject_TypeCheck(token_ref, &KosEvaluatorTokenType)) {
      KosEvaluatorToken *const token = (KosEvaluatorToken *) token_ref;
      if (token->live_depth == 0)
        target_dead = 1;
    }
    if (target_dead) {
      if (KoskariContinuationDead)
        PyErr_SetString(KoskariContinuationDead,
                        "continuation target context is no longer alive");
      else
        PyErr_SetString(PyExc_RuntimeError,
                        "continuation target context is no longer alive");
      Py_DECREF(resume_val);
      return EVAL_RAISE();
    }
  }

  /* Delimited: two-path invoke on the current chain, whatever context
   * captured it. In-place resume when the capture-time prompt is live
   * in ancestry; otherwise the push model grafts a clone of the
   * template, delimiter included, onto the call site. Both return to
   * the call site on POP_PROMPT. Undelimited on a foreign context goes
   * to its owner through KoskariContinuationApply below. */

  if (KosPyDelimitedContinuation_Check(callable)
      || KosContinuation_TOKEN_REF(k) == (PyObject *) KosEvalState_Token(es)) {
    if (kos_invoke_continuation(es, k, resume_val, callable) < 0) {
      Py_DECREF(resume_val);
      return EVAL_RAISE();
    }
    Py_DECREF(resume_val);
    return EVAL_NORMAL();
  }

  (void) continuation_raise_apply(
      KosContinuation_TOKEN_REF(k), callable, resume_val);
  Py_DECREF(resume_val);
  return EVAL_RAISE();
}


static KosOpResult kos_vinstall_continuation(
    KosEvalState *es,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    int tail_call) {

  PyObject *resume_val;
  KosOpResult r;

  (void) tail_call;

  /* the arguments pack as return's do; the resume takes its own ref */
  resume_val = KosValues_PackVector(args, nargs, kwnames);
  if (! resume_val)
    return EVAL_RAISE();
  r = call_call_continuation(es, callable, resume_val);
  Py_DECREF(resume_val);
  return r;
}


KosEvaluatedTypeObject KosPyDelimitedContinuationType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.DelimitedContinuation",
    .tp_basicsize = sizeof(KosPyDelimitedContinuation),
    .tp_itemsize = 0,
    .tp_dealloc = delimited_continuation_dealloc,
    .tp_repr = delimited_continuation_repr,
    .tp_call = KosCallAdapt_CallAsync,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_doc = "Captured delimited continuation",
    .tp_traverse = delimited_continuation_traverse,
    .tp_clear = delimited_continuation_clear,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_call = {
    .kc_vinstall = kos_vinstall_continuation,
    .kc_pycall = delimited_continuation_call,
    .kc_pyvectorcall = NULL,
  },
};


/**
  KosPyDelimitedContinuation wraps a KosContinuation captured by shift.

  DelimitedContinuation cannot be called from Python; application is only
  permitted via CALL inside koskari_eval.
*/
PyObject *KosPyDelimitedContinuation_New(KosContinuation *captured) {

  KosPyDelimitedContinuation *cont = PyObject_GC_New(
    KosPyDelimitedContinuation,
    KOS_EVALUATED_PYTYPE(KosPyDelimitedContinuationType));

  if (! cont)
    return NULL;

  KosPyDelimitedContinuation_CAPTURED(cont) = captured;

  PyObject_GC_Track((PyObject *) cont);
  return (PyObject *) cont;
}


/* === KosPyContinuation type (undelimited, call/cc) === */


static void continuation_dealloc(PyObject *self) {

  KosPyContinuation *cont = (KosPyContinuation *) self;
  KosContinuation *k = KosPyContinuation_CAPTURED(cont);

  PyObject_GC_UnTrack(self);
  KosPyContinuation_CAPTURED(cont) = NULL;
  kos_continuation_free(k);

  Py_TYPE(self)->tp_free(self);
}


static int continuation_traverse(
    PyObject *self,
    visitproc visit,
    void *arg) {

  KosPyContinuation *cont = (KosPyContinuation *) self;

  if (KosPyContinuation_CAPTURED(cont)) {
    return kos_continuation_traverse(KosPyContinuation_CAPTURED(cont), visit, arg);
  }

  return 0;
}


static int continuation_clear(PyObject *self) {

  KosPyContinuation *cont = (KosPyContinuation *) self;
  KosContinuation *k = KosPyContinuation_CAPTURED(cont);

  /* unlink before the free: its teardown can run Python that reaches
     this wrapper */
  KosPyContinuation_CAPTURED(cont) = NULL;
  kos_continuation_free(k);

  return 0;
}


static PyObject *continuation_call(
    PyObject *self,
    PyObject *args,
    PyObject *kwds) {

  KosPyContinuation *cont = (KosPyContinuation *) self;

  if (! KosPyContinuation_CAPTURED(cont)) {
    PyErr_SetString(PyExc_RuntimeError,
                    "continuation has no captured state");
    return NULL;
  }

  {
    PyObject *token_ref = KosContinuation_TOKEN_REF(KosPyContinuation_CAPTURED(cont));
    int target_dead = 0;
    if (! token_ref) {
      target_dead = 1;
    } else if (PyObject_TypeCheck(token_ref, &KosEvaluatorTokenType)) {
      KosEvaluatorToken *const token = (KosEvaluatorToken *) token_ref;
      if (token->live_depth == 0)
        target_dead = 1;
    }
    if (target_dead) {
      if (KoskariContinuationDead)
        PyErr_SetString(KoskariContinuationDead,
                        "continuation target context is no longer alive");
      else
        PyErr_SetString(PyExc_RuntimeError,
                        "continuation target context is no longer alive");
      return NULL;
    }
  }

  /* the arguments pack as return's do */
  PyObject *resume_val = KosValues_PackTuple(args, kwds);
  if (! resume_val)
    return NULL;

  (void) continuation_raise_apply(
      KosContinuation_TOKEN_REF(KosPyContinuation_CAPTURED(cont)),
      self, resume_val);
  Py_DECREF(resume_val);
  return NULL;
}


static PyObject *continuation_repr(PyObject *self) {

  return PyUnicode_FromFormat("<Continuation at %p>", self);
}


KosEvaluatedTypeObject KosPyContinuationType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "koskari.lib.Continuation",
    .tp_basicsize = sizeof(KosPyContinuation),
    .tp_itemsize = 0,
    .tp_dealloc = continuation_dealloc,
    .tp_repr = continuation_repr,
    .tp_call = KosCallAdapt_CallAsync,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_doc = "Captured undelimited continuation",
    .tp_traverse = continuation_traverse,
    .tp_clear = continuation_clear,
    .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_call = {
    .kc_vinstall = kos_vinstall_continuation,
    .kc_pycall = continuation_call,
    .kc_pyvectorcall = NULL,
  },
};


/**
  KosPyContinuation wraps an undelimited KosContinuation (from call/cc).

  Python-callable: raises KoskariContinuationDead or KoskariContinuationApply.
*/
PyObject *KosPyContinuation_New(KosContinuation *captured) {

  KosPyContinuation *cont = PyObject_GC_New(
    KosPyContinuation,
    KOS_EVALUATED_PYTYPE(KosPyContinuationType));

  if (! cont)
    return NULL;

  KosPyContinuation_CAPTURED(cont) = captured;

  PyObject_GC_Track((PyObject *) cont);
  return (PyObject *) cont;
}


/* === Unit lifecycle === */


int koskari_cont_init(PyObject *mod) {

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  /* Initialize string constants (cleared in koskari_cont_clear). */
  STR_CONST(str_args, "args");
  STR_CONST(str_continuation, "continuation");
  STR_CONST(str_target, "target");

  if (PyType_Ready(KOS_EVALUATED_PYTYPE(KosPyDelimitedContinuationType)) ||
      PyType_Ready(KOS_EVALUATED_PYTYPE(KosPyContinuationType)))
    return -1;

  if (PyDict_SetItemString(dict, "DelimitedContinuation",
                           (PyObject *) KOS_EVALUATED_PYTYPE(KosPyDelimitedContinuationType)))
    return -1;

  if (PyDict_SetItemString(dict, "Continuation",
                           (PyObject *) KOS_EVALUATED_PYTYPE(KosPyContinuationType)))
    return -1;

  return 0;
}


int koskari_cont_clear(PyObject *mod) {

  (void) mod;

  Py_CLEAR(str_args);
  Py_CLEAR(str_continuation);
  Py_CLEAR(str_target);
  return 0;
}


// The end.
