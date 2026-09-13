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


#ifndef KOSKARI_ZIP_LONGEST_H
#define KOSKARI_ZIP_LONGEST_H


#include <Python.h>

#include "evaluated.h"


/**
   KosZipLongestType: a native lazy zip-longest over N sources,
   duck-typed against itertools.zip_longest. A KosIter provider, so a
   source may be driven; a source that runs out is replaced by the
   fill value and the rows continue until every source is spent.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/
extern KosEvaluatedTypeObject KosZipLongestType;


#define KosZipLongest_CheckExact(obj) \
  ((obj) && ((obj)->ob_type == KOS_EVALUATED_PYTYPE(KosZipLongestType)))


int koskari_zip_longest_init(PyObject *mod);
int koskari_zip_longest_clear(PyObject *mod);


#endif /* KOSKARI_ZIP_LONGEST_H */


// The end.
