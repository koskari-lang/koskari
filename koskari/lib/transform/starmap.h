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


#ifndef KOSKARI_STARMAP_H
#define KOSKARI_STARMAP_H


#include <Python.h>

#include "evaluated.h"


/**
   KosStarMapType: a native lazy starmap over one source, duck-typed
   against itertools.starmap. A KosIter provider, so the function runs
   in the evaluator with the element's own items as its argument
   vector, and the source may be driven.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/
extern KosEvaluatedTypeObject KosStarMapType;


#define KosStarMap_CheckExact(obj) \
  ((obj) && ((obj)->ob_type == KOS_EVALUATED_PYTYPE(KosStarMapType)))


int koskari_starmap_init(PyObject *mod);
int koskari_starmap_clear(PyObject *mod);


#endif /* KOSKARI_STARMAP_H */


// The end.
