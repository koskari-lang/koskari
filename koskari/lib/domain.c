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
   koskari.lib - domain module

   KosDomain type: identity and lifecycle anchor for dynamic context
   (ambient domain). Each KosContext owns a domain; new contexts inherit
   the active context's domain when kos_ctx_current() is non-NULL.

   author: Christopher O'Brien <obriencj@preoccupied.net>
   license: GNU General Public License v3
   ai-assistant: Claude Opus 4.5 via Cursor
*/


#include "lib.h"


/* === Per-thread TLS state for domain-scoped context and token stacks === */

#define KOS_TLS_STACK_MAX 32

typedef struct {
  KosContext *ctx_stack[KOS_TLS_STACK_MAX];
  size_t ctx_depth;
  KosEvaluatorToken *token_stack[KOS_TLS_STACK_MAX];
  size_t token_depth;
} KosTlsState;

static Py_tss_t tls_state_key = Py_tss_NEEDS_INIT;


static KosTlsState *get_or_alloc_tls_state(void) {
  KosTlsState *s = (KosTlsState *) PyThread_tss_get(&tls_state_key);
  if (! s) {
    s = (KosTlsState *) PyMem_Malloc(sizeof(KosTlsState));
    if (! s)
      return NULL;
    s->ctx_depth = 0;
    s->token_depth = 0;
    PyThread_tss_set(&tls_state_key, s);
  }
  return s;
}


static void push_active_token(KosEvaluatorToken *token) {
  KosTlsState *s = get_or_alloc_tls_state();
  if (s && s->token_depth < KOS_TLS_STACK_MAX)
    s->token_stack[s->token_depth++] = token;
}


static void pop_active_token(KosEvaluatorToken *token) {
  (void) token;
  KosTlsState *s = (KosTlsState *) PyThread_tss_get(&tls_state_key);
  if (s && s->token_depth > 0) {
    s->token_depth--;
    if (s->ctx_depth == 0 && s->token_depth == 0) {
      PyMem_Free(s);
      PyThread_tss_set(&tls_state_key, NULL);
    }
  }
}


static void push_active_ctx(KosContext *ctx) {
  KosTlsState *s = get_or_alloc_tls_state();
  if (s && s->ctx_depth < KOS_TLS_STACK_MAX)
    s->ctx_stack[s->ctx_depth++] = ctx;
}


static void pop_active_ctx(void) {
  KosTlsState *s = (KosTlsState *) PyThread_tss_get(&tls_state_key);
  if (s && s->ctx_depth > 0)
    s->ctx_depth--;
}


/* === domain-scoped ambient context API === */


KosContext *kos_ctx_current(void) {
  KosTlsState *s = (KosTlsState *) PyThread_tss_get(&tls_state_key);
  return (s && s->ctx_depth > 0) ? s->ctx_stack[s->ctx_depth - 1] : NULL;
}


PyObject *kos_domain_current(void) {
  KosContext *c = kos_ctx_current();
  return (c && KosContext_DOMAIN(c)) ? KosContext_DOMAIN(c) : NULL;
}


void kos_domain_push_scope(KosContext *ctx) {
  push_active_token(KosContext_TOKEN(ctx));
  push_active_ctx(ctx);
  KosEvaluatorToken_IncrementLiveDepth(KosContext_TOKEN(ctx));
}


void kos_domain_pop_scope(KosContext *ctx) {
  KosEvaluatorToken_DecrementLiveDepth(KosContext_TOKEN(ctx));
  pop_active_ctx();
  pop_active_token(KosContext_TOKEN(ctx));
}


/* === KosDomain implementation === */


static PyObject *domain_repr(PyObject *self) {
  return PyUnicode_FromFormat("<Domain %p>", (void *) self);
}


PyTypeObject KosDomainType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.Domain",
  .tp_basicsize = sizeof(KosDomain),
  .tp_itemsize = 0,
  .tp_new = PyType_GenericNew,
  .tp_repr = domain_repr,
  .tp_flags = Py_TPFLAGS_DEFAULT,
};


KosDomain *KosDomain_New(void) {
  KosDomain *domain = PyObject_New(KosDomain, &KosDomainType);
  if (! domain)
    return NULL;
  domain->version = 0;
  return domain;
}


/* === unit lifecycle === */


int koskari_domain_init(PyObject *mod) {
  if (PyType_Ready(&KosDomainType) < 0)
    return -1;
  Py_INCREF(&KosDomainType);
  if (PyModule_AddObject(mod, "Domain",
                         (PyObject *) &KosDomainType) < 0) {
    Py_DECREF(&KosDomainType);
    return -1;
  }
  if (PyThread_tss_create(&tls_state_key) != 0)
    return -1;
  return 0;
}


int koskari_domain_clear(PyObject *mod) {
  (void) mod;
  PyThread_tss_delete(&tls_state_key);
  return 0;
}


// The end.
