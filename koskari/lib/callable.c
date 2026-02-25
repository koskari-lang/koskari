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


#include "lib.h"


KosCallableTypeObject KosCallableType = {
  .type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "koskari.lib._KosCallable",
    .tp_basicsize = sizeof(PyObject),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = "Internal base type for native Koskari callables",
    .tp_new = NULL,
  },
  .tp_kos_call = NULL,
  .tp_kos_tail_call = NULL,
  .tp_kos_install = NULL,
  .tp_kos_tail_install = NULL,
};


int koskari_callable_init(PyObject *mod) {
  (void) mod;
  return PyType_Ready(KOS_CALLABLE_PYTYPE(KosCallableType));
}


int koskari_callable_clear(PyObject *mod) {
  (void) mod;
  return 0;
}


// The end.
