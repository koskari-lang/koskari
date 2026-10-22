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
   discovery. Parameter resolution is two-tier: a context's param_refs
   holds the shadow cells written by parameterize (forked at context
   creation by sharing the table; the first writer copies it), and the
   KosDomain object homes root_refs, the root scope where a store with
   no enclosing extent lands. root_refs is never copied; contexts reach
   it by resolution. Every context has a domain, the identity/lifecycle
   anchor of the computation: the active context's, else the innermost
   `with Domain():` scope's, else the thread's default domain, minted on
   first need and released with the thread's record. See
   design/archive/parameter-resolution.md.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
*/


#include "lib.h"


/* === Per-thread TLS state for domain-scoped context stacks === */

/* Initial capacity of each per-thread stack; stacks grow by doubling.
   Growth (rather than a fixed cap) is load-bearing: a silently dropped
   push desynchronizes kos_ctx_current(), which decides parameter cell
   inheritance at context creation. Growth failure is OOM-only and fatal
   -- continuing with a desynced stack would corrupt fork decisions
   silently. */
#define KOS_TLS_STACK_INITIAL 32

struct KosTlsState {
  /* the thread state whose dict holds the capsule; NULL once that
     capsule is gone. Compared against the current thread state to
     accept the thread slot's record without a dict lookup. */
  PyThreadState *owner;
  /* one count for the capsule, one for the thread slot that names it,
     and one per drive holding the record across its run */
  Py_ssize_t refs;
  KosContext **ctx_stack;
  size_t ctx_depth;
  size_t ctx_cap;
  PyObject **python_domain_stack;
  size_t python_domain_depth;
  size_t python_domain_cap;
  /* the thread's default domain (a reference of our own), minted on
     first need: the domain answering when no context is active and no
     `with Domain():` scope is open. Never shared between threads. */
  PyObject *default_domain;
};

/* the calling OS thread's record, a C thread-local slot: one TLS load
   on the fast path, checked against the current thread state by its
   owner field since a thread state can change on one OS thread */
static _Thread_local KosTlsState *tls_state_slot;


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


/* The record is owned by a capsule in the thread state's dict, which
   CPython clears when the thread state is destroyed, and by the thread
   slot of the OS thread that reached it; each holds one count. The
   capsule can be destroyed from another OS thread (a thread state
   cleared at interpreter shutdown, or by an embedder), which cannot
   reach this thread's slot: the record then lives on the slot's count,
   with owner cleared, until this thread next enters and releases it. */
static const char tls_state_capsule_name[] = "koskari.tls_state";
static PyObject *tls_state_dict_key = NULL;  /* interned "koskari.tls_state" */


/* Every release runs with the GIL held: the capsule destructor runs
   from the clear of a thread state's dict, and the other callers run
   on a thread inside the evaluator or a Python API entry. The default
   domain is dropped last, after the record is freed and out of the thread
   slot, so a finalizer it runs that re-enters finds no stale record. */
static void tls_state_release(KosTlsState *s) {

  PyObject *default_domain;

  if (--s->refs > 0)
    return;
  default_domain = s->default_domain;
  PyMem_Free(s->ctx_stack);
  PyMem_Free(s->python_domain_stack);
  PyMem_Free(s);
  Py_XDECREF(default_domain);
}


static void tls_state_capsule_free(PyObject *capsule) {
  KosTlsState *s = (KosTlsState *) PyCapsule_GetPointer(
      capsule, tls_state_capsule_name);
  if (! s) {
    PyErr_Clear();
    return;
  }
  s->owner = NULL;
  if (tls_state_slot == s) {
    tls_state_slot = NULL;
    tls_state_release(s);
  }
  tls_state_release(s);
}


