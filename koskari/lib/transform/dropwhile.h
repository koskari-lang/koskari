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


#ifndef KOSKARI_DROPWHILE_H
#define KOSKARI_DROPWHILE_H


#include <Python.h>

#include "evaluated.h"


/**
   KosDropWhileType: a native lazy dropwhile over one source,
   duck-typed against itertools.dropwhile. A KosIter provider, so the
   predicate runs in the evaluator rather than across the Python
   boundary and the source may be driven.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/
extern KosEvaluatedTypeObject KosDropWhileType;


#define KosDropWhile_CheckExact(obj) \
  ((obj) && ((obj)->ob_type == KOS_EVALUATED_PYTYPE(KosDropWhileType)))


int koskari_dropwhile_init(PyObject *mod);
int koskari_dropwhile_clear(PyObject *mod);


#endif /* KOSKARI_DROPWHILE_H */


// The end.
