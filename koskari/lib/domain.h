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


#ifndef KOSKARI_DOMAIN_H
#define KOSKARI_DOMAIN_H


#include <Python.h>
#include <stdint.h>

#include "eval.h"


/* === KosDomain structure === */


typedef struct {
  PyObject_HEAD
  uint64_t version;
  PyObject *param_bindings;  /* PyDict: name -> value */
} KosDomain;


/* === type object === */


extern PyTypeObject KosDomainType;


/* === type check macros === */


#define KosDomain_Check(obj)                                    \
  ((obj) && PyType_IsSubtype((obj)->ob_type, &KosDomainType))

#define KosDomain_CheckExact(obj)               \
  ((obj) && ((obj)->ob_type == &KosDomainType))


/* === public API === */


KosDomain *KosDomain_New(void);


/* === param value store API === */


/**
   Get param value from domain by name (key).
   Returns borrowed reference or NULL if unset.
*/
PyObject *kos_domain_get_param(PyObject *domain, PyObject *key);


/**
   Set param value in domain by name (key).
   Returns 0 on success, -1 on error.
*/
int kos_domain_set_param(PyObject *domain, PyObject *key, PyObject *value);


/**
   Remove param binding from domain by name (key).
   Returns 0 on success, -1 on error.
*/
int kos_domain_clear_param(PyObject *domain, PyObject *key);


/* === domain-scoped ambient context API === */


void kos_domain_push_scope(KosContext *ctx);
void kos_domain_pop_scope(KosContext *ctx);


int kos_domain_push_python_scope(PyObject *domain);
int kos_domain_pop_python_scope(const PyObject *domain);


/* === unit lifecycle === */


int koskari_domain_init(PyObject *mod);
int koskari_domain_clear(PyObject *mod);


#endif


// The end.