/**
   The record for the current thread state, or NULL when it has none.
   The thread slot answers when its record's owner is the current thread
   state. Otherwise the slot's record, if any, belongs to another thread
   state or to a capsule already destroyed, and is released; the current
   thread state's dict then supplies the record, adopted into the slot
   with its own count. The key lookup cannot raise: the key is an
   interned str. Every call counts as one resolution (tls_resolve).
*/
static KosTlsState *tls_state_current(void) {
  KosTlsState *s = tls_state_slot;
  PyThreadState *ts = PyThreadState_Get();
  PyObject *dict;
  PyObject *capsule;

  KOS_TRACE_EVENTS_INC(KOS_TE_TLS_RESOLVE);

  if (s) {
    if (likely(s->owner == ts))
      return s;
    tls_state_slot = NULL;
    tls_state_release(s);
  }

  dict = tls_state_dict_key ? PyThreadState_GetDict() : NULL;
  if (! dict)
    return NULL;
  capsule = PyDict_GetItemWithError(dict, tls_state_dict_key);
  if (! capsule)
    return NULL;
  s = (KosTlsState *) PyCapsule_GetPointer(capsule, tls_state_capsule_name);
  if (! s) {
    PyErr_Clear();
    return NULL;
  }
  s->refs++;
  tls_state_slot = s;
  return s;
}


/**
   The current thread state's record, allocated when it has none: owned
   by a capsule in the thread state's dict and by the thread slot. With the
   module cleared (the dict key gone) the slot's count is the only one,
   so a finalizer entering the evaluator during interpreter teardown
   still has a record; the owner is set either way, since the slot
   answers only for a record owned by the current thread state.
*/
static KosTlsState *get_or_alloc_tls_state(void) {
  KosTlsState *s = tls_state_current();
  if (s)
    return s;

  s = (KosTlsState *) PyMem_Malloc(sizeof(KosTlsState));
  if (! s)
    return NULL;
  memset(s, 0, sizeof(KosTlsState));
  s->owner = PyThreadState_Get();
  s->refs = 1;   /* the slot's */

  if (tls_state_dict_key) {
    PyObject *capsule = PyCapsule_New(s, tls_state_capsule_name,
                                      tls_state_capsule_free);
    PyObject *dict = capsule ? PyThreadState_GetDict() : NULL;
    if (! capsule || ! dict
        || PyDict_SetItem(dict, tls_state_dict_key, capsule) < 0) {
      if (capsule) {
        /* the capsule's destructor would release s; do it here instead */
        PyCapsule_SetDestructor(capsule, NULL);
        Py_DECREF(capsule);
      }
      PyMem_Free(s);
      return NULL;
    }
    Py_DECREF(capsule);
    s->refs++;   /* the capsule's */
  }

  tls_state_slot = s;
  return s;
}


KosTlsState *kos_tls_current(void) {
  KosTlsState *s = get_or_alloc_tls_state();
  if (unlikely(! s))
    Py_FatalError("koskari: TLS state allocation failed");
  return s;
}


static void push_active_ctx(KosContext *ctx) {
  KosTlsState *s = kos_tls_current();
  s->ctx_stack = (KosContext **) tls_stack_reserve(
      s->ctx_stack, &s->ctx_cap, s->ctx_depth);
  s->ctx_stack[s->ctx_depth++] = ctx;
}


static void pop_active_ctx(void) {
  KosTlsState *s = tls_state_current();
  if (s && s->ctx_depth > 0)
    s->ctx_depth--;
}


void kos_tls_enter_drive(KosTlsState *s, KosContext *ctx) {
  s->refs++;   /* the drive's */
  s->ctx_stack = (KosContext **) tls_stack_reserve(
      s->ctx_stack, &s->ctx_cap, s->ctx_depth);
  s->ctx_stack[s->ctx_depth++] = ctx;
  KosEvaluatorToken_IncrementLiveDepth(KosContext_TOKEN(ctx));
}


PyThreadState *kos_tls_thread_state(const KosTlsState *s) {
  return s->owner;
}


