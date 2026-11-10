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
  size_t ctx_depth;
  size_t ctx_cap;
  /* the thread's default domain (a reference of our own), minted on
     first need: the domain answering when no context is active and no
     `with Domain():` block is open. Only evals entered on this thread
     run under it. */
  PyObject *default_domain;
};

/* the calling OS thread's record, a C thread-local slot: one TLS load
   on the fast path, checked against the current thread state by its
   owner field since a thread state can change on one OS thread. The
   initial-exec model keeps that load off __tls_get_addr. */
static _Thread_local KosTlsState *tls_state_slot KOS_TLS_INITIAL_EXEC;


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
   cleared at interpreter shutdown, by an embedder, or in a forked
   child), which cannot reach this thread's slot: the default domain
   is dropped there, and the record lives on the slot's count, with
   owner cleared, until this thread next enters and releases it. */
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
    s->owner = NULL;
    tls_state_release(s);
    return;
  }

  /* another thread: the owner may never enter again (a forked child
     has no such thread), so the default domain goes now, with the
     capsule's count held across the drop. A finalizer it runs on this
     thread finds this thread's own record, never this one. */
  s->owner = NULL;
  Py_CLEAR(s->default_domain);
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


/* Push ctx as the active context. */
static void tls_ctx_push(KosTlsState *s, KosContext *ctx) {
  size_t depth = s->ctx_depth;

  s->ctx_stack = (KosContext **) tls_stack_reserve(
      s->ctx_stack, &s->ctx_cap, depth);
  s->ctx_stack[depth] = ctx;
  s->ctx_depth = depth + 1;
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


size_t kos_tls_active_depth(const KosTlsState *s) {
  return s->ctx_depth;
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

   base_ctx keys the block to the Koskari context active when it was
   opened: the block is inner to that context (opened by Python code
   the context called out to) and governs every read and eval made
   while that context is the active one, however many times the
   context is entered; a context entered before it keeps the domain it
   took at its own entry. A block opened with no context active is
   inner to none.

   A block is never changed once published: a task created inside it
   holds it through its own copy of the chain. An in-order exit resets
   the var through the enter's token, which CPython refuses from a
   Context other than the one that set it, so a block is exited only
   on the stack that entered it; an out-of-order exit rebuilds the
   blocks above as new blocks over the one beneath.
*/
typedef struct {
  PyObject_HEAD
  PyObject *domain;     /* the KosDomain the block supplies */
  PyObject *prev;       /* the block beneath on this stack, or NULL */
  PyObject *base_ctx;   /* the context active at enter, or NULL */
  PyObject *token;      /* the enter's ContextVar token */
} KosDomainScope;


/* The top block of the running stack, None when no block is open. */
static PyObject *domain_scope_var = NULL;

/* Set once the first block is entered in the process and never
   cleared: before that the ambient lookup skips the ContextVar read,
   which keeps the eval entry free of it; after it a chain may be open
   on any task, so the var is read. */
static int domain_scopes_used = 0;


static int domain_scope_traverse(PyObject *self, visitproc visit, void *arg) {
  KosDomainScope *scope = (KosDomainScope *) self;
  Py_VISIT(scope->domain);
  Py_VISIT(scope->prev);
  Py_VISIT(scope->base_ctx);
  Py_VISIT(scope->token);
  return 0;
}


static int domain_scope_clear(PyObject *self) {
  KosDomainScope *scope = (KosDomainScope *) self;
  Py_CLEAR(scope->domain);
  Py_CLEAR(scope->prev);
  Py_CLEAR(scope->base_ctx);
  Py_CLEAR(scope->token);
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

  if (! domain_scopes_used || ! domain_scope_var)
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


/* Publish scope as the top block; the token is the caller's. */
static PyObject *domain_scope_set_top(PyObject *scope) {
  return PyContextVar_Set(domain_scope_var, scope ? scope : Py_None);
}


/* Take back the block through its enter token, which restores the
   top the enter displaced. CPython refuses a token made in another
   Context: that is a block exited on a stack it was not entered on. */
static int domain_scope_reset(KosDomainScope *scope) {
  if (PyContextVar_Reset(domain_scope_var, scope->token) < 0) {
    if (PyErr_ExceptionMatches(PyExc_ValueError)) {
      PyErr_Clear();
      PyErr_SetString(PyExc_RuntimeError,
                      "Domain block exited on a stack it was not entered on");
    }
    return -1;
  }
  return 0;
}


/* A new block over prev with the same domain and context key. */
static KosDomainScope *domain_scope_new(
    PyObject *domain,
    PyObject *prev,
    PyObject *base_ctx) {

  KosDomainScope *scope = PyObject_GC_New(KosDomainScope, &KosDomainScopeType);

  if (! scope)
    return NULL;
  scope->domain = Py_NewRef(domain);
  scope->prev = Py_XNewRef(prev);
  scope->base_ctx = Py_XNewRef(base_ctx);
  scope->token = NULL;
  PyObject_GC_Track(scope);
  return scope;
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

  if (top && top->base_ctx == (PyObject *) c)
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
   NULL with an error set. Through Python 3.11 the allocation can run a
   collection whose finalizer mints the default on this thread first;
   that one is kept and the new one dropped. */
static PyObject *tls_default_domain(KosTlsState *s) {

  PyObject *domain;

  if (s->default_domain)
    return s->default_domain;

  domain = (PyObject *) KosDomain_New();
  if (! domain)
    return NULL;
  if (s->default_domain)
    Py_DECREF(domain);
  else
    s->default_domain = domain;
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
  {
    KosDomainScope *top = domain_scope_top();

    scope = domain_scope_new(domain, (PyObject *) top,
                             (PyObject *) kos_tls_active_context(s));
    Py_XDECREF(top);
  }
  if (! scope)
    return -1;
  domain_scopes_used = 1;
  {
    PyObject *token = domain_scope_set_top((PyObject *) scope);

    scope->token = token;
    Py_DECREF(scope);   /* the var keeps it */
    return token ? 0 : -1;
  }
}


/**
   Close the innermost open block supplying domain. In order, the enter
   token takes the chain back to the block beneath. Out of order, the
   block's token proves the stack and takes the chain back to what sat
   beneath the block, and the blocks above are published again as new
   blocks over it, each with a token of its own; the blocks a task
   created inside them holds are left as they were. A block that is
   not in the running stack's chain was entered on another task and is
   refused.
*/
static int domain_scope_exit(PyObject *domain) {
  KosDomainScope *top;
  KosDomainScope *scope;
  PyObject *above;
  Py_ssize_t i;

  if (! domain_scope_var) {
    PyErr_SetString(PyExc_RuntimeError,
                    "Domain block exited after module teardown");
    return -1;
  }
  top = domain_scope_top();
  if (top && top->domain == domain) {
    int res = domain_scope_reset(top);
    Py_DECREF(top);
    return res;
  }

  above = PyList_New(0);
  if (! above) {
    Py_XDECREF(top);
    return -1;
  }
  scope = top;
  while (scope && scope->domain != domain) {
    if (PyList_Append(above, (PyObject *) scope) < 0) {
      Py_XDECREF(top);
      Py_DECREF(above);
      return -1;
    }
    scope = (KosDomainScope *) scope->prev;
  }
  Py_XDECREF(top);
  if (! scope) {
    Py_DECREF(above);
    PyErr_SetString(PyExc_RuntimeError,
                    "Domain block exited on a stack it was not entered on");
    return -1;
  }
  if (domain_scope_reset(scope) < 0) {
    Py_DECREF(above);
    return -1;
  }

  /* innermost last: each new block sits on the one published before it */
  {
    PyObject *prev = scope->prev;

    for (i = PyList_GET_SIZE(above); i--; ) {
      KosDomainScope *old = (KosDomainScope *) PyList_GET_ITEM(above, i);
      KosDomainScope *fresh = domain_scope_new(old->domain, prev,
                                               old->base_ctx);

      if (! fresh) {
        Py_DECREF(above);
        return -1;
      }
      {
        PyObject *token = domain_scope_set_top((PyObject *) fresh);

        fresh->token = token;
        prev = (PyObject *) fresh;
        Py_DECREF(fresh);   /* the var keeps it */
        if (! token) {
          Py_DECREF(above);
          return -1;
        }
      }
    }
  }
  Py_DECREF(above);
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
    kos_param_tables_used = 1;
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

   base_ctx keys the span to the Koskari context active when it was
   opened: the span is inner to that context (opened by Python code
   the context called out to) and outer to every other, however many
   times that context is entered. A context entered inside the span
   takes the span's table at creation and reads its own table from
   then on.

   The user's handle (koskari.lib.Parameterize) and the span on the
   chain are two objects. The span is never changed once published: a
   task created inside it holds it through its own copy of the chain,
   and its table is kept after exit, as the rule for creation-time
   inheritance requires. An in-order exit resets the var through the
   enter's token, which CPython refuses from a Context other than the
   one that set it, so a span is exited only on the stack that entered
   it; an out-of-order exit rebuilds the spans above as new spans over
   the one beneath, each handle moving to its new span.
*/
typedef struct {
  PyObject_HEAD
  PyObject *handle;     /* the Parameterize entered with this span */
  PyObject *own;        /* dict str -> cell: this entry's cells */
  PyObject *table;      /* dict str -> cell: the view at entry plus own */
  PyObject *base;       /* the active context's table the span was
                           built over, when the span beneath was not
                           inner to the same context; NULL otherwise */
  PyObject *prev;       /* the span beneath on this stack, or NULL */
  PyObject *base_ctx;   /* the context active at enter, or NULL */
} KosParamNode;

typedef struct {
  PyObject_HEAD
  PyObject *bindings;   /* dict str -> value, from construction */
  PyObject *node;       /* the span on the chain while entered */
  PyObject *token;      /* the enter's ContextVar token while entered */
} KosParamSpan;


/* The top span of the running stack, None when no span is open. */
static PyObject *param_span_var = NULL;

/* Set once the first span is entered in the process and never
   cleared: before that kos_param_ambient_cells skips the chain, which
   keeps the boundary entry free of the ContextVar read; after it a
   chain may be open on any task, so the var is read. */
int kos_param_spans_used = 0;

/* Set once the first context table is made in the process and never
   cleared: a snapshot taken before that has nothing to find, so a
   coroutine's creation skips resolving the thread for it. */
int kos_param_tables_used = 0;


static int param_node_traverse(PyObject *self, visitproc visit, void *arg) {
  KosParamNode *node = (KosParamNode *) self;
  Py_VISIT(node->handle);
  Py_VISIT(node->own);
  Py_VISIT(node->table);
  Py_VISIT(node->base);
  Py_VISIT(node->prev);
  Py_VISIT(node->base_ctx);
  return 0;
}


static int param_node_clear(PyObject *self) {
  KosParamNode *node = (KosParamNode *) self;
  Py_CLEAR(node->handle);
  Py_CLEAR(node->own);
  Py_CLEAR(node->table);
  Py_CLEAR(node->base);
  Py_CLEAR(node->prev);
  Py_CLEAR(node->base_ctx);
  return 0;
}


static void param_node_dealloc(PyObject *self) {
  PyObject_GC_UnTrack(self);
  param_node_clear(self);
  Py_TYPE(self)->tp_free(self);
}


static PyTypeObject KosParamNodeType = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = "koskari.lib.ParameterizeSpan",
  .tp_basicsize = sizeof(KosParamNode),
  .tp_itemsize = 0,
  .tp_dealloc = param_node_dealloc,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_traverse = param_node_traverse,
  .tp_clear = param_node_clear,
  .tp_doc = "One open parameterize block on the running stack",
};


/* The top span as a new reference, or NULL with no error when none. */
static KosParamNode *param_node_top(void) {
  PyObject *top = NULL;

  if (! kos_param_spans_used || ! param_span_var)
    return NULL;
  if (PyContextVar_Get(param_span_var, NULL, &top) < 0) {
    PyErr_Clear();
    return NULL;
  }
  if (top == Py_None) {
    Py_DECREF(top);
    return NULL;
  }
  return (KosParamNode *) top;
}


/* Publish node as the top span; the token is the caller's. */
static PyObject *param_node_set_top(PyObject *node) {
  return PyContextVar_Set(param_span_var, node ? node : Py_None);
}


/* Take back a span through its enter token, which restores the top
   the enter displaced. CPython refuses a token made in another
   Context: that is a span exited on a stack it was not entered on. */
static int param_node_reset(PyObject *token) {
  if (PyContextVar_Reset(param_span_var, token) < 0) {
    if (PyErr_ExceptionMatches(PyExc_ValueError)) {
      PyErr_Clear();
      PyErr_SetString(PyExc_RuntimeError,
                      "parameterize block exited on a stack it was not"
                      " entered on");
    }
    return -1;
  }
  return 0;
}


/**
   A new span over prev for handle, keyed to base_ctx: its table is
   the view beneath -- prev's table when prev is inner to the same
   context, else base, the active context's table or NULL -- plus own.
   base is kept on the span only when it stood in for prev's table.
*/
static KosParamNode *param_node_new(
    PyObject *handle,
    PyObject *own,
    PyObject *base,
    KosParamNode *prev,
    PyObject *base_ctx) {

  KosParamNode *node;
  PyObject *view;
  PyObject *table;
  int chained = prev && prev->base_ctx == base_ctx;

  view = chained ? prev->table : base;
  table = view ? PyDict_Copy(view) : PyDict_New();
  if (! table)
    return NULL;
  if (PyDict_Update(table, own) < 0) {
    Py_DECREF(table);
    return NULL;
  }

  node = PyObject_GC_New(KosParamNode, &KosParamNodeType);
  if (! node) {
    Py_DECREF(table);
    return NULL;
  }
  node->handle = Py_NewRef(handle);
  node->own = Py_NewRef(own);
  node->table = table;
  node->base = chained ? NULL : Py_XNewRef(base);
  node->prev = Py_XNewRef((PyObject *) prev);
  node->base_ctx = Py_XNewRef(base_ctx);
  PyObject_GC_Track(node);
  return node;
}


PyObject *kos_param_span_cells(KosTlsState *s) {
  KosParamNode *top = param_node_top();
  PyObject *table = NULL;

  if (top) {
    if (top->base_ctx == (PyObject *) kos_tls_active_context(s))
      table = top->table;
    Py_DECREF(top);   /* the var keeps it */
  }
  return table;
}


static PyObject *param_span_enter(KosParamSpan *self, PyObject *unused) {
  KosTlsState *s;
  KosContext *active;
  KosParamNode *top;
  KosParamNode *node;
  PyObject *own;
  PyObject *token;
  PyObject *key;
  PyObject *value;
  Py_ssize_t pos = 0;

  (void) unused;
  if (self->node) {
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

  own = PyDict_New();
  if (! own)
    return NULL;
  while (PyDict_Next(self->bindings, &pos, &key, &value)) {
    PyObject *cell = KosSymbolicRef_New(key, value);
    if (! cell || PyDict_SetItem(own, key, cell) < 0) {
      Py_XDECREF(cell);
      Py_DECREF(own);
      return NULL;
    }
    Py_DECREF(cell);
  }

  top = param_node_top();
  node = param_node_new((PyObject *) self, own,
                        active ? KosContext_PARAM_REFS(active) : NULL,
                        top, (PyObject *) active);
  Py_XDECREF(top);
  Py_DECREF(own);
  if (! node)
    return NULL;

  kos_param_spans_used = 1;
  token = param_node_set_top((PyObject *) node);
  if (! token) {
    Py_DECREF(node);
    return NULL;
  }
  self->node = (PyObject *) node;
  self->token = token;
  Py_INCREF(self);
  return (PyObject *) self;
}


/**
   Leave the span. In order, the enter token takes the chain back to
   the span beneath. Out of order, the token proves the stack and takes
   the chain back to what sat beneath the span, and the spans above are
   published again as new spans over it, their tables rebuilt without
   this span's cells and each handle moved to its new span; the spans a
   task created inside them holds are left as they were. A span that is
   not in the running stack's chain was entered on another task and is
   refused.
*/
static PyObject *param_span_exit(KosParamSpan *self, PyObject *args) {
  PyObject *exc_type = NULL;
  PyObject *exc_val = NULL;
  PyObject *exc_tb = NULL;
  KosParamNode *top;
  KosParamNode *node;
  PyObject *above;
  Py_ssize_t i;

  if (! PyArg_UnpackTuple(args, "__exit__", 3, 3,
                          &exc_type, &exc_val, &exc_tb))
    return NULL;
  if (! self->node) {
    PyErr_SetString(PyExc_RuntimeError, "parameterize block not entered");
    return NULL;
  }
  if (! param_span_var) {
    PyErr_SetString(PyExc_RuntimeError,
                    "parameterize block exited after module teardown");
    return NULL;
  }

  top = param_node_top();
  if ((PyObject *) top == self->node) {
    Py_DECREF(top);
    if (param_node_reset(self->token) < 0)
      return NULL;
    Py_CLEAR(self->node);
    Py_CLEAR(self->token);
    Py_RETURN_NONE;
  }

  above = PyList_New(0);
  if (! above) {
    Py_XDECREF(top);
    return NULL;
  }
  node = top;
  while (node && (PyObject *) node != self->node) {
    if (PyList_Append(above, (PyObject *) node) < 0) {
      Py_XDECREF(top);
      Py_DECREF(above);
      return NULL;
    }
    node = (KosParamNode *) node->prev;
  }
  Py_XDECREF(top);
  if (! node) {
    Py_DECREF(above);
    PyErr_SetString(PyExc_RuntimeError,
                    "parameterize block exited on a stack it was not"
                    " entered on");
    return NULL;
  }
  if (param_node_reset(self->token) < 0) {
    Py_DECREF(above);
    return NULL;
  }

  /* innermost last: each new span sits on the one published before
     it. The nearest span above takes the context table this span was
     built over when it had none of its own. */
  {
    KosParamNode *prev = (KosParamNode *) node->prev;
    PyObject *carried = node->base;

    for (i = PyList_GET_SIZE(above); i--; ) {
      KosParamNode *old = (KosParamNode *) PyList_GET_ITEM(above, i);
      KosParamSpan *handle = (KosParamSpan *) old->handle;
      KosParamNode *fresh = param_node_new(
          old->handle, old->own, old->base ? old->base : carried,
          prev, old->base_ctx);
      PyObject *token;

      carried = NULL;
      if (! fresh) {
        Py_DECREF(above);
        return NULL;
      }
      token = param_node_set_top((PyObject *) fresh);
      if (! token) {
        Py_DECREF(fresh);
        Py_DECREF(above);
        return NULL;
      }
      Py_XSETREF(handle->node, (PyObject *) fresh);
      Py_XSETREF(handle->token, token);
      prev = fresh;
    }
  }
  Py_DECREF(above);
  Py_CLEAR(self->node);
  Py_CLEAR(self->token);
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
  Py_VISIT(span->node);
  Py_VISIT(span->token);
  return 0;
}


static int param_span_clear(PyObject *self) {
  KosParamSpan *span = (KosParamSpan *) self;
  Py_CLEAR(span->bindings);
  Py_CLEAR(span->node);
  Py_CLEAR(span->token);
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

  /* the root scope of the domain in force on the thread, the one an
     eval entered here would run under: a block opened inside the
     active context's callout wins over the context's own domain. A
     thread default not minted yet has nothing bound. */
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
  PyObject *domain_obj;
  PyObject *root;

  /* the domain in force on the thread, as the lookup resolves it */
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


/* === koskari-vars === */


/**
  The koskari_vars callable. It is a KosCallable, so a call from
  Koskari runs its install with the caller's frame still current, in
  tail position too, and reads that frame's namespace. A call from
  Python reads the namespace of the frame running in the thread's
  active context. With an object, either answers the
  object's __dict__, as Python's vars does.
*/
typedef struct KosVarsCallable {
  PyObject_HEAD
  vectorcallfunc vectorcall;
} KosVarsCallable;


static int vars_check_args(Py_ssize_t nargs, PyObject *kwnames) {
  if (kwnames && PyTuple_GET_SIZE(kwnames)) {
    PyErr_SetString(PyExc_TypeError,
                    "koskari-vars() takes no keyword arguments");
    return -1;
  }

  if (nargs > 1) {
    PyErr_Format(PyExc_TypeError,
                 "koskari-vars() takes at most 1 argument (%zd given)",
                 nargs);
    return -1;
  }

  return 0;
}


static PyObject *vars_of(PyObject *obj) {
  PyObject *dict = PyObject_GetAttrString(obj, "__dict__");
  if (! dict && PyErr_ExceptionMatches(PyExc_AttributeError)) {
    PyErr_Clear();
    PyErr_SetString(PyExc_TypeError,
                    "koskari-vars() argument must have __dict__ attribute");
  }
  return dict;
}


static PyObject *vars_of_eval(KosContext *ctx) {
  if (! ctx || ! KosContext_NAMESPACE(ctx)) {
    PyErr_SetString(PyExc_RuntimeError,
                    "koskari-vars() without an argument outside a running"
                    " evaluation");
    return NULL;
  }
  return KosContext_RevealNamespace(ctx);
}


static KosOpResult vars_kos_vinstall(
    KosContext *ctx,
    PyObject *callable,
    PyObject *const *args,
    Py_ssize_t nargs,
    PyObject *kwnames,
    int tail_call) {

  (void) callable;
  (void) tail_call;

  if (vars_check_args(nargs, kwnames) < 0)
    return EVAL_RAISE();

  PyObject *result = nargs ? vars_of(args[0]) : vars_of_eval(ctx);
  return result ? EVAL_RETURN(result) : EVAL_RAISE();
}


static PyObject *vars_vectorcall(
    PyObject *callable,
    PyObject *const *args,
    size_t nargsf,
    PyObject *kwnames) {

  Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);

  (void) callable;
  if (vars_check_args(nargs, kwnames) < 0)
    return NULL;

  if (nargs)
    return vars_of(args[0]);
  return vars_of_eval(kos_ctx_current());
}


static PyObject *vars_repr(PyObject *self) {
  (void) self;
  return PyUnicode_FromString("<built-in function koskari-vars>");
}


static KosEvaluatedTypeObject KosVarsCallableType = {
  .type = {
  PyVarObject_HEAD_INIT(NULL, 0)

  .tp_name = "koskari.lib.koskari_vars",
  .tp_basicsize = sizeof(KosVarsCallable),
  .tp_itemsize = 0,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_VECTORCALL,
  .tp_doc = "koskari_vars(obj=...) => dict or namespace\n"
  "With an object, its __dict__. With no argument, the namespace of the\n"
  "calling frame.",
  .tp_call = PyVectorcall_Call,
  .tp_vectorcall_offset = offsetof(KosVarsCallable, vectorcall),
  .tp_repr = vars_repr,
  .tp_base = KOS_EVALUATED_PYTYPE(KosEvaluatedType),
  },
  .kos_call = {
  .kc_vinstall = vars_kos_vinstall,
  .kc_pycall = NULL,
  .kc_pyvectorcall = vars_vectorcall,
  },
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

  if (PyType_Ready(&KosParamNodeType) < 0)
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

  if (PyType_Ready(KOS_EVALUATED_PYTYPE(KosVarsCallableType)) < 0)
    return -1;

  KosVarsCallable *kvars = PyObject_New(
      KosVarsCallable, KOS_EVALUATED_PYTYPE(KosVarsCallableType));
  if (! kvars)
    return -1;

  kvars->vectorcall = vars_vectorcall;
  if (PyModule_AddObject(mod, "koskari_vars", (PyObject *) kvars) < 0) {
    Py_DECREF(kvars);
    return -1;
  }

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
