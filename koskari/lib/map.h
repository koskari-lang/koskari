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


#ifndef KOSKARI_MAP_H
#define KOSKARI_MAP_H


#include <Python.h>

#include "evaluated.h"


/**
   KosMapType: a native lazy map over one source, duck-typed against
   builtins.map. A KosIter provider: its steps run as C when both the
   fetch and the invoke complete synchronously, and park on the site's
   await suspension (the re-entry contract) when either goes
   asynchronous. Carries both the async and the demand Python surfaces
   through the KosIterAdapt fillers.
*/
extern KosEvaluatedTypeObject KosMapType;


#define KosMap_CheckExact(obj) \
  ((obj) && ((obj)->ob_type == KOS_EVALUATED_PYTYPE(KosMapType)))


int koskari_map_init(PyObject *mod);
int koskari_map_clear(PyObject *mod);


#endif /* KOSKARI_MAP_H */


// The end.