void kos_tls_leave_drive(KosTlsState *s, KosContext *ctx) {
  KosEvaluatorToken_DecrementLiveDepth(KosContext_TOKEN(ctx));
  if (s->ctx_depth > 0)
    s->ctx_depth--;
  tls_state_release(s);
}


/* === domain-scoped ambient context API === */


KosContext *kos_tls_active_context(const KosTlsState *s) {
  return s->ctx_depth > 0 ? s->ctx_stack[s->ctx_depth - 1] : NULL;
}


KosContext *kos_ctx_current(void) {
  KosTlsState *s = tls_state_current();
  return s ? kos_tls_active_context(s) : NULL;
}


/**
   The domain answering on the thread without minting one: the active
   context's, else the innermost `with Domain():` scope's, else the
   thread default once it exists. Borrowed, or NULL when the thread
   default has not been minted yet.
*/
static PyObject *domain_ambient_peek(const KosTlsState *s) {

  KosContext *c;

  if (s->ctx_depth > 0) {
    c = s->ctx_stack[s->ctx_depth - 1];
    if (c && KosContext_DOMAIN(c))
      return KosContext_DOMAIN(c);
  }
  if (s->python_domain_depth > 0)
    return s->python_domain_stack[s->python_domain_depth - 1];
  return s->default_domain;
}


/* The thread default domain, minted on the first call. Borrowed, or
   NULL with an error set. */
static PyObject *tls_default_domain(KosTlsState *s) {
  if (! s->default_domain)
    s->default_domain = (PyObject *) KosDomain_New();
  return s->default_domain;
}


PyObject *kos_tls_domain(KosTlsState *s) {
  PyObject *domain = domain_ambient_peek(s);
  return domain ? domain : tls_default_domain(s);
}


/* A failure to allocate the record is a MemoryError here, not the
   fatal error kos_tls_current raises. */
PyObject *kos_domain_current(void) {

  KosTlsState *s = get_or_alloc_tls_state();

  if (! s) {
    if (! PyErr_Occurred())
      PyErr_NoMemory();
    return NULL;
  }
  return kos_tls_domain(s);
}


void kos_domain_push_scope(KosContext *ctx) {
  push_active_ctx(ctx);
  KosEvaluatorToken_IncrementLiveDepth(KosContext_TOKEN(ctx));
}


void kos_domain_pop_scope(KosContext *ctx) {
  KosEvaluatorToken_DecrementLiveDepth(KosContext_TOKEN(ctx));
  pop_active_ctx();
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
  KosTlsState *s = tls_state_current();

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
  return 0;
}


/* === param cell store === */


static PyObject *ensure_root_refs(KosDomain *domain) {
  if (! domain->root_refs) {
    domain->root_refs = PyDict_New();
    if (! domain->root_refs)
      return NULL;
  }
  return domain->root_refs;
}


/**
   The context's parameterize cell table, for writing. The returned
   table is this context's own to write: it is created on first need,
   and a table shared with another context or with an unstarted
   coroutine (the refcount records the other holders) is copied first.
   Borrowed. Returns NULL with an error set.
*/
PyObject *kos_context_param_refs(KosContext *ctx) {

  PyObject *table = KosContext_PARAM_REFS(ctx);

  if (! table) {
    table = PyDict_New();
    if (! table)
      return NULL;
    KosContext_PARAM_REFS(ctx) = table;
    return table;
  }

  if (Py_REFCNT(table) > 1) {
    PyObject *own = PyDict_Copy(table);
    if (! own)
      return NULL;
    Py_SETREF(KosContext_PARAM_REFS(ctx), own);
    table = own;
  }
  return table;
}


/* Probe a domain's root scope without creating it. Borrowed or NULL. */
static PyObject *root_probe(PyObject *domain_obj, PyObject *key) {
  PyObject *root;

  if (! domain_obj || ! KosDomain_CheckExact(domain_obj))
    return NULL;
  root = ((KosDomain *) domain_obj)->root_refs;
  if (! root)
    return NULL;
  return PyDict_GetItem(root, key);
}


