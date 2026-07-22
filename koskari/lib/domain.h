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
  PyObject *base_cells;  /* PyDict: name -> cell (KosRef); the pre-define
                            dict root contexts copy at creation, and the
                            Python-side access point when no context is
                            active */
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
   Resolve the cell for name (key) in a cell store dict, creating an
   empty cell on miss. Returns new reference, or NULL on error.
*/
PyObject *kos_param_resolve_cell_in(PyObject *cells, PyObject *key);


/**
   The current parameter cell store: the active context's param_cells,
   else the innermost python-scope domain's base_cells. Returns borrowed
   reference, or NULL without error when neither is ambient.

   The peek variant never creates the dict (NULL also when the store
   exists but is unallocated); the reserve variant lazily creates it
   (NULL with error set on allocation failure).
*/
PyObject *kos_param_cells_peek(void);
PyObject *kos_param_cells_reserve(void);


/**
   A context's param cell dict, lazily created.
   Returns borrowed reference, or NULL on allocation failure.
*/
PyObject *kos_context_param_cells(KosContext *ctx);


/**
   A domain's base cell dict, lazily created.
   Returns borrowed reference, or NULL on allocation failure.
*/
PyObject *kos_domain_param_cells(PyObject *domain);


/**
   Resolve the cell for name (key) in the domain's base cells, creating
   an empty cell on miss. Returns new reference, or NULL on error.
*/
PyObject *kos_domain_resolve_cell(PyObject *domain, PyObject *key);


/**
   Get param value from domain by name (key): deref of the cell.
   Returns new reference, or NULL without error when there is no cell or
   the cell is empty.
*/
PyObject *kos_domain_get_param(PyObject *domain, PyObject *key);


/**
   Set param value in domain by name (key): write-through on the cell,
   resolving (creating) it as needed.
   Returns 0 on success, -1 on error.
*/
int kos_domain_set_param(PyObject *domain, PyObject *key, PyObject *value);


/**
   Empty the cell for name (key). The cell object stays in place, so
   holders observe the clear. No-op when no cell exists.
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
