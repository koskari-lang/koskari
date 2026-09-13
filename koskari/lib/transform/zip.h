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


#ifndef KOSKARI_ZIP_H
#define KOSKARI_ZIP_H


#include <Python.h>

#include "evaluated.h"


/**
   KosZipType: a native lazy zip over N sources, duck-typed against
   builtins.zip. A KosIter provider, and the first multi-source
   provider on the transform base: it owns the acquired iterators and
   publishes them through the base's sources view. Where builtins.zip
   drives every source through tp_iternext and so cannot take an async
   iterator at all, this walks each row through the evaluator when any
   source is driven.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/
extern KosEvaluatedTypeObject KosZipType;


#define KosZip_CheckExact(obj) \
  ((obj) && ((obj)->ob_type == KOS_EVALUATED_PYTYPE(KosZipType)))


int koskari_zip_init(PyObject *mod);
int koskari_zip_clear(PyObject *mod);


#endif /* KOSKARI_ZIP_H */


// The end.