PyObject *kos_param_lookup(KosContext *ctx, PyObject *key) {
  PyObject *shadow;
  PyObject *cell;

  if (! ctx)
    return NULL;

  shadow = KosContext_PARAM_REFS(ctx);
  if (shadow) {
    cell = PyDict_GetItem(shadow, key);
    if (cell)
      return cell;
  }

  return root_probe(KosContext_DOMAIN(ctx), key);
}


PyObject *kos_param_lookup_ambient(PyObject *key) {
  KosTlsState *s = tls_state_current();
  KosContext *ctx;

  if (! s)
    return NULL;

  ctx = s->ctx_depth > 0 ? s->ctx_stack[s->ctx_depth - 1] : NULL;
  if (ctx)
    return kos_param_lookup(ctx, key);

  /* a thread default not minted yet has nothing bound */
  return root_probe(domain_ambient_peek(s), key);
}


/* Resolve-or-create in an already-materialized root scope dict. */
static PyObject *root_resolve_in(PyObject *root, PyObject *key) {
  PyObject *cell;

  cell = PyDict_GetItem(root, key);
  if (cell) {
    Py_INCREF(cell);
    return cell;
  }

  cell = KosSymbolicRef_New(key, NULL);
  if (! cell)
    return NULL;
  if (PyDict_SetItem(root, key, cell) < 0) {
    Py_DECREF(cell);
    return NULL;
  }
  return cell;
}


PyObject *kos_param_root_resolve(KosContext *ctx, PyObject *key) {
  PyObject *domain_obj;
  PyObject *root;

  domain_obj = ctx ? KosContext_DOMAIN(ctx) : NULL;
  if (! domain_obj || ! KosDomain_CheckExact(domain_obj)) {
    PyErr_SetString(PyExc_RuntimeError,
                    "parameter store outside an active domain");
    return NULL;
  }

  root = ensure_root_refs((KosDomain *) domain_obj);
  if (! root)
    return NULL;

  return root_resolve_in(root, key);
}


PyObject *kos_param_root_resolve_ambient(PyObject *key) {
  KosContext *ctx = kos_ctx_current();
  PyObject *domain_obj;
  PyObject *root;

  if (ctx)
    return kos_param_root_resolve(ctx, key);

  domain_obj = kos_domain_current();
  if (! domain_obj)
    return NULL;

  root = ensure_root_refs((KosDomain *) domain_obj);
  if (! root)
    return NULL;

  return root_resolve_in(root, key);
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

  if (! PyArg_UnpackTuple(args, "__exit__", 3, 3,
                          &exc_type, &exc_val, &exc_tb))
    return NULL;
  if (kos_domain_pop_python_scope(self) < 0)
    return NULL;
  Py_RETURN_NONE;
}


