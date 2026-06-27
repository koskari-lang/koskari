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


#ifndef KOSKARI_PARTIAL_H
#define KOSKARI_PARTIAL_H


#include <Python.h>

#include "callable.h"


/**
   KosPartialCallableType: a standalone native KosCallable that reproduces the
   behavior of functools.partial (func / args / keywords binding, calling,
   nested flattening, pickling, instance __dict__).

   It does NOT inherit functools.partial, so isinstance(x, functools.partial)
   is False -- the match is by duck typing only. In exchange it is a static
   KosCallableTypeObject, so koskari bytecode calls dispatch straight through
   the install slot (merge bound + call args, tail-call the inner callable)
   with no intermediate KosContext.
*/
extern KosCallableTypeObject KosPartialCallableType;


#define KosPartial_CheckExact(obj) \
  ((obj) && ((obj)->ob_type == KOS_CALLABLE_PYTYPE(KosPartialCallableType)))


int koskari_partial_init(PyObject *mod);
int koskari_partial_clear(PyObject *mod);


#endif /* KOSKARI_PARTIAL_H */


// The end.
