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


#ifndef KOSKARI_CALLABLE_H
#define KOSKARI_CALLABLE_H


#include <Python.h>

#include "eval.h"
#include "evaluated.h"


struct KosContext;


/**
   Check-and-resolve in one: when obj is a KosCallable (a KosEvaluated
   whose suite carries the install slot), store the resolved suite into
   *out_slots and return 1; otherwise return 0 with *out_slots
   untouched. The single resolve serves the whole call sequence --
   frame prep and dispatch take the suite from here rather than
   re-resolving.
*/
static inline int KosCallable_CheckSlots(
    PyObject *obj,
    KosCallSlots **out_slots) {

  if (! obj || ! KosEvaluated_Check(obj))
    return 0;

  KosEvaluatedTypeObject *et = KosEvaluated_Resolve(Py_TYPE(obj));
  if (! et)
    return 0;

  KosCallSlots *slots = &et->kos_call;
  if (! slots->kc_vinstall)
    return 0;

  *out_slots = slots;
  return 1;
}


static inline int kos_callable_check(PyObject *obj) {

  KosCallSlots *slots;
  return KosCallable_CheckSlots(obj, &slots);
}

#define KosCallable_Check(obj) kos_callable_check(obj)


/* === KosCallShape: a callable held for repeated invocation === */

/**
   How a held callable is invoked, resolved once:

   FRAME -- a KosCallable; invoke through the evaluator's call entry,
   whose install reports whether it completed in hand or installed a
   frame. A KosCallable always runs in the running evaluator; routing
   one across the Python boundary would force a fresh context and a
   nested loop for a call the current one can make.
   PLAIN -- an ordinary python callable, needing no evaluator at all;
   invoke directly.
*/
typedef enum KosCallShapeKind {
  KOS_CALL_SHAPE_FRAME = 0,
  KOS_CALL_SHAPE_PLAIN,
} KosCallShapeKind;


/**
   A callable an object holds and invokes repeatedly, with its calling
   shape resolved at construction rather than per call. cs_callable is
   owned -- the holder's traverse visits it and its clear releases it
   through KosCallShape_Clear.
*/
typedef struct KosCallShape {
  PyObject *cs_callable;
  uint8_t cs_kind;
  uint8_t cs_direct;   /* FRAME only: the install fills a frame whose
                          return is the call's value, guaranteed --
                          true for an exact non-yielding KosFunction.
                          Other FRAME shapes (values, bound methods)
                          may complete inline instead, so a caller
                          relying on frame-return delivery must host
                          them under a frame of its own. */
} KosCallShape;


/**
   Resolve callable's shape into *shape, taking a reference.
*/
void KosCallShape_Init(KosCallShape *shape, PyObject *callable);


static inline void KosCallShape_Clear(KosCallShape *shape) {

  Py_CLEAR(shape->cs_callable);
}


/* === KosArgSpread: tuple + dict as a vectorcall vector === */


/**
   A tuple of positionals and a dict of keywords spread into a
   vectorcall-shaped vector: argv holds the positionals then the
   keyword values, kwnames names the keywords (NULL when there are
   none). The spread holds what the callee may otherwise lose under
   it: a reference to each keyword value taken from a dict (the last
   nheld entries of argv) and to a bound tuple, since code the callee
   runs can reach a live keywords dict or a partial's __setstate__
   and drop them before the callee consumes its arguments. Entries
   taken from a call vector stay borrowed: the caller's frame holds
   them. A keyword vector goes in the caller's buffer when it fits,
   else on the heap.
*/
typedef struct {
  PyObject *const *argv;
  Py_ssize_t nargs;
  PyObject *kwnames;
  PyObject **heap;
  Py_ssize_t nheld;
  PyObject *held_tuple;
} KosArgSpread;


/**
   Fill spread from args_tuple (may be NULL) and kwds_dict (may be NULL
   or empty). Without keywords argv borrows the tuple's item array and
   nothing is used or allocated. With keywords the vector goes in buf
   when nargs plus the keyword count fits bufcap, else on the heap; buf
   may be NULL with bufcap 0, and the keyword values are held. Returns
   0, or -1 with PyErr set and nothing left to clear.
*/
int KosArgSpread_Init(
    KosArgSpread *spread,
    PyObject *args_tuple,
    PyObject *kwds_dict,
    PyObject **buf,
    Py_ssize_t bufcap);


/**
   Fill spread with a bound tuple of positionals and a bound dict of
   keywords (either may be NULL or empty) merged with a call vector
   (args, nargs, kwnames): bound positionals first, then the call's,
   then the keyword values. A call keyword that names a bound keyword
   replaces its value in place. With nothing bound argv borrows args
   itself and nothing is allocated. Otherwise the vector goes in buf
   when it fits bufcap, else on the heap; buf may be NULL with bufcap
   0. The bound tuple and the keyword values taken from the bound dict
   are held; entries from the call vector are borrowed from it. Names
   are matched by their text, so no Python code runs
   inside the merge. A bound key that is not a str raises TypeError,
   and so does a call name when there are bound keywords to match it
   against. Returns 0, or -1 with PyErr set and nothing left to clear.
*/
int KosArgSpread_InitMerged(
    KosArgSpread *spread,
    PyObject *bound_args,
    PyObject *bound_kwds,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    PyObject **buf,
    Py_ssize_t bufcap);


/** Release what KosArgSpread_Init or KosArgSpread_InitMerged took. */
void KosArgSpread_Clear(KosArgSpread *spread);


/**
   Copy src's items into dst under exact str keys: a str subclass key
   is stored as a plain str, and any other key raises TypeError. The
   bound keywords of partial and values go through it, so the merge
   can match names by text. src may be NULL. Returns 0, or -1 with
   PyErr set.
*/
int KosKeywords_Update(PyObject *dst, PyObject *src);


/* === Python-slot adapters === */

/**
   Invoke a callable from Python through the kos call interface: resolve
   the call suite and dispatch its kc_pycall worker; PyObject_Call is the
   fallback for anything without one. The result is whatever the type's
   surface dictates -- a Coroutine or AsyncGenerator for async-surface
   types, a completed value for demand-surface types.

   callable, args, kwds are borrowed. args may be NULL.
*/
PyObject *Koskari_AsyncCall(
    PyObject *callable, PyObject *args, PyObject *kwds);

/**
   tp_call filler for an async-surface callable: Koskari_AsyncCall.
*/
PyObject *KosCallAdapt_CallAsync(
    PyObject *self, PyObject *args, PyObject *kwds);

/**
   vectorcallfunc filler with the same semantics: dispatch the suite's
   kc_pyvectorcall when present, else build tuple+dict and defer to
   Koskari_AsyncCall.
*/
PyObject *KosCallAdapt_VectorAsync(
    PyObject *self, PyObject *const *args, size_t nargsf,
    PyObject *kwnames);

int koskari_callable_init(PyObject *mod);
int koskari_callable_clear(PyObject *mod);


#endif


// The end.
