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

   KosDomain type and the per-thread TLS plumbing for ambient context
   discovery. Parameter state lives on each KosContext (param_refs,
   forked at creation); the KosDomain object homes base_refs -- the
   pre-define dict root contexts copy from, and the Python-side access
   point when no context is active. Every context has a domain: the
   ambient one when present, minted fresh as needed otherwise; it is the
   identity/lifecycle anchor of the computation. See
   design/interpreter/domain-task-isolation.md.

   author: Christopher O'Brien <obriencj@preoccupied.net>
   license: GNU General Public License v3
   ai-assistant: Claude Opus 4.5 via Cursor
*/


#include "lib.h"


/* === Per-thread TLS state for domain-scoped context and token stacks === */

/* Initial capacity of each per-thread stack; stacks grow by doubling.
   Growth (rather than a fixed cap) is load-bearing: a silently dropped
   push desynchronizes kos_ctx_current(), which decides parameter cell
   inheritance at context creation. Growth failure is OOM-only and fatal
   -- continuing with a desynced stack would corrupt fork decisions
   silently. */
#define KOS_TLS_STACK_INITIAL 32

typedef struct {
  KosContext **ctx_stack;
  size_t ctx_depth;
  size_t ctx_cap;
  KosEvalState **eval_stack;
  size_t eval_depth;
  size_t eval_cap;
  KosEvaluatorToken **token_stack;
  size_t token_depth;
  size_t token_cap;
  PyObject **python_domain_stack;
  size_t python_domain_depth;
  size_t python_domain_cap;
} KosTlsState;

static Py_tss_t tls_state_key = Py_tss_NEEDS_INIT;


/* Grow `stack` (an array of pointer-sized slots) to admit index `depth`.
   Returns the (possibly moved) array, or aborts on allocation failure. */
static void *tls_stack_reserve(void *stack, size_t *cap, size_t depth) {
  size_t ncap;
  void *grown;

  if (depth < *cap)
    return stack;

  ncap = *cap ? *cap * 2 : KOS_TLS_STACK_INITIAL;
  grown = PyMem_Realloc(stack, ncap * sizeof(void *));
  if (! grown)
    Py_FatalError("koskari: TLS stack allocation failed");
  *cap = ncap;
  return grown;
}


static void tls_state_free(KosTlsState *s) {
  PyMem_Free(s->ctx_stack);
  PyMem_Free(s->eval_stack);
  PyMem_Free(s->token_stack);
  PyMem_Free(s->python_domain_stack);
  PyMem_Free(s);
  PyThread_tss_set(&tls_state_key, NULL);
}


static KosTlsState *get_or_alloc_tls_state(void) {
  KosTlsState *s = (KosTlsState *) PyThread_tss_get(&tls_state_key);
  if (! s) {
    s = (KosTlsState *) PyMem_Malloc(sizeof(KosTlsState));
    if (! s)
      return NULL;
    memset(s, 0, sizeof(KosTlsState));
    PyThread_tss_set(&tls_state_key, s);
  }
  return s;
}


static void push_active_token(KosEvaluatorToken *token) {
  KosTlsState *s = get_or_alloc_tls_state();
  if (! s)
    Py_FatalError("koskari: TLS state allocation failed");
  s->token_stack = (KosEvaluatorToken **) tls_stack_reserve(
      s->token_stack, &s->token_cap, s->token_depth);
  s->token_stack[s->token_depth++] = token;
}


static void pop_active_token(KosEvaluatorToken *token) {
  (void) token;
  KosTlsState *s = (KosTlsState *) PyThread_tss_get(&tls_state_key);
  if (s && s->token_depth > 0) {
    s->token_depth--;
    if (s->ctx_depth == 0 && s->token_depth == 0 && s->python_domain_depth == 0)
      tls_state_free(s);
  }
}


static void push_active_ctx(KosContext *ctx) {
  KosTlsState *s = get_or_alloc_tls_state();
  if (! s)
    Py_FatalError("koskari: TLS state allocation failed");
  s->ctx_stack = (KosContext **) tls_stack_reserve(
      s->ctx_stack, &s->ctx_cap, s->ctx_depth);
  s->ctx_stack[s->ctx_depth++] = ctx;
}


static void pop_active_ctx(void) {
  KosTlsState *s = (KosTlsState *) PyThread_tss_get(&tls_state_key);
  if (s && s->ctx_depth > 0)
    s->ctx_depth--;
}


