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


#ifndef KOSKARI_COMPAT_H
#define KOSKARI_COMPAT_H


#include <Python.h>

#if PY_MINOR_VERSION == 14
#include "compat_314.h"

#elif PY_MINOR_VERSION == 13
#include "compat_313.h"

#elif PY_MINOR_VERSION == 12
#include "compat_312.h"

#elif PY_MINOR_VERSION == 11
#include "compat_311.h"

#elif PY_MINOR_VERSION == 10
#include "compat_310.h"

#elif PY_MINOR_VERSION == 9
#include "compat_39.h"

#endif


/* === GC tracked test === */

/* kos_gc_is_tracked(op): whether the collector holds op, read from the
   object's own GC record the way CPython's internal inline reads it.
   kos_gc_clear_finalized(op): forget that op's finalizer has run, for
   a record revived from a function's dead slot. Both defined in each
   version's compat header, which owns the layout. */


/* === Immortal objects === */

/*
   Make one object immortal: its refcount holds the sentinel the
   interpreter skips in Py_INCREF and Py_DECREF (PEP 683, 3.12 and
   later; the sentinel was renamed in 3.14). Before 3.12 there is no
   such refcount and the object stays ordinary, kept alive by its
   holder's reference. The object must own no references the collector
   needs to see: untrack it first.
*/
#if defined(Py_GIL_DISABLED)
/* no sentinel: the free-threaded build immortalizes differently */
#elif PY_MINOR_VERSION >= 14
#define KOS_IMMORTAL_REFCNT _Py_IMMORTAL_INITIAL_REFCNT
#elif PY_MINOR_VERSION >= 12
#define KOS_IMMORTAL_REFCNT _Py_IMMORTAL_REFCNT
#endif

static inline void kos_immortalize(PyObject *obj) {
#ifdef KOS_IMMORTAL_REFCNT
  Py_SET_REFCNT(obj, (Py_ssize_t) KOS_IMMORTAL_REFCNT);
#else
  (void) obj;
#endif
}


/* === The eval breaker === */

/*
   CPython's eval breaker is the word its own loop tests at every
   cooperation point (a backward jump, a call, a resume) to learn that
   another thread wants the GIL or a signal is pending; the evaluator
   tests the same word at the same kinds of points. The version headers
   above replicate the layout that reaches it, in one of two shapes:

   - KOS_BREAKER_WORD(tstate), 3.13 and later: a bit set on the thread
     state, with the bits named KOS_BREAKER_GIL_DROP,
     KOS_BREAKER_SIGNALS and KOS_BREAKER_GC_SCHEDULED. Which of them
     the evaluator services, and how, is the evaluator's decision
     (KOS_BREAKER_MASK and kos_eval_poll_now in eval.h / eval.c).
   - KOS_CEVAL_STATE(interp), before 3.13: the interpreter's ceval
     state, whose eval_breaker is one aggregate int, with
     gil_drop_request beside it.

   A version defining neither gets a word that always reads up, so
   every cooperation point polls, and a GIL drop answered as always
   requested: correct, and slow only until the layout is mapped.
*/
#if defined(KOS_BREAKER_WORD)

typedef uintptr_t KosBreakerWord;

/* The address of tstate's eval breaker */
static inline const volatile KosBreakerWord *koskari_eval_breaker_locate_tstate(
    PyThreadState *tstate) {

  return (const volatile KosBreakerWord *) &KOS_BREAKER_WORD(tstate);
}

/* The address of this thread's eval breaker; NULL without a thread state */
static inline const volatile KosBreakerWord *koskari_eval_breaker_locate(void) {
  PyThreadState *const tstate = PyThreadState_Get();
  return tstate ? koskari_eval_breaker_locate_tstate(tstate) : NULL;
}

/* Whether another thread asked this one to drop the GIL */
static inline int koskari_gil_drop_requested(void) {
# ifdef Py_GIL_DISABLED
  return 0;
# else
  const volatile KosBreakerWord *w = koskari_eval_breaker_locate();
  return w && (*w & KOS_BREAKER_GIL_DROP) != 0;
# endif
}

