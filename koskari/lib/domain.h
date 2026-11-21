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
                            outside any parameterize extent goes here */
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
   for Python-side entry with no context in hand: it reads the cells in
   force on the thread, then the root scope of the domain in force, the
   one an eval entered here would run under: a `with Domain():` block
   opened inside the active context's callout, else the active
   context's domain, else the innermost block's, else the thread
   default. A thread default not yet minted has nothing bound and is
   not minted here.

   Returns borrowed reference, or NULL when nothing is bound.
*/
PyObject *kos_param_lookup(KosContext *ctx, PyObject *key);
PyObject *kos_param_lookup_ambient(PyObject *key);


/**
   Resolve the cell bound to name (key) in the root scope, creating an
   empty cell on miss. This is where a store with no enclosing
   parameterize extent goes.

   The ambient variant discovers the domain the same way as
   kos_param_lookup_ambient, minting the thread default domain when it
   is the one answering and does not exist yet.

   Returns new reference, or NULL on error.
*/
PyObject *kos_param_root_resolve(KosContext *ctx, PyObject *key);
PyObject *kos_param_root_resolve_ambient(PyObject *key);


/**
   A context's param shadow dict, lazily created.
   Returns borrowed reference, or NULL on allocation failure.
*/
PyObject *kos_context_param_refs(KosContext *ctx);


/* Set once a Python parameterize span has been entered anywhere in
   the process, never cleared: before that no stack has one and the
   span chain is not consulted, so a boundary entry pays one load;
   after it a chain may be open on any task, so the var is read. */
extern int kos_param_spans_used;

/* Set once a context's own cell table has been made anywhere in the
   process, never cleared. Every table an ambient read can see comes
   from a span or from kos_context_param_refs, so with both flags clear
   a snapshot is NULL without resolving the thread. */
extern int kos_param_tables_used;


/** Whether any parameterize cell table can exist on this process. */
static inline int kos_param_cells_possible(void) {
  return kos_param_spans_used || kos_param_tables_used;
}


/**
   The table of the innermost Python parameterize span opened inside
   the active context (NULL active: with no context active), or NULL
   when the top span is outer to it or there is none. Borrowed.
*/
PyObject *kos_param_span_cells(KosTlsState *s);


/**
   The cell table an ambient read on this thread sees before the root
   scope: the innermost Python parameterize span opened inside the
   active context, else the active context's own table. A new context
   takes it at creation. active is the thread's active context or
   NULL (kos_tls_active_context). Borrowed, or NULL when there are no
   cells.
*/
static inline PyObject *kos_param_ambient_cells(
    KosTlsState *s,
    KosContext *active) {

  if (unlikely(kos_param_spans_used)) {
    PyObject *table = kos_param_span_cells(s);
    if (table)
      return table;
  }
  return active ? KosContext_PARAM_REFS(active) : NULL;
}


/* koskari.lib.Parameterize: the Python-side parameterize extent */
extern PyTypeObject KosParamSpanType;


/* === domain-scoped ambient context API === */


void kos_domain_push_scope(KosContext *ctx);
void kos_domain_pop_scope(KosContext *ctx);


/* === unit lifecycle === */


int koskari_domain_init(PyObject *mod);
int koskari_domain_clear(PyObject *mod);


#endif


// The end.