static PyObject *m_parameter(PyObject *mod, PyObject *args) {
  PyObject *key = NULL;
  PyObject *key_str = NULL;
  PyObject *default_val = NULL;
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

  /* A read creates nothing. The caller's default answers when nothing
     is bound, and never lands in a store. */
  cell = kos_param_lookup_ambient(key_str);
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


static PyObject *domain_current_domain(PyObject *cls, PyObject *unused) {

  (void) cls;
  (void) unused;
  PyObject *domain = kos_domain_current();
  if (! domain)
    return NULL;
  Py_INCREF(domain);
  return domain;
}


static PyObject *domain_get_contexts(const PyObject *self, PyObject *unused) {

  (void) unused;
  KosTlsState *s = tls_state_current();
  PyObject *out = PyList_New(0);
  if (! out)
    return NULL;

  if (! s || s->ctx_depth == 0)
    return out;

  /* innermost first: the contexts running on this thread under this
     domain, as the Context objects themselves */
  for (size_t i = s->ctx_depth; i--; ) {
    KosContext *ctx = s->ctx_stack[i];
    if (! ctx || KosContext_DOMAIN(ctx) != self)
      continue;
    if (PyList_Append(out, (PyObject *) ctx) < 0) {
      Py_DECREF(out);
      return NULL;
    }
  }

  return out;
}


/**
   Both tiers of the parameter store, for inspection: "shadow" is the
   active context's parameterize cells when that context belongs to this
   Domain, "root" is this Domain's root scope. Each is a fresh dict of
   name -> cell, so callers see the binding topology without being able
   to disturb it.
*/
static PyObject *domain_param_scopes(PyObject *self, PyObject *unused) {

  (void) unused;
  KosContext *ctx = kos_ctx_current();
  PyObject *out = PyDict_New();
  PyObject *shadow;
  PyObject *root;

  if (! out)
    return NULL;

  shadow = (ctx && KosContext_DOMAIN(ctx) == self
            && KosContext_PARAM_REFS(ctx))
      ? PyDict_Copy(KosContext_PARAM_REFS(ctx))
      : PyDict_New();
  if (! shadow) {
    Py_DECREF(out);
    return NULL;
  }
  if (domain_snapshot_set_item(out, "shadow", shadow) < 0) {
    Py_DECREF(out);
    return NULL;
  }

  root = ((KosDomain *) self)->root_refs
      ? PyDict_Copy(((KosDomain *) self)->root_refs)
      : PyDict_New();
  if (! root) {
    Py_DECREF(out);
    return NULL;
  }
  if (domain_snapshot_set_item(out, "root", root) < 0) {
    Py_DECREF(out);
    return NULL;
  }

  return out;
}


static PyMethodDef domain_methods[] = {
  {"current_domain", (PyCFunction) domain_current_domain,
   METH_CLASS | METH_NOARGS,
   "Return the Domain answering on this thread: the running context's,"
   " else the innermost with-Domain scope's, else the thread default"},
  {"get_contexts", (PyCFunction) domain_get_contexts, METH_NOARGS,
   "Return the Context objects running on this thread under this Domain,"
   " innermost first; Context.snapshot() pictures each one"},
  {"param_scopes", (PyCFunction) domain_param_scopes, METH_NOARGS,
   "Return {'shadow': {name: cell}, 'root': {name: cell}} for this Domain"},
  {"__enter__", (PyCFunction) domain_enter, METH_NOARGS,
   "Push this domain onto the per-thread scope stack"},
  {"__exit__", (PyCFunction) domain_exit, METH_VARARGS,
   "Pop this domain from the per-thread scope stack"},
  {NULL, NULL, 0, NULL},
};


static PyMethodDef domain_mod_methods[] = {
  { "parameter", (PyCFunction) m_parameter, METH_VARARGS,
    "parameter(name, default=...) => value\n"
    "Resolve the value bound to name: the enclosing parameterize extent\n"
    "first, then the root scope. Creates nothing and writes nothing.\n"
    "If unbound, return default when provided, otherwise raise NameError." },
  { NULL, },
};


static PyObject *domain_repr(PyObject *self) {
  return PyUnicode_FromFormat("<Domain %p>", (void *) self);
}


static int domain_traverse(PyObject *self, visitproc visit, void *arg) {
  Py_VISIT(((KosDomain *) self)->root_refs);
  return 0;
}


static int domain_clear(PyObject *self) {
  Py_CLEAR(((KosDomain *) self)->root_refs);
  return 0;
}


static void domain_dealloc(PyObject *self) {
  PyObject_GC_UnTrack(self);
  Py_CLEAR(((KosDomain *) self)->root_refs);
  Py_TYPE(self)->tp_free(self);
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
  domain->root_refs = NULL;
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
  tls_state_dict_key = PyUnicode_InternFromString("koskari.tls_state");
  if (! tls_state_dict_key)
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


/* The thread slot outlives the module: a finalizer entering the
   evaluator after the clear still stores its record in it. */
int koskari_domain_clear(PyObject *mod) {
  (void) mod;
  Py_CLEAR(tls_state_dict_key);
  return 0;
}


// The end.