#elif defined(KOS_CEVAL_STATE)

typedef int KosBreakerWord;

/* The address of the eval breaker of tstate's interpreter; NULL when
   tstate has no interpreter */
static inline const volatile KosBreakerWord *koskari_eval_breaker_locate_tstate(
    PyThreadState *tstate) {

  if (! tstate->interp)
    return NULL;
  return (const volatile KosBreakerWord *)
      &KOS_CEVAL_STATE(tstate->interp)->eval_breaker;
}

/* The address of the interpreter's eval breaker; NULL without a thread */
static inline const volatile KosBreakerWord *koskari_eval_breaker_locate(void) {
  PyThreadState *const tstate = PyThreadState_Get();
  return tstate ? koskari_eval_breaker_locate_tstate(tstate) : NULL;
}

/* Whether another thread asked this one to drop the GIL */
static inline int koskari_gil_drop_requested(void) {
  PyThreadState *const tstate = PyThreadState_Get();
  if (! tstate || ! tstate->interp)
    return 0;
  return KOS_CEVAL_STATE(tstate->interp)->gil_drop_request != 0;
}

#else

typedef int KosBreakerWord;
# define KOS_BREAKER_UNMAPPED 1

#endif

/* A word that never reads clear: the unmapped default, and the stand-in
   when a version's locate finds no thread state. */
static const volatile KosBreakerWord koskari_breaker_always = 1;

#ifdef KOS_BREAKER_UNMAPPED
static inline const volatile KosBreakerWord *koskari_eval_breaker_locate_tstate(
    PyThreadState *tstate) {

  (void) tstate;
  return &koskari_breaker_always;
}

static inline const volatile KosBreakerWord *koskari_eval_breaker_locate(void) {
  return &koskari_breaker_always;
}

static inline int koskari_gil_drop_requested(void) {
  return 1;
}
#endif


/* === macros for all versions === */

#if PY_MINOR_VERSION < 13
#define PyMapping_HasKeyWithError(o, k)  PyMapping_HasKey((o), (k))
#endif


/* === StopIteration raise === */

/* A StopIteration instance carrying value, in the state the type's
   new and init leave one: the type's allocation (every exception slot
   NULL, suppress_context clear), args the one-tuple of value, and the
   value slot. The type call that would do the same costs a vectorcall
   into type_call, BaseException_new and StopIteration_init per raise,
   and the iterator protocol raises one on every return a Python
   driver takes from a Koskari coroutine. Steals value: the value slot
   takes the caller's reference, the args tuple its own. New reference,
   or NULL with an error set and value released.
   https://github.com/python/cpython/blob/v3.12.0/Objects/exceptions.c */
static inline PyObject *kos_stop_iteration_new(PyObject *value) {
  PyTypeObject *type = (PyTypeObject *) PyExc_StopIteration;
  PyStopIterationObject *exc =
      (PyStopIterationObject *) type->tp_alloc(type, 0);

  if (! exc) {
    Py_DECREF(value);
    return NULL;
  }
  exc->args = PyTuple_New(1);
  if (! exc->args) {
    Py_DECREF(exc);
    Py_DECREF(value);
    return NULL;
  }
  PyTuple_SET_ITEM(exc->args, 0, Py_NewRef(value));
  exc->value = value;
  return (PyObject *) exc;
}


/* Report a return value through the iterator protocol as CPython's
   generator send does on this version: None (or NULL) raises an empty
   StopIteration through PyErr_SetNone; any other value is carried by a
   StopIteration instance built here (a bare PyErr_SetObject would
   tuple-splat a tuple value) and raised by kos_raise_stop_instance.
   Steals value. Always returns NULL. */