void kos_tls_push_active_eval(KosEvalState *es) {
  KosTlsState *s;

  if (! es)
    return;
  s = get_or_alloc_tls_state();
  if (! s)
    Py_FatalError("koskari: TLS state allocation failed");
  s->eval_stack = (KosEvalState **) tls_stack_reserve(
      s->eval_stack, &s->eval_cap, s->eval_depth);
  s->eval_stack[s->eval_depth++] = es;
}


void kos_tls_pop_active_eval(void) {
  KosTlsState *s = (KosTlsState *) PyThread_tss_get(&tls_state_key);
  if (s && s->eval_depth > 0)
    s->eval_depth--;
}


KosEvalState *kos_tls_active_eval_for_ctx(const KosContext *ctx) {
  KosTlsState *s;
  size_t i;

  if (! ctx)
    return NULL;
  s = (KosTlsState *) PyThread_tss_get(&tls_state_key);
  if (! s)
    return NULL;
  for (i = s->eval_depth; i > 0; i--) {
    KosEvalState *es = s->eval_stack[i - 1];
    if (es && KosEvalState_CONTEXT(es) == ctx)
      return es;
  }
  return NULL;
}


/* === domain-scoped ambient context API === */


KosContext *kos_ctx_current(void) {
  KosTlsState *s = (KosTlsState *) PyThread_tss_get(&tls_state_key);
  return (s && s->ctx_depth > 0) ? s->ctx_stack[s->ctx_depth - 1] : NULL;
}


