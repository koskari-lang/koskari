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
  koskari/lib/eval/eval_iter.c

  Python iteration from the evaluator: iterator acquire and close over
  both protocols, result classification, the OBJECT_NEXT / ITER_NEXT
  next paths, and the globals namespace lookup. Included into eval.c as
  a segment (see the eval/ includes there).

  :author: Christopher O'Brien <obriencj@preoccupied.net>
  :license: GNU General Public License v3
*/


/* === Python iter next (OBJECT_NEXT / ITER_NEXT opcodes) === */


/**
   Acquire an iterator from an iterable, uniform over both protocols
   (the PUSH_FOREACH preference order, shared with OBJECT_ITER so the
   two cannot drift): a KosIter provider passes through as itself (a
   KosPyIter included: its suite forwards to its provider); an object
   with am_aiter yields its async iterator; otherwise the sync protocol
   (PyObject_GetIter, including the sequence fallback).

   :param iterable: borrowed
   :return: new iterator reference, or NULL with PyErr set
*/
PyObject *kos_iter_acquire(PyObject *iterable) {

  if (KosIter_Check(iterable)) {
    Py_INCREF(iterable);
    return iterable;
  }

  if (Py_TYPE(iterable)->tp_as_async
      && Py_TYPE(iterable)->tp_as_async->am_aiter)
    return Py_TYPE(iterable)->tp_as_async->am_aiter(iterable);

  return PyObject_GetIter(iterable);
}


/**
   Close an acquired iterator, best-effort and exception-fenced: a
   provider through its suite's ki_close, a sync iterator through
   close(), an async iterator through aclose() with its awaitable
   demand-driven -- an immediate completion honors the cleanup, a
   genuine suspension takes the cannot-honor fallback of closing the
   in-flight iterator. Any pending exception is preserved.
*/
void kos_iter_close(PyObject *iter) {

  PyObject *ftyp, *fval, *ftb;

  if (! iter)
    return;

  PyErr_Fetch(&ftyp, &fval, &ftb);

  {
    KosIterSlots *islots;

    if (KosIter_CheckSlots(iter, &islots)) {
      (void) islots->ki_close(iter);
      PyErr_Clear();

    } else if (Py_TYPE(iter)->tp_iternext) {
      (void) kos_iter_close_or_raise(iter);
      PyErr_Clear();

    } else {
      PyObject *aw = PyObject_CallMethod(iter, "aclose", NULL);
      if (aw) {
        PyObject *it = kos_get_awaitable(aw);
        Py_DECREF(aw);
        if (it) {
          PyObject *step = NULL;
          if (kos_iter_send(it, Py_None, &step) == PYGEN_NEXT) {
            /* Suspend attempt during close; cannot honor further --
               close the iterator so its GeneratorExit cleanup runs. */
            Py_DECREF(step);
            (void) kos_iter_close_or_raise(it);
          }
          Py_XDECREF(step);
          Py_DECREF(it);
        }
      }
      PyErr_Clear();
    }
  }

  PyErr_Restore(ftyp, fval, ftb);
}


/**
   Classify a produced result under ambient async: a non-awaitable, a
   Defer, or a future/task handle is a completed value; anything else
   awaitable must be awaited before use.
*/
int KosEval_ResultIsValue(PyObject *result) {

  PyTypeObject *typ = Py_TYPE(result);

  if (! typ->tp_as_async || ! typ->tp_as_async->am_await)
    return 1;
  if (KosDefer_Check(result))
    return 1;
  return KosEval_IsFuture(result);
}

/**
   Get next value from a Python async iterator using the given __anext__
   awaitable. Caller must have obtained awaitable via get_await_next(iter);
   this helper consumes it (DECREF). *result_out is set with a new
   reference on GET_NEXT_VALUE.
*/
static KosGetNextResult next_from_python_async_iter(
    KosEvalState *es,
    PyObject **await_out,
    PyObject *iter,
    PyObject **result_out,
    PyObject *awaitable) {

  (void) iter;
  int suspended = 0;
  PyObject *yielded = NULL;
  PyObject *result = KosEvalState_EagerStepAwaitable(es, awaitable, &suspended,
                                                     &yielded);
  Py_DECREF(awaitable);

  if (suspended && yielded) {
    KosEvalState_MarkSuspendedNotRunning(es);
    *await_out = yielded;
    return GET_NEXT_SUSPEND;
  }

  if (PyErr_ExceptionMatches(PyExc_StopAsyncIteration)) {
    PyErr_Clear();
    return GET_NEXT_EXHAUSTED;
  }

  if (result) {
    *result_out = result;
    return GET_NEXT_VALUE;
  }

  return GET_NEXT_ERROR;
}


