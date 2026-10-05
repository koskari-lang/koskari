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
   koskari/lib/exception.h

   Exception state handling shared by the evaluator, the dynamic chain,
   and the coroutine and iterator wrappers.

   KosExcState is an owned (type, value, traceback) triple. It is the
   one shape a handled exception travels in when it moves between the
   thread state and somewhere Koskari keeps it: a TRY or FINALLY region
   node saves the state it displaces and restores it on leaving, the
   way CPython pushes and pops the handled exception around a handler,
   and a context saves its handler-visible state across suspensions so
   contexts interleaved on one thread do not see each other's.

   The KosExc_NormalizeThrow pair turns the arguments of a
   generator-style throw into an owned (class, instance) pair for the
   evaluator's throw ingress.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#ifndef KOSKARI_EXCEPTION_H
#define KOSKARI_EXCEPTION_H


#include <Python.h>


/* === exception state === */


typedef struct KosExcState {
  PyObject *type;   /* owned or NULL */
  PyObject *value;  /* owned or NULL */
  PyObject *tb;     /* owned or NULL */
} KosExcState;


/* Nothing held */
static inline void KosExc_Init(KosExcState *s) {
  s->type = NULL;
  s->value = NULL;
  s->tb = NULL;
}


/* True when the state carries an exception */
static inline int KosExc_Held(const KosExcState *s) {
  return s->value != NULL || s->type != NULL;
}


/* Release whatever is held; the state is empty afterwards */
static inline void KosExc_Clear(KosExcState *s) {
  Py_CLEAR(s->type);
  Py_CLEAR(s->value);
  Py_CLEAR(s->tb);
}


/* Take the thread's handler-visible exception (sys.exc_info) into s,
   releasing anything s held. The thread state keeps its own view; use
   KosExc_Install to change it. */
static inline void KosExc_Fetch(KosExcState *s) {
  KosExc_Clear(s);
  PyErr_GetExcInfo(&s->type, &s->value, &s->tb);
}


/* Make s the thread's handler-visible exception, transferring the
   references; s is empty afterwards. An empty s clears the thread's. */
static inline void KosExc_Install(KosExcState *s) {
  PyErr_SetExcInfo(s->type, s->value, s->tb);
  KosExc_Init(s);
}


/* Take the pending (raised) exception into s, normalized, clearing the
   error indicator. s must be empty. */
static inline void KosExc_TakeRaised(KosExcState *s) {
  PyErr_Fetch(&s->type, &s->value, &s->tb);
  PyErr_NormalizeException(&s->type, &s->value, &s->tb);
}


/* Raise s again as the pending exception, transferring the references;
   s is empty afterwards. */
static inline void KosExc_Reraise(KosExcState *s) {
  PyErr_Restore(s->type, s->value, s->tb);
  KosExc_Init(s);
}


/* Move src into dst, which must be empty; src is empty afterwards */
static inline void KosExc_Move(KosExcState *dst, KosExcState *src) {
  *dst = *src;
  KosExc_Init(src);
}


/* Copy src into dst with new references; dst must be empty */
static inline void KosExc_Copy(KosExcState *dst, const KosExcState *src) {
  Py_XINCREF(src->type);
  Py_XINCREF(src->value);
  Py_XINCREF(src->tb);
  dst->type = src->type;
  dst->value = src->value;
  dst->tb = src->tb;
}


/* GC visit of the held references */
static inline int KosExc_Traverse(
    const KosExcState *s,
    visitproc visit,
    void *arg) {

  Py_VISIT(s->type);
  Py_VISIT(s->value);
  Py_VISIT(s->tb);
  return 0;
}


/* === throw arguments === */


/**
   Normalize the (typ[, val[, tb]]) triple of a generator-style throw
   into an owned (exception class, exception instance) pair. typ may be
   an exception class with an optional val, or a bare exception instance
   with no val. tb is accepted for signature compatibility and dropped;
   the evaluator's throw ingress carries no traceback.

   :param typ: exception class or instance
   :param val: value or instance for a class typ, or NULL/None
   :param tb: traceback or NULL/None (ignored)
   :param typ_out: receives a new reference to the exception class
   :param exc_out: receives a new reference to the exception instance
   :return: 0 on success, -1 with TypeError set on a malformed triple
*/
int KosExc_NormalizeThrow(
    PyObject *typ,
    PyObject *val,
    PyObject *tb,
    PyObject **typ_out,
    PyObject **exc_out);


/**
   Parse a throw method's args tuple with format (e.g. "O|OO:throw") and
   normalize it through KosExc_NormalizeThrow.

   :return: 0 on success, -1 with an exception set
*/
int KosExc_NormalizeThrowArgs(
    PyObject *args,
    const char *format,
    PyObject **typ_out,
    PyObject **exc_out);


#endif /* KOSKARI_EXCEPTION_H */


// The end.