static inline PyObject *kos_raise_stop_iteration(PyObject *value) {
  PyObject *exc;

  if (! value || value == Py_None) {
    Py_XDECREF(value);
    PyErr_SetNone(PyExc_StopIteration);
    return NULL;
  }

  exc = kos_stop_iteration_new(value);
  if (exc)
    kos_raise_stop_instance(exc);
  return NULL;
}


/* === Inline-values instance attributes: shared name -> index scan === */

#if PY_MINOR_VERSION >= 11
/*
   The instance-value field cache addresses an object's inline attribute
   values by their index in the type's shared keys. CPython keeps the keys
   accessors internal and unexported, so we replicate just the prefix of
   struct _dictkeysobject we read; the per-version value access lives in the
   matching compat_3xx.h behind kos_type_has_inline_values /
   kos_inline_value_slots. This layout holds for 3.11 (managed dict introduced)
   through 3.14; the free-threaded mutex only appears under Py_GIL_DISABLED.
   3.9 / 3.10 predate the managed dict (split dicts, opaque keys) and stay on
   the generic path. See eval/eval_cache.c (KOS_FIELD_INSTANCE_VALUE).
*/


/* The prefix of struct _dictkeysobject: a type's shared/cached keys. */
typedef struct {
  Py_ssize_t dk_refcnt;
  uint8_t dk_log2_size;
  uint8_t dk_log2_index_bytes;
  uint8_t dk_kind;
#ifdef Py_GIL_DISABLED
  PyMutex dk_mutex;
#endif
  uint32_t dk_version;
  Py_ssize_t dk_usable;
  Py_ssize_t dk_nentries;
  char dk_indices[];
} KosSharedKeys;


/* A unicode key entry (split/unicode tables store these). */
typedef struct {
  PyObject *me_key;
  PyObject *me_value;
} KosSharedKeyEntry;


/*
   The entries follow the hash table (dk_indices), whose size in bytes is
   (1 << dk_log2_index_bytes). Mirrors _DK_ENTRIES for the unicode case.
*/
static inline KosSharedKeyEntry *kos_shared_key_entries(KosSharedKeys *dk) {
  size_t ix = (size_t) 1 << dk->dk_log2_index_bytes;
  return (KosSharedKeyEntry *) (dk->dk_indices + ix);
}


/*
   Find the inline-values index of name among tp's shared keys. Returns 1 and
   sets *index on a match, 0 otherwise (no cached keys, general keys, or name
   absent). The entry index is the value index: inline values are ordered by
   the shared keys. Called only on the cold path, so the linear scan over
   exact str keys, which runs no user code, is amortized to nothing.
*/
static inline int kos_type_value_index(
    PyTypeObject *tp, PyObject *name, uint32_t *index) {

  KosSharedKeys *keys = (KosSharedKeys *) ((PyHeapTypeObject *) tp)->ht_cached_keys;
  if (! keys || keys->dk_kind == 0)          /* need shared unicode keys */
    return 0;

  KosSharedKeyEntry *ents = kos_shared_key_entries(keys);
  Py_ssize_t n = keys->dk_nentries;
  for (Py_ssize_t i = 0; i < n; i++) {
    PyObject *k = ents[i].me_key;
    if (! k)
      continue;
    if (k == name
        || (PyUnicode_CheckExact(k) && PyUnicode_CheckExact(name)
            && PyUnicode_Compare(k, name) == 0)) {
      if (i > (Py_ssize_t) UINT32_MAX)
        return 0;
      *index = (uint32_t) i;
      return 1;
    }
  }
  return 0;
}
#endif

/**
   Return the value associated with a StopAsyncIteration instance (new ref).
   On 3.13+ uses .value; on earlier versions uses exc.args[0].  Returns
   Py_None (new ref) when no value is present.  Caller must Py_DECREF the result.
*/
PyObject *koskari_StopAsyncIteration_value(PyObject *exc);
PyObject *koskari_StopAsyncIteration_FromValue(PyObject *payload);

#define StopAsyncIteration_VALUE(exc)  koskari_StopAsyncIteration_value(exc)

