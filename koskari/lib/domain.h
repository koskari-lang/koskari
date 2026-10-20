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
  PyObject *root_refs;  /* PyDict: name -> cell (KosRef); the root scope
                            of this computation. Never copied: contexts
                            reach it by resolution, and a store made
                            outside any parameterize extent lands here */
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


/* === param cell store API === */


/**
   Resolve the cell bound to name (key) for reading: the context's
   shadow table (written by parameterize), then the root scope of the
   context's domain. Creates nothing and sets no error on a miss.

   The lookup variant takes the context from the caller, which is how
   the evaluator resolves (no TLS on that path). The ambient variant is
   for Python-side entry with no context in hand: it discovers the
   active context, falling back to the innermost python-scope domain's
   root scope.

   Returns borrowed reference, or NULL when nothing is bound.
*/
PyObject *kos_param_lookup(KosContext *ctx, PyObject *key);
PyObject *kos_param_lookup_ambient(PyObject *key);


/**
   Resolve the cell bound to name (key) in the root scope, creating an
   empty cell on miss. This is where a store with no enclosing
   parameterize extent lands.

   The ambient variant discovers the domain the same way as
   kos_param_lookup_ambient, and fails with RuntimeError when neither a
   context nor a python domain scope is active.

   Returns new reference, or NULL on error.
*/
PyObject *kos_param_root_resolve(KosContext *ctx, PyObject *key);
PyObject *kos_param_root_resolve_ambient(PyObject *key);


/**
   A context's param shadow dict, lazily created.
   Returns borrowed reference, or NULL on allocation failure.
*/
PyObject *kos_context_param_refs(KosContext *ctx);


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
