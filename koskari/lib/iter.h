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
   onto the consumer context (KOS_ITER_INSTALLED), with delivery through
   the site's iteration node.
*/

extern KosEvaluatedTypeObject KosPyAsyncGenType;


struct KosPyAsyncGen {
  PyObject_HEAD
  KosContext *ctx;
  void *gen;         /* KosDynGen * when set; agen holds ref when GEN released from chain */
  PyObject *code_name;  /* strong ref; KosCode name for repr (ctx may not expose frame) */
  int in_progress;   /* anext in progress */
  int started;       /* first step has driven the evaluator */
  int closed;        /* generator closed */
};


#define KosPyAsyncGen_Check(obj)                                        \
  ((obj) && PyType_IsSubtype((obj)->ob_type,                            \
                             KOS_EVALUATED_PYTYPE(KosPyAsyncGenType)))

#define KosPyAsyncGen_CheckExact(obj)                                   \
  ((obj) && ((obj)->ob_type == KOS_EVALUATED_PYTYPE(KosPyAsyncGenType)))


PyObject *KosPyAsyncGen_New(KosContext *ctx);


/**
   Release KosDynGen and KosContext owned by an async generator without
   freeing the Python wrapper.  Safe when other references may remain alive
*/

void KosPyAsyncGen_Teardown(KosPyAsyncGen *agen);


/**
   Create a KosPyAsyncGen from code and namespace. Requires KosCode_YIELDS(code).
   Caller owns the returned reference.

   :param code: KosCode with yield opcodes
   :param ns: frame namespace (new ref consumed)
   :return: new KosPyAsyncGen or NULL on error
*/
PyObject *KosAsyncGen_NewFromCodeAndNs(KosCode *code, PyObject *ns);


/* === Unit lifecycle === */


int koskari_iter_init(PyObject *mod);
int koskari_iter_clear(PyObject *mod);


#endif


// The end.
