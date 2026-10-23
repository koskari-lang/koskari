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
   it by resolution. A parameterize block opened from Python is a span
   (koskari.lib.Parameterize): the same cells, held in a table on a
   ContextVar chain that a context takes at creation and an ambient
   read consults before the active context's own table when the span
   was opened inside that context. A context has a domain while an
   eval runs it, taken at the entry from the thread running it: the
   active context's, else the innermost `with Domain():` scope's, else
   the thread's default domain, minted on first need and released with
   the thread's record. A context resumed on another thread runs under
   that thread's domain; only its cells travel with it. See
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
  size_t *ctx_serial;   /* per slot: the entry's serial, from serial */
  size_t ctx_depth;
  size_t ctx_cap;
  size_t serial;        /* counts the eval entries on this thread; a
                           parameterize span records the active entry's
                           serial, which orders it against the contexts
                           entered before and after it */
  /* the thread's default domain (a reference of our own), minted on
     first need: the domain answering when no context is active and no
     `with Domain():` block is open. Only evals entered on this thread
     run under it. */
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


/* Drop the record's default domain with the record whole and one
   count held across the drop: a finalizer the drop runs that re-enters
   finds the record where its caller left it and allocates nothing, and
   an eval it runs takes and returns a count of its own. A default
   domain such a re-entry mints is dropped in turn. Returns 0 once the
   held count comes back alone and the memory may go, 1 while a
   re-entry still holds the record. */
static int tls_state_drop_domain(KosTlsState *s) {

  PyObject *domain;

  while ((domain = s->default_domain)) {
    s->default_domain = NULL;
    s->refs = 1;
    Py_DECREF(domain);
    if (--s->refs > 0)
      return 1;
  }
  return 0;
}


/* Every release runs with the GIL held: the capsule destructor runs
   from the clear of a thread state's dict, and the other callers run
   on a thread inside the evaluator or a Python API entry. The last
   count drops the default domain before the memory goes. */
static void tls_state_release(KosTlsState *s) {

  if (--s->refs > 0)
    return;
  if (tls_state_drop_domain(s))
    return;
  PyMem_Free(s->ctx_stack);
  PyMem_Free(s->ctx_serial);
  PyMem_Free(s);
}


static void tls_state_capsule_free(PyObject *capsule) {
  KosTlsState *s = (KosTlsState *) PyCapsule_GetPointer(
      capsule, tls_state_capsule_name);
  if (! s) {
    PyErr_Clear();
    return;
  }

  if (tls_state_slot == s) {
    /* the owning thread: the capsule's count goes first; when the
       slot's is then the last, it goes with the record still the
       slot's and its owner set, so a finalizer the default domain's
       drop runs finds this record; the slot is cleared once the record
       is gone */
    tls_state_release(s);
    if (s->refs == 1) {
      tls_state_release(s);
      tls_state_slot = NULL;
      return;
    }
    tls_state_slot = NULL;
  }
  s->owner = NULL;
  tls_state_release(s);
}