/* _PyType_Lookup remains private on all supported versions */
#define PyType_Lookup _PyType_Lookup


/* === compat unit lifecycle === */

int koskari_compat_init(const PyObject *mod);
int koskari_compat_clear(PyObject *mod);


/* 1 when the exact native coroutine is suspended under another awaiter,
   0 when not, -1 with an error set. */
int kos_coro_awaited(PyObject *coro);

/* The iterator that awaiting o steps: an exact native coroutine itself,
   refused when already awaited; else am_await when the type has the
   slot; else a types.coroutine marked generator itself; else __await__
   looked up and called. New reference, or NULL with an error set. */
PyObject *kos_get_awaitable(PyObject *o);

/* The first step of awaiting o: the await iterator as kos_get_awaitable
   finds it, sent None once. Returns the completion (new reference) with
   *iter NULL; or NULL with *iter and *yielded set (new references) when
   the step suspended; or NULL with both NULL and an exception set. */
PyObject *kos_await_first_step(PyObject *o, PyObject **iter, PyObject **yielded);

/* Throw exc, an exception instance, into a delegated iterator by the
   yield-from rule: a GeneratorExit closes the iterator first, an error
   from that close propagating, and is then raised here; any other
   exception goes to the iterator's throw when it has one, in the
   one-argument form, and is raised here when it has none. The outcome
   is the send protocol's: PYGEN_NEXT with *result the yield,
   PYGEN_RETURN with *result the completion (None when it carried
   none), PYGEN_ERROR with *result NULL and the exception set. */
PySendResult kos_iter_throw(PyObject *iter, PyObject *exc, PyObject **result);

/* Close a delegated iterator: its close when it has one, an error from
   it propagating. Returns 0, or -1 with the exception set. The fenced
   teardown form that also covers providers and async iterators is
   kos_iter_close in eval.h. */
int kos_iter_close_or_raise(PyObject *iter);


/**
   Extend list with items from iterable.  Returns 0 on success, -1 on error.
   Replaces _PyList_Extend (removed in 3.13+).
*/
int koskari_List_Extend(PyObject *list, PyObject *iterable);


/**
   Return dict.items() view (new ref).  Replaces _PyDictView_New(,,PyDictItems_Type).
*/
PyObject *koskari_DictView_Items(PyObject *dict);

/**
   Return dict.keys() view (new ref).  Replaces _PyDictView_New(,,PyDictKeys_Type).
*/
PyObject *koskari_DictView_Keys(PyObject *dict);


/**
   Fetch value from current StopIteration/StopAsyncIteration exception.
   Returns 0 on success, -1 on error.  Clears the exception.  On success
   sets *ptr to the value (new ref) or Py_None.  Replaces
   PyGen_FetchStopIterationValue when the symbol is not exported (e.g. 3.14).
*/
int koskari_FetchStopIterationValue(PyObject **ptr);


/**
   Fetch and clear the current exception, return the exception instance
   (new ref).  Replaces PyErr_GetRaisedException (added in 3.12).
*/
PyObject *koskari_Err_GetRaisedException(void);


/**
   Raise the AttributeError getattr raises for a name obj lacks: CPython's
   message, with name and obj recorded on the exception from 3.10 on.
*/
void koskari_Err_SetMissingAttribute(PyObject *obj, PyObject *name);


/**
   True when two str objects are equal, 0 otherwise: identity first, then
   PyUnicode_Compare. Both operands must be str; there is no type check.
   Replaces PyUnicode_Equal (public from 3.14).
*/
int koskari_Unicode_Equal(PyObject *a, PyObject *b);


/**
   Replaces _PyErr_FormatFromCause, moved to Python's internal-only header
   in 3.13+ and no longer visible to extensions.  Fetches the current
   exception, formats a new one, chains the old as __cause__/__context__,
   and restores the new one as current.  Used via macro in
   compat_313/314.h.
*/
PyObject *koskari_Err_FormatFromCause(PyObject *exception,
                                       const char *format, ...);


#endif


// The end.
