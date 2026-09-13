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


#ifndef KOSKARI_CHAIN_H
#define KOSKARI_CHAIN_H


#include <Python.h>

#include "evaluated.h"


/**
   KosChainType: a native lazy chain over a series of iterables,
   duck-typed against itertools.chain. A KosIter provider: one source
   is live at a time, each acquired only when the one before it runs
   out, so a source that is never reached is never touched.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/
extern KosEvaluatedTypeObject KosChainType;


#define KosChain_CheckExact(obj) \
  ((obj) && ((obj)->ob_type == KOS_EVALUATED_PYTYPE(KosChainType)))


int koskari_chain_init(PyObject *mod);
int koskari_chain_clear(PyObject *mod);


#endif /* KOSKARI_CHAIN_H */


// The end.