/**
   The record for the current thread state, or NULL when it has none.
   The thread slot answers when its record's owner is the current thread
   state. Otherwise the slot's record, if any, belongs to another thread
   state or to a capsule already destroyed: it leaves the slot, the
   current thread state's dict supplies the record, adopted into the
   slot with its own count, and the stale record is released last, so a
   finalizer its release runs sees the slot settled and takes no count
   of its own. The key lookup cannot raise: the key is an interned str.
   Every call counts as one resolution (tls_resolve).
*/
static KosTlsState *tls_state_current(void) {
  KosTlsState *s = tls_state_slot;
  KosTlsState *stale = NULL;
  PyThreadState *ts = PyThreadState_Get();
  PyObject *dict;
  PyObject *capsule;

  KOS_TRACE_EVENTS_INC(KOS_TE_TLS_RESOLVE);

  if (s) {
    if (likely(s->owner == ts))
      return s;
    stale = s;
    tls_state_slot = NULL;
    s = NULL;
  }

  dict = tls_state_dict_key ? PyThreadState_GetDict() : NULL;
  capsule = dict ? PyDict_GetItemWithError(dict, tls_state_dict_key) : NULL;
  if (capsule) {
    s = (KosTlsState *) PyCapsule_GetPointer(capsule, tls_state_capsule_name);
    if (! s) {
      PyErr_Clear();
    } else {
      s->refs++;
      tls_state_slot = s;
    }
  }

  if (stale)
    tls_state_release(stale);
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


/* Push ctx as the active context, numbering the entry. */
static void tls_ctx_push(KosTlsState *s, KosContext *ctx) {
  size_t depth = s->ctx_depth;

  if (depth >= s->ctx_cap) {
    size_t cap = s->ctx_cap;
    s->ctx_stack = (KosContext **) tls_stack_reserve(
        s->ctx_stack, &cap, depth);
    s->ctx_serial = (size_t *) tls_stack_reserve(
        s->ctx_serial, &s->ctx_cap, depth);
  }
  s->ctx_stack[depth] = ctx;
  s->ctx_serial[depth] = ++s->serial;
  s->ctx_depth = depth + 1;
}


/* The active entry's serial, 0 when no context is active. */
static size_t tls_active_serial(const KosTlsState *s) {
  return s->ctx_depth > 0 ? s->ctx_serial[s->ctx_depth - 1] : 0;
}


static void push_active_ctx(KosContext *ctx) {
  tls_ctx_push(kos_tls_current(), ctx);
}


static void pop_active_ctx(void) {
  KosTlsState *s = tls_state_current();
  if (s && s->ctx_depth > 0)
    s->ctx_depth--;
}


void kos_tls_enter_drive(KosTlsState *s, KosContext *ctx) {
  s->refs++;   /* the drive's */
  tls_ctx_push(s, ctx);
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


/* === with-Domain blocks === */


/**
   One `with Domain():` block, open on the running stack. Blocks on
   one stack chain through prev; the top of the chain lives in a
   ContextVar, so an asyncio task runs under its own copy of the chain,
   inherits the blocks open at its creation, and never meets a block
   opened by another task.

   base_serial orders the block against the Koskari contexts on the
   thread: it is the serial of the entry active when the block was
   opened, so the block is inner to that context (opened by Python code
   the context called out to) and governs every eval entered inside
   it, while a context entered before it keeps the domain it took at
   its own entry.
*/
typedef struct {
  PyObject_HEAD
  PyObject *domain;     /* the KosDomain the block supplies */
  PyObject *prev;       /* the block beneath on this stack, or NULL */
  size_t base_serial;   /* the active entry's serial at enter, 0 when
                           no context was active */
} KosDomainScope;


/* The top block of the running stack, None when no block is open. */
static PyObject *domain_scope_var = NULL;

/* Blocks entered and not exited, process-wide: at zero the ambient
   lookup skips the ContextVar read, which keeps the eval entry free of
   it. */
static Py_ssize_t domain_scopes_open = 0;


static int domain_scope_traverse(PyObject *self, visitproc visit, void *arg) {
  KosDomainScope *scope = (KosDomainScope *) self;
  Py_VISIT(scope->domain);
  Py_VISIT(scope->prev);
  return 0;
}


static int domain_scope_clear(PyObject *self) {
  KosDomainScope *scope = (KosDomainScope *) self;
  Py_CLEAR(scope->domain);
  Py_CLEAR(scope->prev);
  return 0;
}


static void domain_scope_dealloc(PyObject *self) {
  PyObject_GC_UnTrack(self);
  domain_scope_clear(self);
  Py_TYPE(self)->tp_free(self);
}


static PyTypeObject KosDomainScopeType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.DomainScope",
  .tp_basicsize = sizeof(KosDomainScope),
  .tp_itemsize = 0,
  .tp_dealloc = domain_scope_dealloc,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_traverse = domain_scope_traverse,
  .tp_clear = domain_scope_clear,
  .tp_doc = "One open with-Domain block on the running stack",
};


/* The top block as a new reference, or NULL with no error when none
   is open. */
static KosDomainScope *domain_scope_top(void) {
  PyObject *top = NULL;

  if (domain_scopes_open == 0 || ! domain_scope_var)
    return NULL;
  if (PyContextVar_Get(domain_scope_var, NULL, &top) < 0) {
    PyErr_Clear();
    return NULL;
  }
  if (top == Py_None) {
    Py_DECREF(top);
    return NULL;
  }
  return (KosDomainScope *) top;
}


static int domain_scope_set_top(PyObject *scope) {
  PyObject *token = PyContextVar_Set(domain_scope_var,
                                     scope ? scope : Py_None);
  if (! token)
    return -1;
  Py_DECREF(token);
  return 0;
}


/**
   The domain answering on the thread without minting one: the
   innermost `with Domain():` block when it was opened under the active
   context (or with none active), else the active context's, else the
   innermost block's, else the thread default. Borrowed, or NULL when
   the thread default has not been minted yet.
*/
static PyObject *domain_ambient_peek(const KosTlsState *s) {

  KosDomainScope *top = domain_scope_top();
  KosContext *c = s->ctx_depth > 0 ? s->ctx_stack[s->ctx_depth - 1] : NULL;
  PyObject *domain;

  if (top && top->base_serial == tls_active_serial(s))
    domain = top->domain;
  else if (c && KosContext_DOMAIN(c))
    domain = KosContext_DOMAIN(c);
  else if (top)
    domain = top->domain;
  else
    domain = s->default_domain;

  Py_XDECREF(top);   /* the var keeps it */
  return domain;
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


/* Open a block supplying domain on the running stack. */
static int domain_scope_enter(PyObject *domain) {
  KosTlsState *s;
  KosDomainScope *scope;

  if (! domain_scope_var) {
    PyErr_SetString(PyExc_RuntimeError,
                    "Domain block entered after module teardown");
    return -1;
  }
  s = get_or_alloc_tls_state();
  if (! s) {
    if (! PyErr_Occurred())
      PyErr_NoMemory();
    return -1;
  }
  scope = PyObject_GC_New(KosDomainScope, &KosDomainScopeType);
  if (! scope)
    return -1;
  scope->domain = Py_NewRef(domain);
  scope->prev = (PyObject *) domain_scope_top();
  scope->base_serial = tls_active_serial(s);
  PyObject_GC_Track(scope);

  if (domain_scope_set_top((PyObject *) scope) < 0) {
    Py_DECREF(scope);
    return -1;
  }
  domain_scopes_open++;
  Py_DECREF(scope);   /* the var keeps it */
  return 0;
}


/**
   Close the innermost open block supplying domain. In order, the top
   of the chain moves to the block beneath. Out of order on its own
   stack, the block is unlinked from beneath the blocks above it. A
   block that is not in the running stack's chain was entered on
   another task and is refused.
*/
static int domain_scope_exit(PyObject *domain) {
  KosDomainScope *top;
  KosDomainScope *above;
  KosDomainScope *scope;

  if (! domain_scope_var) {
    PyErr_SetString(PyExc_RuntimeError,
                    "Domain block exited after module teardown");
    return -1;
  }
  top = domain_scope_top();
  if (top && top->domain == domain) {
    int res = domain_scope_set_top(top->prev);
    Py_DECREF(top);
    if (res < 0)
      return -1;
    domain_scopes_open--;
    return 0;
  }

  above = top;
  scope = top ? (KosDomainScope *) top->prev : NULL;
  while (scope && scope->domain != domain) {
    above = scope;
    scope = (KosDomainScope *) scope->prev;
  }
  if (! scope) {
    Py_XDECREF(top);
    PyErr_SetString(PyExc_RuntimeError,
                    "Domain block exited on a stack it was not entered on");
    return -1;
  }

  Py_XINCREF(scope->prev);
  Py_XSETREF(above->prev, scope->prev);
  Py_DECREF(top);
  domain_scopes_open--;
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


/* === Parameterize span: a parameterize extent opened from Python === */


/**
   A span is the Python-side parameterize extent: a block of Python
   execution with cells bound for its duration. It holds its own cells
   and the merged table an ambient read sees inside it: the view at
   entry plus its own cells. Spans on one stack chain through prev; the
   top of the chain lives in a ContextVar, so an asyncio task runs under
   its own copy of the chain, inherits the spans open at its creation,
   and never meets a span opened by another task.

   base_serial orders the span against the Koskari contexts on the
   thread: it is the serial of the entry active when the span was
   opened, so the span is inner to that context (opened by Python code
   the context called out to) and outer to every context entered after
   it. A context entered inside the span takes the span's table at
   creation and reads its own table from then on.

   The table is kept after exit: a context or task that took it at
   creation keeps that view, as the rule for creation-time inheritance
   requires.
*/
typedef struct {
  PyObject_HEAD
  PyObject *bindings;   /* dict str -> value, from construction */
  PyObject *own;        /* dict str -> cell: this entry's cells */
  PyObject *table;      /* dict str -> cell: the view at entry plus own */
  PyObject *base;       /* the active context's table the span was
                           built over, when the span beneath was not
                           inner to the same context; NULL otherwise */
  PyObject *prev;       /* the span beneath on this stack, or NULL */
  size_t base_serial;   /* the active entry's serial at enter, 0 when
                           no context was active */
  int entered;
} KosParamSpan;


/* The top span of the running stack, None when no span is open. */
static PyObject *param_span_var = NULL;

/* Spans entered and not exited, process-wide: at zero
   kos_param_ambient_cells skips the chain, which keeps the boundary
   entry free of the ContextVar read. */
Py_ssize_t kos_param_spans_open = 0;


/* The top span as a new reference, or NULL with no error when none. */
static KosParamSpan *param_span_top(void) {
  PyObject *top = NULL;

  if (! param_span_var)
    return NULL;
  if (PyContextVar_Get(param_span_var, NULL, &top) < 0) {
    PyErr_Clear();
    return NULL;
  }
  if (top == Py_None) {
    Py_DECREF(top);
    return NULL;
  }
  return (KosParamSpan *) top;
}


static int param_span_set_top(PyObject *span) {
  PyObject *token = PyContextVar_Set(param_span_var, span ? span : Py_None);
  if (! token)
    return -1;
  Py_DECREF(token);
  return 0;
}


PyObject *kos_param_span_cells(KosTlsState *s) {
  KosParamSpan *top = param_span_top();
  PyObject *table = NULL;

  if (top) {
    if (top->base_serial == tls_active_serial(s))
      table = top->table;
    Py_DECREF(top);   /* the var keeps it */
  }
  return table;
}


/* Rebuild a span's merged table after the span beneath it changed. */
static int param_span_rebuild(KosParamSpan *span) {
  KosParamSpan *prev = (KosParamSpan *) span->prev;
  PyObject *base;
  PyObject *table;

  base = (prev && prev->base_serial == span->base_serial)
      ? prev->table : span->base;
  table = base ? PyDict_Copy(base) : PyDict_New();
  if (! table)
    return -1;
  if (PyDict_Update(table, span->own) < 0) {
    Py_DECREF(table);
    return -1;
  }
  Py_XSETREF(span->table, table);
  return 0;
}


static PyObject *param_span_enter(KosParamSpan *self, PyObject *unused) {
  KosTlsState *s;
  KosContext *active;
  KosParamSpan *top;
  PyObject *base = NULL;
  PyObject *table = NULL;
  PyObject *own = NULL;
  PyObject *key;
  PyObject *value;
  Py_ssize_t pos = 0;

  (void) unused;
  if (self->entered) {
    PyErr_SetString(PyExc_RuntimeError,
                    "parameterize block already entered");
    return NULL;
  }
  if (! param_span_var) {
    PyErr_SetString(PyExc_RuntimeError,
                    "parameterize block entered after module teardown");
    return NULL;
  }

  s = get_or_alloc_tls_state();
  if (! s) {
    if (! PyErr_Occurred())
      PyErr_NoMemory();
    return NULL;
  }
  active = kos_tls_active_context(s);
  top = param_span_top();

  Py_CLEAR(self->base);
  if (top && top->base_serial == tls_active_serial(s)) {
    base = top->table;
  } else if (active) {
    base = KosContext_PARAM_REFS(active);
    Py_XINCREF(base);
    self->base = base;
  }

  table = base ? PyDict_Copy(base) : PyDict_New();
  if (! table)
    goto fail;
  own = PyDict_New();
  if (! own)
    goto fail;

  while (PyDict_Next(self->bindings, &pos, &key, &value)) {
    PyObject *cell = KosSymbolicRef_New(key, value);
    if (! cell)
      goto fail;
    if (PyDict_SetItem(own, key, cell) < 0
        || PyDict_SetItem(table, key, cell) < 0) {
      Py_DECREF(cell);
      goto fail;
    }
    Py_DECREF(cell);
  }

  if (param_span_set_top((PyObject *) self) < 0)
    goto fail;
  kos_param_spans_open++;

  Py_XSETREF(self->own, own);
  Py_XSETREF(self->table, table);
  Py_XSETREF(self->prev, (PyObject *) top);
  self->base_serial = tls_active_serial(s);
  self->entered = 1;
  Py_INCREF(self);
  return (PyObject *) self;

 fail:
  Py_XDECREF(top);
  Py_XDECREF(table);
  Py_XDECREF(own);
  Py_CLEAR(self->base);
  return NULL;
}


/**
   Leave the span. In order, the top of the chain moves to the span
   beneath. Out of order on its own stack, the span is unlinked from
   beneath the spans above it and their tables are rebuilt without its
   cells. A span that is not in the running stack's chain was entered
   on another task and is refused.
*/
static PyObject *param_span_exit(KosParamSpan *self, PyObject *args) {
  PyObject *exc_type = NULL;
  PyObject *exc_val = NULL;
  PyObject *exc_tb = NULL;
  KosParamSpan *top;
  KosParamSpan *above;
  PyObject *chain;
  Py_ssize_t i;

  if (! PyArg_UnpackTuple(args, "__exit__", 3, 3,
                          &exc_type, &exc_val, &exc_tb))
    return NULL;
  if (! self->entered) {
    PyErr_SetString(PyExc_RuntimeError, "parameterize block not entered");
    return NULL;
  }
  if (! param_span_var) {
    PyErr_SetString(PyExc_RuntimeError,
                    "parameterize block exited after module teardown");
    return NULL;
  }

  top = param_span_top();
  if (top == self) {
    Py_DECREF(top);
    if (param_span_set_top(self->prev) < 0)
      return NULL;
    Py_CLEAR(self->prev);
    self->entered = 0;
    kos_param_spans_open--;
    Py_RETURN_NONE;
  }

  chain = PyList_New(0);
  if (! chain) {
    Py_XDECREF(top);
    return NULL;
  }
  above = top;
  while (above && (KosParamSpan *) above->prev != self) {
    if (PyList_Append(chain, (PyObject *) above) < 0) {
      Py_XDECREF(top);
      Py_DECREF(chain);
      return NULL;
    }
    above = (KosParamSpan *) above->prev;
  }
  Py_XDECREF(top);
  if (! above) {
    Py_DECREF(chain);
    PyErr_SetString(PyExc_RuntimeError,
                    "parameterize block exited on a stack it was not"
                    " entered on");
    return NULL;
  }
  if (PyList_Append(chain, (PyObject *) above) < 0) {
    Py_DECREF(chain);
    return NULL;
  }

  /* above was built over this span; it now sits on what this span sat
     on, taking the context table beneath when it had none of its own */
  Py_XINCREF(self->prev);
  Py_XSETREF(above->prev, self->prev);
  if (self->base && ! above->base) {
    Py_INCREF(self->base);
    above->base = self->base;
  }
  Py_CLEAR(self->prev);
  self->entered = 0;
  kos_param_spans_open--;

  for (i = PyList_GET_SIZE(chain); i--; ) {
    if (param_span_rebuild((KosParamSpan *) PyList_GET_ITEM(chain, i)) < 0) {
      Py_DECREF(chain);
      return NULL;
    }
  }
  Py_DECREF(chain);
  Py_RETURN_NONE;
}


static PyObject *param_span_tp_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  PyObject *mapping = NULL;
  PyObject *given = NULL;
  PyObject *bindings = NULL;
  KosParamSpan *self;
  PyObject *key;
  PyObject *value;
  Py_ssize_t pos = 0;

  if (kwds && PyDict_GET_SIZE(kwds)) {
    PyErr_SetString(PyExc_TypeError,
                    "Parameterize takes no named arguments");
    return NULL;
  }
  if (! PyArg_ParseTuple(args, "O:Parameterize", &mapping))
    return NULL;

  given = PyDict_New();
  if (! given)
    return NULL;
  if (PyDict_Update(given, mapping) < 0)
    goto fail;
  bindings = PyDict_New();
  if (! bindings)
    goto fail;

  while (PyDict_Next(given, &pos, &key, &value)) {
    PyObject *name = kos_key_to_str(key);
    if (! name)
      goto fail;
    if (PyDict_SetItem(bindings, name, value) < 0) {
      Py_DECREF(name);
      goto fail;
    }
    Py_DECREF(name);
  }
  Py_DECREF(given);

  self = (KosParamSpan *) type->tp_alloc(type, 0);
  if (! self) {
    Py_DECREF(bindings);
    return NULL;
  }
  self->bindings = bindings;
  return (PyObject *) self;

 fail:
  Py_DECREF(given);
  Py_XDECREF(bindings);
  return NULL;
}


static int param_span_traverse(PyObject *self, visitproc visit, void *arg) {
  KosParamSpan *span = (KosParamSpan *) self;
  Py_VISIT(span->bindings);
  Py_VISIT(span->own);
  Py_VISIT(span->table);
  Py_VISIT(span->base);
  Py_VISIT(span->prev);
  return 0;
}


static int param_span_clear(PyObject *self) {
  KosParamSpan *span = (KosParamSpan *) self;
  Py_CLEAR(span->bindings);
  Py_CLEAR(span->own);
  Py_CLEAR(span->table);
  Py_CLEAR(span->base);
  Py_CLEAR(span->prev);
  return 0;
}


static void param_span_dealloc(PyObject *self) {
  PyObject_GC_UnTrack(self);
  param_span_clear(self);
  Py_TYPE(self)->tp_free(self);
}


static PyMethodDef param_span_methods[] = {
  {"__enter__", (PyCFunction) param_span_enter, METH_NOARGS,
   "Bind the cells for the block; returns the span"},
  {"__exit__", (PyCFunction) param_span_exit, METH_VARARGS,
   "Unbind the block's cells"},
  {NULL, NULL, 0, NULL},
};


PyTypeObject KosParamSpanType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.Parameterize",
  .tp_basicsize = sizeof(KosParamSpan),
  .tp_itemsize = 0,
  .tp_new = param_span_tp_new,
  .tp_dealloc = param_span_dealloc,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_traverse = param_span_traverse,
  .tp_clear = param_span_clear,
  .tp_methods = param_span_methods,
  .tp_doc = "Parameterize(bindings): a context manager binding the"
            " named parameters for the extent of a Python block",
};


/* === parameter resolution === */


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
  PyObject *table;
  PyObject *cell;

  if (! s)
    return NULL;

  ctx = kos_tls_active_context(s);
  table = kos_param_ambient_cells(s, ctx);
  if (table) {
    cell = PyDict_GetItem(table, key);
    if (cell)
      return cell;
  }

  if (ctx)
    return root_probe(KosContext_DOMAIN(ctx), key);

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
  if (domain_scope_enter(self) < 0)
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
  if (domain_scope_exit(self) < 0)
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


/* The running stack of the calling thread, every domain: a list of
   the Context objects innermost first, empty when nothing runs. */
static PyObject *domain_get_stack(PyObject *cls, PyObject *unused) {

  KosTlsState *s = tls_state_current();
  PyObject *out = PyList_New(0);

  (void) cls;
  (void) unused;
  if (! out)
    return NULL;
  if (! s)
    return out;

  for (size_t i = s->ctx_depth; i--; ) {
    KosContext *ctx = s->ctx_stack[i];
    if (! ctx)
      continue;
    if (PyList_Append(out, (PyObject *) ctx) < 0) {
      Py_DECREF(out);
      return NULL;
    }
  }

  return out;
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
  {"get_stack", (PyCFunction) domain_get_stack,
   METH_CLASS | METH_NOARGS,
   "Return the Context objects running on this thread under any Domain,"
   " innermost first; Context.snapshot() pictures each one with its"
   " dynamic chain"},
  {"get_contexts", (PyCFunction) domain_get_contexts, METH_NOARGS,
   "Return the Context objects running on this thread under this Domain,"
   " innermost first; Context.snapshot() pictures each one"},
  {"param_scopes", (PyCFunction) domain_param_scopes, METH_NOARGS,
   "Return {'shadow': {name: cell}, 'root': {name: cell}} for this Domain"},
  {"__enter__", (PyCFunction) domain_enter, METH_NOARGS,
   "Open a block on the running stack that this Domain's root answers in"},
  {"__exit__", (PyCFunction) domain_exit, METH_VARARGS,
   "Close the block; refused on a stack that did not open it"},
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
  .tp_doc = "Domain(): the owner of one root parameter scope. Each thread"
            " has a default Domain of its own; a with-Domain block supplies"
            " another for its extent, and a bare parameter store made"
            " inside it is read by every later call in the block and by"
            " no other.",
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

  if (PyType_Ready(&KosParamSpanType) < 0)
    return -1;
  Py_INCREF(&KosParamSpanType);
  if (PyModule_AddObject(mod, "Parameterize",
                         (PyObject *) &KosParamSpanType) < 0) {
    Py_DECREF(&KosParamSpanType);
    return -1;
  }
  param_span_var = PyContextVar_New("koskari.parameterize", Py_None);
  if (! param_span_var)
    return -1;

  if (PyType_Ready(&KosDomainScopeType) < 0)
    return -1;
  domain_scope_var = PyContextVar_New("koskari.domain", Py_None);
  if (! domain_scope_var)
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
  Py_CLEAR(param_span_var);
  Py_CLEAR(domain_scope_var);
  return 0;
}


// The end.
