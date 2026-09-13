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


#ifndef KOSKARI_TAKEWHILE_H
#define KOSKARI_TAKEWHILE_H


#include <Python.h>

#include "evaluated.h"


/**
   KosTakeWhileType: a native lazy takewhile over one source,
   duck-typed against itertools.takewhile. A KosIter provider, so the
   predicate runs in the evaluator and the source may be driven.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/
extern KosEvaluatedTypeObject KosTakeWhileType;


#define KosTakeWhile_CheckExact(obj) \
  ((obj) && ((obj)->ob_type == KOS_EVALUATED_PYTYPE(KosTakeWhileType)))


int koskari_takewhile_init(PyObject *mod);
int koskari_takewhile_clear(PyObject *mod);


#endif /* KOSKARI_TAKEWHILE_H */


// The end.