PyObject *kos_domain_current(void) {
  KosTlsState *s = (KosTlsState *) PyThread_tss_get(&tls_state_key);
  KosContext *c;

  if (s && s->ctx_depth > 0) {
    c = s->ctx_stack[s->ctx_depth - 1];
    if (c && KosContext_DOMAIN(c))
      return KosContext_DOMAIN(c);
  }
  if (s && s->python_domain_depth > 0)
    return s->python_domain_stack[s->python_domain_depth - 1];
  return NULL;
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


int kos_domain_push_python_scope(PyObject *domain) {
  KosTlsState *s;

  if (! domain || ! KosDomain_CheckExact(domain)) {
    PyErr_SetString(PyExc_TypeError, "expected a Domain instance");
    return -1;
  }
  s = get_or_alloc_tls_state();
  if (! s)
    return -1;
  s->python_domain_stack = (PyObject **) tls_stack_reserve(
      (void *) s->python_domain_stack, &s->python_domain_cap,
      s->python_domain_depth);
  s->python_domain_stack[s->python_domain_depth++] = domain;
  Py_INCREF(domain);
  return 0;
}


int kos_domain_pop_python_scope(const PyObject *domain) {
  KosTlsState *s = (KosTlsState *) PyThread_tss_get(&tls_state_key);

  if (! s || s->python_domain_depth == 0) {
    PyErr_SetString(PyExc_RuntimeError, "python domain stack underflow");
    return -1;
  }
  if (domain && s->python_domain_stack[s->python_domain_depth - 1] != domain) {
    PyErr_SetString(PyExc_RuntimeError,
                    "python domain stack mismatch: popped domain not at top");
    return -1;
  }
  s->python_domain_depth--;
  Py_DECREF(s->python_domain_stack[s->python_domain_depth]);
  s->python_domain_stack[s->python_domain_depth] = NULL;
  if (s->ctx_depth == 0 && s->token_depth == 0 && s->python_domain_depth == 0)
    tls_state_free(s);
  return 0;
}


/* === param cell store helpers === */


static PyObject *ensure_base_refs(KosDomain *domain) {
  if (! domain->base_refs) {
    domain->base_refs = PyDict_New();
    if (! domain->base_refs)
      return NULL;
  }
  return domain->base_refs;
}


PyObject *kos_domain_param_refs(PyObject *domain_obj) {
  if (! domain_obj || ! KosDomain_CheckExact(domain_obj)) {
    PyErr_SetString(PyExc_TypeError, "expected a Domain instance");
    return NULL;
  }
  return ensure_base_refs((KosDomain *) domain_obj);
}


PyObject *kos_context_param_refs(KosContext *ctx) {
  if (! KosContext_PARAM_REFS(ctx)) {
    KosContext_PARAM_REFS(ctx) = PyDict_New();
    if (! KosContext_PARAM_REFS(ctx))
      return NULL;
  }
  return KosContext_PARAM_REFS(ctx);
}


PyObject *kos_param_refs_peek(void) {
  KosTlsState *s = (KosTlsState *) PyThread_tss_get(&tls_state_key);

  if (s && s->ctx_depth > 0)
    return KosContext_PARAM_REFS(s->ctx_stack[s->ctx_depth - 1]);
  if (s && s->python_domain_depth > 0)
    return ((KosDomain *) s->python_domain_stack[s->python_domain_depth - 1])
        ->base_refs;
  return NULL;
}


PyObject *kos_param_refs_reserve(void) {
  KosTlsState *s = (KosTlsState *) PyThread_tss_get(&tls_state_key);

  if (s && s->ctx_depth > 0)
    return kos_context_param_refs(s->ctx_stack[s->ctx_depth - 1]);
  if (s && s->python_domain_depth > 0)
    return ensure_base_refs(
        (KosDomain *) s->python_domain_stack[s->python_domain_depth - 1]);
  return NULL;
}


PyObject *kos_param_resolve_ref_in(PyObject *cells, PyObject *key) {
  PyObject *cell;

  cell = PyDict_GetItem(cells, key);
  if (cell) {
    Py_INCREF(cell);
    return cell;
  }

  cell = KosSymbolicRef_New(key, NULL);
  if (! cell)
    return NULL;
  if (PyDict_SetItem(cells, key, cell) < 0) {
    Py_DECREF(cell);
    return NULL;
  }
  return cell;
}


PyObject *kos_domain_resolve_ref(PyObject *domain_obj, PyObject *key) {
  PyObject *cells;

  cells = kos_domain_param_refs(domain_obj);
  if (! cells)
    return NULL;

  return kos_param_resolve_ref_in(cells, key);
}


/* === KosDomain implementation === */


static PyObject *domain_enter(PyObject *self) {
  if (kos_domain_push_python_scope(self) < 0)
    return NULL;
  Py_INCREF(self);
  return self;
}


static PyObject *domain_exit(PyObject *self, PyObject *args) {
  PyObject *exc_type = NULL;
  PyObject *exc_val = NULL;
  PyObject *exc_tb = NULL;

  (void) PyArg_UnpackTuple(args, "__exit__", 3, 3,
                          &exc_type, &exc_val, &exc_tb);
  if (kos_domain_pop_python_scope(self) < 0)
    return NULL;
  Py_RETURN_NONE;
}


static PyObject *m_parameter(PyObject *mod, PyObject *args) {
  PyObject *key = NULL;
  PyObject *key_str = NULL;
  PyObject *default_val = NULL;
  PyObject *cells;
  PyObject *cell;
  PyObject *value;

  (void) mod;
  if (! PyArg_ParseTuple(args, "O|O:parameter", &key, &default_val))
    return NULL;

  if (! key || key == Py_None) {
    PyErr_SetString(PyExc_ValueError, "parameter requires a key (name)");
    return NULL;
  }

  key_str = kos_key_to_str(key);
  if (! key_str)
    return NULL;

  /* value query: observational only. No cell is created and no default
     is written; the default answers the caller when the store cannot.
     Cell operations (ParamRef, parameter-ref) are where places are
     materialized and declared defaults fill. */
  cells = kos_param_refs_peek();
  if (! cells && ! kos_ctx_current() && ! kos_domain_current()) {
    if (default_val) {
      Py_DECREF(key_str);
      Py_INCREF(default_val);
      return default_val;
    }

    Py_DECREF(key_str);
    PyErr_SetString(PyExc_RuntimeError,
                    "parameter outside active domain and no default");
    return NULL;
  }

  cell = cells ? PyDict_GetItem(cells, key_str) : NULL;
  Py_DECREF(key_str);
  value = cell ? KosSymbolicRef_VALUE(cell) : NULL;
  if (value) {
    Py_INCREF(value);
    return value;
  }

  if (default_val) {
    Py_INCREF(default_val);
    return default_val;
  }

  PyErr_Format(PyExc_NameError, "Param %S has no binding and no default", key);
  return NULL;
}


static PyObject *m_parameter_ref(PyObject *mod, PyObject *args) {
  PyObject *key = NULL;
  PyObject *key_str = NULL;
  PyObject *default_val = NULL;
  PyObject *cells;
  PyObject *cell;

  (void) mod;
  if (! PyArg_ParseTuple(args, "O|O:parameter-ref", &key, &default_val))
    return NULL;

  if (! key || key == Py_None) {
    PyErr_SetString(PyExc_ValueError, "parameter-ref requires a key (name)");
    return NULL;
  }

  key_str = kos_key_to_str(key);
  if (! key_str)
    return NULL;

  cells = kos_param_refs_reserve();
  if (! cells) {
    Py_DECREF(key_str);
    if (! PyErr_Occurred())
      PyErr_SetString(PyExc_RuntimeError,
                      "parameter-ref outside active domain");
    return NULL;
  }

  cell = kos_param_resolve_ref_in(cells, key_str);
  Py_DECREF(key_str);
  if (! cell)
    return NULL;

  if (default_val && ! KosSymbolicRef_VALUE(cell)) {
    /* cell operation: the declared default fills the empty place (D6
       cell rules; value queries never do this) */
    Py_INCREF(default_val);
    KosSymbolicRef_VALUE(cell) = default_val;
  }

  return cell;
}


static int domain_snapshot_set_item(
    PyObject *dict,
    const char *key,
    PyObject *value) {

  if (PyDict_SetItemString(dict, key, value) < 0) {
    Py_DECREF(value);
    return -1;
  }
  Py_DECREF(value);
  return 0;
}


static int domain_snapshot_set_uint(PyObject *dict, const char *key, uint32_t value) {

  return domain_snapshot_set_item(
      dict, key, PyLong_FromUnsignedLong((unsigned long) value));
}


static PyObject *context_to_dict(KosContext *ctx) {

  PyObject *out = PyDict_New();
  if (! out)
    return NULL;

  if (domain_snapshot_set_item(out, "schema_version", PyLong_FromLong(1)) < 0
      || domain_snapshot_set_item(out, "context_id", PyLong_FromVoidPtr((void *) ctx)) < 0
      || domain_snapshot_set_uint(out, "flags", KosContext_FLAGS(ctx)) < 0
      || domain_snapshot_set_uint(out, "suspension_event",
                                  (uint32_t) KosContext_SUSPENSION_EVENT(ctx)) < 0) {
    Py_DECREF(out);
    return NULL;
  }

  PyObject *domain = KosContext_DOMAIN(ctx) ? KosContext_DOMAIN(ctx) : Py_None;
  Py_INCREF(domain);
  if (domain_snapshot_set_item(out, "domain", domain) < 0) {
    Py_DECREF(out);
    return NULL;
  }

  {
    KosEvalState *active_es = kos_tls_active_eval_for_ctx(ctx);
    PyObject *ns;

    if (active_es && KosEvalState_NAMESPACE(active_es))
      ns = KosEvalState_NAMESPACE(active_es);
    else if (KosContext_NAMESPACE(ctx))
      ns = KosContext_NAMESPACE(ctx);
    else
      ns = Py_None;
    Py_INCREF(ns);
    if (domain_snapshot_set_item(out, "namespace", ns) < 0) {
      Py_DECREF(out);
      return NULL;
    }
  }

  {
    KosEvalState *active_es = kos_tls_active_eval_for_ctx(ctx);
    KosDynNode *dyn_top = active_es
        ? KosEvalState_DYN_TOP(active_es)
        : KosContext_DYN_TOP(ctx);
    PyObject *nodes = kos_dynchain_snapshot_to_list(dyn_top);
    if (! nodes) {
      Py_DECREF(out);
      return NULL;
    }
    if (domain_snapshot_set_item(out, "dynnodes", nodes) < 0) {
      Py_DECREF(out);
      return NULL;
    }
  }

  return out;
}


static PyObject *domain_current_domain(PyObject *cls, PyObject *unused) {

  (void) cls;
  (void) unused;
  PyObject *domain = kos_domain_current();
  if (! domain)
    Py_RETURN_NONE;
  Py_INCREF(domain);
  return domain;
}


static PyObject *domain_get_contexts(const PyObject *self, PyObject *unused) {

  (void) unused;
  KosTlsState *s = (KosTlsState *) PyThread_tss_get(&tls_state_key);
  PyObject *out = PyList_New(0);
  if (! out)
    return NULL;

  if (! s || s->ctx_depth == 0)
    return out;

  for (size_t i = s->ctx_depth; i--; ) {
    KosContext *ctx = s->ctx_stack[i];
    if (! ctx || KosContext_DOMAIN(ctx) != self)
      continue;
    PyObject *entry = context_to_dict(ctx);
    if (! entry || PyList_Append(out, entry) < 0) {
      Py_XDECREF(entry);
      Py_DECREF(out);
      return NULL;
    }
    Py_DECREF(entry);
  }

  return out;
}


static PyMethodDef domain_methods[] = {
  {"current_domain", (PyCFunction) domain_current_domain,
   METH_CLASS | METH_NOARGS,
   "Return the active Domain from current TLS scope, or None"},
  {"get_contexts", (PyCFunction) domain_get_contexts, METH_NOARGS,
   "Return current-thread context snapshots for this Domain"},
  {"__enter__", (PyCFunction) domain_enter, METH_NOARGS,
   "Push this domain onto the per-thread scope stack"},
  {"__exit__", (PyCFunction) domain_exit, METH_VARARGS,
   "Pop this domain from the per-thread scope stack"},
  {NULL, NULL, 0, NULL},
};


static PyMethodDef domain_mod_methods[] = {
  { "parameter", (PyCFunction) m_parameter, METH_VARARGS,
    "parameter(name, default=...) => value\n"
    "Fetch the current domain parameter binding for name.\n"
    "If unbound, return default when provided, otherwise raise NameError.\n"
    "If no active domain, return default when provided, otherwise raise RuntimeError." },
  { "parameter_ref", (PyCFunction) m_parameter_ref, METH_VARARGS,
    "parameter_ref(name, default=...) => Cell\n"
    "Fetch the live ref currently bound to name, creating an empty ref\n"
    "on miss; an empty ref takes the default when provided.\n"
    "The result is a plain Ref: deref/store on it act on the shared\n"
    "binding, including after the extent that installed it has popped. It\n"
    "does not track later parameterize rebinding of the name; use\n"
    "parameter() for dynamic reads.\n"
    "Raises RuntimeError when no context or domain scope is active." },
  { NULL, },
};


static PyObject *domain_repr(PyObject *self) {
  return PyUnicode_FromFormat("<Domain %p>", (void *) self);
}


static int domain_traverse(PyObject *self, visitproc visit, void *arg) {
  Py_VISIT(((KosDomain *) self)->base_refs);
  return 0;
}


static int domain_clear(PyObject *self) {
  Py_CLEAR(((KosDomain *) self)->base_refs);
  return 0;
}


static void domain_dealloc(PyObject *self) {
  PyObject_GC_UnTrack(self);
  Py_CLEAR(((KosDomain *) self)->base_refs);
  Py_TYPE(self)->tp_free(self);
}


PyObject *kos_domain_get_param(PyObject *domain_obj, PyObject *key) {
  PyObject *bindings;
  PyObject *cell;
  PyObject *value;

  if (! domain_obj || ! KosDomain_CheckExact(domain_obj))
    return NULL;
  bindings = ((KosDomain *) domain_obj)->base_refs;
  if (! bindings)
    return NULL;
  cell = PyDict_GetItem(bindings, key);
  if (! cell)
    return NULL;
  value = KosSymbolicRef_VALUE(cell);
  if (value)
    Py_INCREF(value);
  return value;
}


int kos_domain_set_param(PyObject *domain_obj, PyObject *key, PyObject *value) {
  PyObject *cell;

  cell = kos_domain_resolve_ref(domain_obj, key);
  if (! cell)
    return -1;
  Py_INCREF(value);
  Py_XSETREF(KosSymbolicRef_VALUE(cell), value);
  Py_DECREF(cell);
  return 0;
}


int kos_domain_clear_param(PyObject *domain_obj, PyObject *key) {
  PyObject *bindings;
  PyObject *cell;

  if (! domain_obj || ! KosDomain_CheckExact(domain_obj))
    return -1;
  bindings = ((KosDomain *) domain_obj)->base_refs;
  if (! bindings)
    return 0;
  cell = PyDict_GetItem(bindings, key);
  if (! cell)
    return 0;
  Py_CLEAR(KosSymbolicRef_VALUE(cell));
  return 0;
}


PyTypeObject KosDomainType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.Domain",
  .tp_basicsize = sizeof(KosDomain),
  .tp_itemsize = 0,
  .tp_new = PyType_GenericNew,
  .tp_repr = domain_repr,
  .tp_dealloc = domain_dealloc,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_traverse = domain_traverse,
  .tp_clear = domain_clear,
  .tp_methods = domain_methods,
};


KosDomain *KosDomain_New(void) {
  KosDomain *domain = PyObject_GC_New(KosDomain, &KosDomainType);
  if (! domain)
    return NULL;
  domain->version = 0;
  domain->base_refs = NULL;
  PyObject_GC_Track(domain);
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

  for (PyMethodDef *mdef = domain_mod_methods; mdef->ml_name; mdef++) {
    PyObject *func = PyCFunction_New(mdef, mod);
    if (! func)
      return -1;

    if (PyModule_AddObject(mod, mdef->ml_name, func) < 0) {
      Py_DECREF(func);
      return -1;
    }
  }

  return 0;
}


int koskari_domain_clear(PyObject *mod) {
  (void) mod;
  PyThread_tss_delete(&tls_state_key);
  return 0;
}


// The end.
