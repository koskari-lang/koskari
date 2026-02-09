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


/**
   koskari.lib

   Native extension module for Koskari

   author: Christopher O'Brien <obriencj@gmail.com>
   license: GPL v.3
*/


#include "lib.h"


/* === module methods === */


static PyMethodDef libmod_methods[] = {

  { NULL, },
};


/* === module lifecycle === */


static int koskari_lib_init(PyObject *mod) {
  if (! mod)
    return -1;

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  /* compat must be initialized first */
  if (koskari_compat_init(mod))
    return -1;

  /* initialize implementation units */
  if (koskari_atom_init(mod))
    return -1;

  if (koskari_pair_init(mod))
    return -1;

  if (koskari_ref_init(mod))
    return -1;

  if (koskari_namespace_init(mod))
    return -1;

  if (koskari_bytecode_init(mod))
    return -1;

  if (koskari_formals_init(mod))
    return -1;

  if (koskari_values_init(mod))
    return -1;

  if (koskari_operator_init(mod))
    return -1;

  if (koskari_stack_init(mod))
    return -1;

  if (koskari_utils_init(mod))
    return -1;

  if (koskari_parse_init(mod))
    return -1;

  if (koskari_eval_init(mod))
    return -1;

  if (koskari_func_init(mod))
    return -1;

  return 0;
}


int koskari_lib_clear(PyObject *mod) {
  if (! mod)
    return -1;

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  /* clear implementation units */
  if (koskari_func_clear(mod))
    return -1;

  if (koskari_eval_clear(mod))
    return -1;

  if (koskari_parse_clear(mod))
    return -1;

  if (koskari_utils_clear(mod))
    return -1;

  if (koskari_stack_clear(mod))
    return -1;

  if (koskari_operator_clear(mod))
    return -1;

  if (koskari_values_clear(mod))
    return -1;

  if (koskari_formals_clear(mod))
    return -1;

  if (koskari_bytecode_clear(mod))
    return -1;

  if (koskari_namespace_clear(mod))
    return -1;

  if (koskari_ref_clear(mod))
    return -1;

  if (koskari_pair_clear(mod))
    return -1;

  if (koskari_atom_clear(mod))
    return -1;

  /* compat should be cleared last */
  if (koskari_compat_clear(mod))
    return -1;

  PyDict_Clear(dict);

  return 0;
}


/* === module definition === */


static struct PyModuleDef libmod = {
  .m_base = PyModuleDef_HEAD_INIT,
  .m_name = "koskari.lib",
  .m_doc = "Native extension module for Koskari",
  .m_size = -1,
  .m_methods = libmod_methods,
  .m_slots = NULL,
  .m_traverse = NULL,
  .m_clear = koskari_lib_clear,
  .m_free = NULL,
};


PyMODINIT_FUNC PyInit_lib(void) {
  PyObject *mod = PyModule_Create(&libmod);
  if (! mod)
    return NULL;

  if (koskari_lib_init(mod)) {
    Py_DECREF(mod);
    return NULL;
  }

  return mod;
}


// The end.