/**
   Get next value from a Python sync iterator (PyIter_Next).
   *result_out is set with a new reference on GET_NEXT_VALUE.
*/
static KosGetNextResult next_from_python_sync_iter(
    KosEvalState *es,
    PyObject **await_out,
    PyObject *iter,
    PyObject **result_out) {

  (void) es;
  (void) await_out;

  PyObject *n = PyIter_Next(iter);
  if (n) {
    *result_out = n;
    return GET_NEXT_VALUE;
  }

  if (PyErr_Occurred())
    return GET_NEXT_ERROR;

  return GET_NEXT_EXHAUSTED;
}


/**
   Dispatcher: get next from iter (Koskari async gen, Python async iter,
   or Python sync iter). Does not push; does not perform GetIter.
   *result_out set only on GET_NEXT_VALUE (caller pushes and DECREF).
*/
static inline KosGetNextResult op_get_next_from_iter(
    KosEvalState *es,
    PyObject **await_out,
    PyObject *iter,
    PyObject **result_out) {

  /* Koskari async gen is handled in NEXT/ITER_NEXT via in-loop attach; never here. */

  PyObject *awaitable = get_await_next(iter);
  if (awaitable) {
    KosGetNextResult r = next_from_python_async_iter(es, await_out, iter,
                                                      result_out, awaitable);
    return r;
  }

  /* NULL from get_await_next means the object has no am_anext, with
     no exception set, or that __anext__ or GetAwaitable failed. A
     synchronous StopAsyncIteration is this iterator's exhaustion,
     exactly as it is once the awaitable is stepped. */
  if (PyErr_Occurred()) {
    if (PyErr_ExceptionMatches(PyExc_StopAsyncIteration)) {
      PyErr_Clear();
      return GET_NEXT_EXHAUSTED;
    }
    return GET_NEXT_ERROR;
  }

  if (PyIter_Check(iter))
    return next_from_python_sync_iter(es, await_out, iter, result_out);

  PyErr_SetString(PyExc_TypeError, "not an iterator");
  return GET_NEXT_ERROR;
}


/**
   The step for an iteration node's bound Python iterator, keyed by the
   kind its bind recorded, so no step probes the type again. The sync
   arm calls tp_iternext through the type, as CPython's FOR_ITER does,
   and consults the error state only on a NULL result; a StopIteration
   there is exhaustion. The async arm is the eager __anext__ step.
   Providers never reach here; their suite steps them.
*/
static inline KosGetNextResult op_get_next_from_bound(
    KosEvalState *es,
    uint8_t kind,
    PyObject **await_out,
    PyObject *iter,
    PyObject **result_out) {

  if (likely(kind == KOS_IREQ_KIND_PY_SYNC)) {
    iternextfunc next = Py_TYPE(iter)->tp_iternext;
    PyObject *n = likely(next) ? next(iter) : NULL;
    if (likely(n)) {
      *result_out = n;
      return GET_NEXT_VALUE;
    }
    if (PyErr_Occurred()) {
      if (! PyErr_ExceptionMatches(PyExc_StopIteration))
        return GET_NEXT_ERROR;
      PyErr_Clear();
    } else if (unlikely(! next)) {
      PyErr_SetString(PyExc_TypeError, "not an iterator");
      return GET_NEXT_ERROR;
    }
    return GET_NEXT_EXHAUSTED;
  }

  if (kind == KOS_IREQ_KIND_PY_ASYNC) {
    PyObject *awaitable = get_await_next(iter);
    if (awaitable)
      return next_from_python_async_iter(es, await_out, iter, result_out,
                                         awaitable);
    /* No error: the type lost its __anext__ since the bind classified
       it; take the sync step, which raises for an iterator with neither.
       Otherwise __anext__ or GetAwaitable failed, or raised
       StopAsyncIteration synchronously, which is this iterator's
       exhaustion. */
    if (! PyErr_Occurred())
      return op_get_next_from_bound(es, KOS_IREQ_KIND_PY_SYNC, await_out,
                                    iter, result_out);
    if (PyErr_ExceptionMatches(PyExc_StopAsyncIteration)) {
      PyErr_Clear();
      return GET_NEXT_EXHAUSTED;
    }
    return GET_NEXT_ERROR;
  }

  PyErr_SetString(PyExc_TypeError, "not an iterator");
  return GET_NEXT_ERROR;
}


/**
  Globals
*/
static inline PyObject *KosEvalState_GlobalsNamespace(KosEvalState *es) {

  KosDynFrame *frame = KosEvalState_FRAME(es);
  PyObject *ns;

  if (! frame) {
    PyErr_SetString(PyExc_RuntimeError, "no frame for globals lookup");
    return NULL;
  }

  ns = KosDynFrame_NS(frame);
  if (! ns || ! KosNamespace_Check(ns)) {
    PyErr_SetString(PyExc_RuntimeError,
                    "frame has no namespace for globals lookup");
    return NULL;
  }

  while (KosNamespace_PARENT(ns))
    ns = KosNamespace_PARENT(ns);

  return ns;
}


// The end.
