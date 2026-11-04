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


#ifndef KOSKARI_ITER_H
#define KOSKARI_ITER_H


#include <Python.h>

#include "evaluated.h"


/* === KosPyAsyncGen === */


/**
   KosPyAsyncGen wraps a KosContext for Python's async generator protocol.

   Similar to KosCoroutine but yields values via __anext__. A KosIter
   provider: its kos_iter suite steps by splicing the generator's frames
   onto the consumer context (KOS_ITER_INSTALLED), with delivery to the
   consumer frame's resume.
*/

extern KosEvaluatedTypeObject KosPyAsyncGenType;


struct KosPyAsyncGen {
  PyObject_HEAD
  KosContext *ctx;
  /* the generator's state between installs: the resting span and
     what the next install needs; a GEN node marks the span on a chain
     while it is installed and refers back here */
  KosGenState state;
  PyObject *code_name;  /* strong ref; KosCode name for repr (ctx may not expose frame) */
  /* the function that made the call, owned for the generator's whole
     life so the dealloc can store the finished generator on it
     (KosFunction_DEAD_RECORD); NULL otherwise */
  PyObject *fn;
  int closed;        /* generator closed */
};


#define KosPyAsyncGen_Check(obj)                                        \
  ((obj) && PyType_IsSubtype((obj)->ob_type,                            \
                             KOS_EVALUATED_PYTYPE(KosPyAsyncGenType)))

#define KosPyAsyncGen_CheckExact(obj)                                   \
  ((obj) && ((obj)->ob_type == KOS_EVALUATED_PYTYPE(KosPyAsyncGenType)))


/**
   A wrapper over an unstarted generator's frame, complete before it is
   GC-tracked: the state is what the traverse reads, so it is filled
   here rather than by the caller afterwards. Steals ctx (may be NULL)
   and frame (freed on failure). fn (borrowed, may be NULL) is the
   function that made the call: its stored dead generator is the record
   revived when one is waiting, else a fresh allocation.
*/
PyObject *KosPyAsyncGen_New(KosContext *ctx, KosDynFrame *frame, PyObject *fn);


/**
   Close and free the resting span and the context owned by an async
   generator without freeing the Python wrapper. Safe when other
   references may remain alive
*/

void KosPyAsyncGen_Teardown(KosPyAsyncGen *agen);


/**
   Move the generator's step, suspended mid-await on a consumer's
   chain, onto the generator's own context, where the generator owns
   it: the span from the chain's top down to gen is frozen off from
   (kos_dyn_gen_freeze), which gets the consumer's registers back, and
   installed on the wrapper's context, created when there is none,
   with the await iterator, the resume inject, the handler-visible
   exception state and the AWAIT suspension mark moved with it. The
   step stays in flight (busy), so the generator refuses another drive
   as already running and closes from its own finalizer. The caller
   is the consumer's finalizer, which then closes what is left of its
   own chain. A generator whose finalizer has already run is closed
   here instead.

   :param agen: the GEN's owner, a reference held by the caller
   :param from: the consumer's context, not running
   :param gen: the innermost GEN on from's chain (kos_dyn_find_gen)
   :return: 0, or -1 with the error set and the span back on from's
     chain, from's drive state unmoved
*/
int KosPyAsyncGen_Park(
    KosPyAsyncGen *agen,
    KosContext *from,
    KosDynGen *gen);


/**
   Create a KosPyAsyncGen from code and namespace. Requires KosCode_YIELDS(code).
   Caller owns the returned reference.

   :param code: KosCode with yield opcodes
   :param ns: frame namespace (new ref consumed)
   :return: new KosPyAsyncGen or NULL on error
*/
PyObject *KosAsyncGen_NewFromCodeAndNs(KosCode *code, PyObject *ns);

/**
   An AsyncGenerator over a ready root frame (stolen; freed on
   failure): the frame is the resting span, its fn tag kept so the
   frame returns to the function's cache when the generator is
   exhausted. The one constructor a function call uses,
   from either boundary; fn is that function (borrowed, NULL for an
   Expression) and holds the record between calls.
*/
PyObject *KosAsyncGen_NewFromFrame(PyObject *fn, KosDynFrame *frame);


/* === Unit lifecycle === */


int koskari_iter_init(PyObject *mod);
int koskari_iter_clear(PyObject *mod);


#endif


// The end.
