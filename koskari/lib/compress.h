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


#ifndef KOSKARI_COMPRESS_H
#define KOSKARI_COMPRESS_H


#include <Python.h>

#include "evaluated.h"


/**
   KosCompressType: a native lazy compress over a data source and a
   selector source, duck-typed against itertools.compress. A KosIter
   provider, so either source may be driven.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/
extern KosEvaluatedTypeObject KosCompressType;


#define KosCompress_CheckExact(obj) \
  ((obj) && ((obj)->ob_type == KOS_EVALUATED_PYTYPE(KosCompressType)))


int koskari_compress_init(PyObject *mod);
int koskari_compress_clear(PyObject *mod);


#endif /* KOSKARI_COMPRESS_H */


// The end.
