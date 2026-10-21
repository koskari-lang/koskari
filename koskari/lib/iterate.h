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


#ifndef KOSKARI_ITERATE_H
#define KOSKARI_ITERATE_H


#include <Python.h>

#include "evaluated.h"


/**
   Python-surface adapters over the KosIter suite.

   A provider builds its own PyAsyncMethods and method tables from these
   fillers, so the work of a correct __anext__ awaitable and a correct
   CPython iterator (KosPyIter) is written once. Each adapter is a site
   like any opcode site: it dispatches ki_step through the suite and
   renders the results into the Python protocols instead of stack
   pushes and PC branches. The stepper owns a lazily created drive
   context; send and throw values are delivered through koskari_eval's
   entry ingress.
*/

PyObject *KosIterAdapt_Anext(PyObject *self);            /* am_anext */
PyObject *KosIterAdapt_Asend(PyObject *self, PyObject *v);
PyObject *KosIterAdapt_Athrow(PyObject *self, PyObject *args);
PyObject *KosIterAdapt_Aclose(PyObject *self, PyObject *ignored);
PyObject *KosIterAdapt_PyIter(PyObject *self);       /* tp_iter */


int koskari_iterate_init(PyObject *mod);
int koskari_iterate_clear(PyObject *mod);


#endif /* KOSKARI_ITERATE_H */


// The end.
