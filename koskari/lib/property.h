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


#ifndef KOSKARI_PROPERTY_H
#define KOSKARI_PROPERTY_H


#include <Python.h>


/**
   KosPropertyType: subtype of the built-in `property` that auto-wraps
   KosCallable accessors in Demand at registration time.
*/
extern PyTypeObject KosPropertyType;


int koskari_property_init(PyObject *mod);
int koskari_property_clear(PyObject *mod);


#endif /* KOSKARI_PROPERTY_H */


// The end.
