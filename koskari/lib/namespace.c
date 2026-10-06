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
   koskari.lib - namespace module

   Namespace types for Koskari

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "lib.h"
#include "debug.h"
#include "hints.h"
#include "tracing.h"


/* === String constants === */


static PyObject *_namespace_str = NULL;  // "<namespace>"
static PyObject *str_class_name = NULL;  // "__class__", interned
static PyObject *_module_str = NULL;     // "<module>"

/* The module dunders CPython reads from a module's globals with the raw
   dict API (__name__ for a function's __module__, __package__ and
   __spec__ for a relative import, __file__ and __loader__ for
   tracebacks and linecache). A module namespace mirrors their values
   into its dict storage so those raw reads see them. */
static PyObject *mdl_mirrored_names = NULL;   // tuple of str


// forward declarations
static PyObject *mdl_ns_borrow_ref(PyObject *self, PyObject *name);
static PyObject *mdl_ns_builtin(PyObject *self, PyObject *name);


/* The empty namespace is a static object with no GC header in front
   of it, the way the nil singletons are: never allocated, never freed,
   and never visited by the collector (ns_is_gc answers no for it). Its
   name, binds and names tuple are made at unit init and released at
   unit clear. */
static KosNamespace ns_empty_object = {
  { .head = { PyVarObject_HEAD_INIT(&KosNamespaceType, 0) 0, 0, NULL, NULL } },
  { NULL, NULL, NULL, NULL, { 0, 0 } },
};

#define NS_EMPTY ((PyObject *) &ns_empty_object)
static int ns_fast_index(PyObject *self, PyObject *key);
static PyObject *ns_newfull_from_nspec(
    PyTypeObject *type, PyObject *parent, PyObject *nspec);


/* === Namespace freelist === */


/* Freelist head; cached slabs are linked via KosNamespace_PARENT. */
static KosNamespace *ns_freelist = NULL;
static size_t ns_freecount = 0;


/* Allocate a KosNamespace-family object of the given type and slot count.
   For KosNamespaceType with count <= KOS_NS_MIN_SLOTS: pops a pre-zeroed slab
   from the freelist (hit) or allocates oversized to KOS_NS_MIN_SLOTS (miss).
   All fast[] slots are guaranteed NULL on return; all header fields except
   parent are NULL/zero. The caller must initialise all header fields.

   Allocation is GC-capable but the object is born untracked; see
   KosNamespace_TrackChain for when a namespace is revealed to the
   collector. Freelist slabs are always untracked (dealloc untracks
   before recycling). */
/* Zero the raw-flag words and point the overflow words at the trailing
   area past the slots (which the caller sized for them), or NULL. */
static void ns_init_raw_storage(KosNamespace *ns, size_t count) {
  size_t words = kos_ns_raw_more_words(count);

  KosNamespace_FAST_RAW(ns)[0] = 0;
  KosNamespace_FAST_RAW(ns)[1] = 0;
  KosNamespace_FAST_RAW_MORE(ns) = words ? (uint64_t *) &ns->fast[count] : NULL;
  if (words)
    memset(KosNamespace_FAST_RAW_MORE(ns), 0, words * sizeof(uint64_t));
}


static PyObject *ns_alloc(PyTypeObject *type, size_t count) {

  if (type != &KosNamespaceType || count > (size_t) KOS_NS_MIN_SLOTS) {
    /* The overflow raw-flag words ride after the slots in the same
       allocation; tp_itemsize is one pointer, the size of a word. */
    size_t items = count + kos_ns_raw_more_words(count);
    PyObject *p = PyObject_GC_NewVar(PyObject, type, (Py_ssize_t) items);
    if (! p)
      return NULL;

    if (count > 0)
      memset(((KosNamespace *) p)->fast, 0, count * sizeof(PyObject *));
    ns_init_raw_storage((KosNamespace *) p, count);
    KosNamespace_TEMPLATE(p) = NULL;

    return p;
  }

  if (ns_freelist) {
    KosNamespace *ns = ns_freelist;
    ns_freelist = (KosNamespace *) KosNamespace_PARENT(ns);
    ns_freecount--;

    KosNamespace_PARENT(ns) = NULL;
    KosNamespace_TEMPLATE(ns) = NULL;
    KOS_TRACE_FL_ALLOC("namespace", 1);

    PyObject *p = (PyObject *) ns;
    /* the revival CPython's own type freelists use: a refcount of one,
       and in a debug build the reference total and trace chain the
       dealloc gave up */
    _Py_NewReference(p);
    ns_init_raw_storage(ns, count);
    // Py_SET_TYPE(p, type);
    // Py_SET_SIZE(p, (Py_ssize_t) KOS_NS_MIN_SLOTS);
    return p;
  }

  KOS_TRACE_FL_ALLOC("namespace", 0);
  PyObject *p = PyObject_GC_NewVar(PyObject, type,
                                   (Py_ssize_t) KOS_NS_MIN_SLOTS);
  if (! p)
    return NULL;

  memset(((KosNamespace *) p)->fast, 0,
         (size_t) KOS_NS_MIN_SLOTS * sizeof(PyObject *));
  ns_init_raw_storage((KosNamespace *) p, count);
  KosNamespace_TEMPLATE(p) = NULL;
  return p;
}


/* Copy the raw-flag bits for count slots from one holder to another:
   the two inline words, then the overflow words when count needs them. */
static void ns_copy_raw_bits(
    uint64_t *dst, uint64_t *dst_more,
    const uint64_t *src, const uint64_t *src_more,
    size_t count) {

  dst[0] = src[0];
  dst[1] = src[1];
  size_t words = kos_ns_raw_more_words(count);
  if (words)
    memcpy(dst_more, src_more, words * sizeof(uint64_t));
}


void KosNamespace_TrackChain(PyObject *ns) {
  /* the empty namespace is static and has no GC header to track */
  while (likely(KosNamespace_Check(ns)) && PyObject_IS_GC(ns)
         && ! PyObject_GC_IsTracked(ns)) {
    PyObject_GC_Track(ns);
    ns = (PyObject *) KosNamespace_PARENT(ns);
  }
}


int KosNamespace_TraverseUntracked(PyObject *ns, visitproc visit, void *arg) {

  if (ns && KosNamespace_Check(ns) && ! PyObject_GC_IsTracked(ns))
    return Py_TYPE(ns)->tp_traverse(ns, visit, arg);
  return 0;
}


PyObject *KosNamespace_Reveal(PyObject *ns) {
  if (likely(ns)) {
    KosNamespace_TrackChain(ns);
    Py_INCREF(ns);
  }
  return ns;
}


/* === NamespaceSpec === */


static PyMemberDef nspec_members[] = {
  { "name", T_OBJECT,
    offsetof(KosNamespaceSpec, ns_name), READONLY,
    " " },

  { "local_syms", T_OBJECT,
    offsetof(KosNamespaceSpec, local_syms), READONLY,
    " " },

  { "closure_syms", T_OBJECT,
    offsetof(KosNamespaceSpec, closure_syms), READONLY,
    " " },

  { "global_syms", T_OBJECT,
    offsetof(KosNamespaceSpec, global_syms), READONLY,
    " " },

  { NULL },
};


PyObject *KosNamespace_FromSpec(PyObject *nspec, PyObject *parent) {
  return ns_newfull_from_nspec(&KosNamespaceType, parent, nspec);
}


PyObject *KosNamespace_Copy(PyObject *template_ns) {

  if (! KosNamespace_SupportsFast(template_ns)) {
    PyErr_Format(PyExc_TypeError, "KosNamespace_Copy requires Namespace or"
                 " CollectorNamespace, not %R", template_ns);
    return NULL;
  }

  size_t count = KosNamespace_FAST_COUNT(template_ns);
  PyObject *parent = KosNamespace_PARENT(template_ns);

  SAYF("[KosNamespace_Copy] template=%R count=%zu parent=%R",
       template_ns, (unsigned long) count, parent);

  PyObject *self = ns_alloc(&KosNamespaceType, count);
  if (! self)
    return NULL;

  // One reference to the template covers name, parent and fast_names:
  // the template owns all three for as long as this copy holds it, so
  // they are stored borrowed (see KosNamespace_TEMPLATE).
  Py_INCREF(template_ns);
  KosNamespace_TEMPLATE(self) = template_ns;
  KosNamespace_NAME(self) = KosNamespace_NAME(template_ns);
  KosNamespace_PARENT(self) = parent;
  KosNamespace_FAST_NAMES(self) = KosNamespace_FAST_NAMES(template_ns);

  // Function namespaces carry every ref in fast[]; name lookups resolve
  // against fast_names, so no binds dict is allocated.  Raw-slot promotion
  // stays in the fast slot; only a dynamic non-slot bind lazily creates one.
  KosNamespace_BINDS(self) = NULL;
  KosNamespace_FAST_COUNT(self) = (uint32_t) count;
  KosNamespace_FAST_LOCALS(self) = KosNamespace_FAST_LOCALS(template_ns);

  // Copy raw-flag bits wholesale.
  ns_copy_raw_bits(KosNamespace_FAST_RAW(self),
                   KosNamespace_FAST_RAW_MORE(self),
                   KosNamespace_FAST_RAW(template_ns),
                   KosNamespace_FAST_RAW_MORE(template_ns),
                   count);

  // Only non-raw (closure/global) slots carry a Ref to copy; raw local slots
  // are already NULL from ns_alloc, so skip them.  The Ref lives in the fast
  // slot and name lookups resolve against fast_names via ns_fast_index, so no
  // per-call binds entry is built.
  for (size_t i = 0; i < count; i++) {
    if (kos_ns_is_raw(template_ns, i))
      continue;

    PyObject *ref = KosNamespace_GET_FAST_VAL(template_ns, i);
    if (! ref) {
      SAYF("[KosNamespace_Copy] slot %zu: closure ref is NULL",
           (unsigned long) i);
      goto fail;
    }

    Py_INCREF(ref);
    KosNamespace_GET_FAST_VAL(self, i) = ref;

    SAYF("[KosNamespace_Copy] slot %zu: ref=%R", (unsigned long) i, ref);
  }

  SAYF("[KosNamespace_Copy] done self=%R", self);
  return self;

 fail:
  SAY("[KosNamespace_Copy] fail");
  Py_DECREF(self);
  return NULL;
}


static PyObject *nspec_m_namespace(PyObject *self, PyObject *parent) {

  PyObject *ns = KosNamespace_FromSpec(self, parent);
  KosNamespace_TrackChain(ns);
  return ns;
}


static PyMethodDef nspec_methods[] = {
  { "get_namespace", (PyCFunction) nspec_m_namespace, METH_O,
    "NSPEC.get_namespace(parent) -> NS" },

  { NULL },
};


static int nspec_contains(PyObject *self, PyObject *key) {
  PyObject *loc = KosNamespaceSpec_LOCAL_SYMS(self);
  PyObject *clsr = KosNamespaceSpec_CLOSURE_SYMS(self);
  PyObject *glbl = KosNamespaceSpec_GLOBAL_SYMS(self);
  if (loc && PySequence_Contains(loc, key))
    return 1;

  if (clsr && PySequence_Contains(clsr, key))
    return 1;

  if (glbl && PySequence_Contains(glbl, key))
    return 1;
  return 0;
}


static PySequenceMethods nspec_seq = {
  .sq_contains = nspec_contains,
};


static PyObject *nspec_new(PyTypeObject *type,
                           PyObject *args, PyObject *kwds) {

  PyObject *name = NULL;
  PyObject *local_syms = NULL;
  PyObject *closure_syms = NULL;
  PyObject *global_syms = NULL;

  if (! PyArg_ParseTuple(args, "OOO|O:NamespaceSpec", &name,
                         &local_syms, &closure_syms, &global_syms))
    return NULL;

  return KosNamespaceSpec_New(name, local_syms, closure_syms, global_syms);
}


static void nspec_dealloc(PyObject *self) {
  Py_CLEAR(KosNamespaceSpec_NAME(self));
  Py_CLEAR(KosNamespaceSpec_LOCAL_SYMS(self));
  Py_CLEAR(KosNamespaceSpec_CLOSURE_SYMS(self));
  Py_CLEAR(KosNamespaceSpec_GLOBAL_SYMS(self));
  Py_CLEAR(KosNamespaceSpec_FAST_NAMES(self));
  PyMem_Free(KosNamespaceSpec_FAST_RAW_MORE(self));
  KosNamespaceSpec_FAST_RAW_MORE(self) = NULL;

  Py_TYPE(self)->tp_free(self);
}


static PyObject *nspec_repr(PyObject *self) {
  PyObject *name = KosNamespaceSpec_NAME(self);
  PyObject *closures = KosNamespaceSpec_CLOSURE_SYMS(self);
  PyObject *locals = KosNamespaceSpec_LOCAL_SYMS(self);
  PyObject *result;

  if (! name)
    name = Py_None;

  if (! closures) {
    closures = PyTuple_New(0);
    if (! closures)
      return NULL;
  }

  if (! locals) {
    locals = PyTuple_New(0);
    if (! locals) {
      if (! KosNamespaceSpec_CLOSURE_SYMS(self))
        Py_DECREF(closures);
      return NULL;
    }
  }

  PyObject *globals = KosNamespaceSpec_GLOBAL_SYMS(self);
  if (! globals) {
    globals = PyTuple_New(0);
    if (! globals) {
      if (! KosNamespaceSpec_CLOSURE_SYMS(self))
        Py_DECREF(closures);
      if (! KosNamespaceSpec_LOCAL_SYMS(self))
        Py_DECREF(locals);
      return NULL;
    }

  } else {
    Py_INCREF(globals);
  }

  result = PyUnicode_FromFormat("NamespaceSpec(%R, %R, %R, %R)",
                               name, closures, locals, globals);

  if (! KosNamespaceSpec_CLOSURE_SYMS(self))
    Py_DECREF(closures);

  if (! KosNamespaceSpec_LOCAL_SYMS(self))
    Py_DECREF(locals);

  Py_DECREF(globals);

  return result;
}


PyTypeObject KosNamespaceSpecType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "NamespaceSpec",
  sizeof(KosNamespaceSpec),
  0,

  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_members = nspec_members,
  .tp_methods = nspec_methods,
  .tp_as_sequence = &nspec_seq,
  .tp_new = nspec_new,
  .tp_dealloc = nspec_dealloc,
  .tp_repr = nspec_repr,
};


PyObject *KosNamespaceSpec_New(
    PyObject *name,
    PyObject *local_syms,
    PyObject *closure_syms,
    PyObject *global_syms) {

  // SAYF("KosNamespaceSpec_New(%R, %R, %R, %R)",
  //      name, local_syms, closure_syms, global_syms);

  if (name == Py_None)
    name = NULL;

  if (name && ! PyUnicode_Check(name)) {
    PyErr_Format(PyExc_TypeError, "name must be a str, not %R", name);
    return NULL;
  }

  if (local_syms == Py_None || ! local_syms) {
    local_syms = PyTuple_New(0);
    if (! local_syms)
      return NULL;

  } else {
    local_syms = PySequence_Tuple(local_syms);
    if (! local_syms)
      return NULL;
  }

  if (closure_syms == Py_None || ! closure_syms) {
    closure_syms = PyTuple_New(0);
    if (! closure_syms) {
      Py_DECREF(local_syms);
      return NULL;
    }

  } else {
    closure_syms = PySequence_Tuple(closure_syms);
    if (! closure_syms) {
      Py_DECREF(local_syms);
      return NULL;
    }
  }

  if (global_syms == Py_None || ! global_syms) {
    global_syms = PyTuple_New(0);
    if (! global_syms) {
      Py_DECREF(local_syms);
      Py_DECREF(closure_syms);
      return NULL;
    }

  } else {
    global_syms = PySequence_Tuple(global_syms);
    if (! global_syms) {
      Py_DECREF(local_syms);
      Py_DECREF(closure_syms);
      return NULL;
    }
  }

  PyObject *self = PyObject_New(PyObject, &KosNamespaceSpecType);
  if (! self) {
    Py_XDECREF(local_syms);
    Py_XDECREF(closure_syms);
    Py_XDECREF(global_syms);
    return NULL;
  }

  Py_XINCREF(name);
  KosNamespaceSpec_NAME(self) = name;

  KosNamespaceSpec_LOCAL_SYMS(self) = local_syms;
  KosNamespaceSpec_CLOSURE_SYMS(self) = closure_syms;
  KosNamespaceSpec_GLOBAL_SYMS(self) = global_syms;
  KosNamespaceSpec_FAST_NAMES(self) = NULL;
  KosNamespaceSpec_FAST_RAW(self)[0] = 0;
  KosNamespaceSpec_FAST_RAW(self)[1] = 0;
  KosNamespaceSpec_FAST_RAW_MORE(self) = NULL;

  // Build the cached fast_names tuple: every local, closure, and global
  // symbol in that order. Storage is unbounded; the compiler decides
  // which slots its FAST operands can reach.
  Py_ssize_t n_loc = PyTuple_GET_SIZE(local_syms);
  Py_ssize_t n_clo = PyTuple_GET_SIZE(closure_syms);
  Py_ssize_t n_glo = PyTuple_GET_SIZE(global_syms);
  Py_ssize_t total = n_loc + n_clo + n_glo;

  PyObject *fast_names = PyTuple_New(total);
  if (! fast_names) {
    Py_DECREF(self);
    return NULL;
  }

  Py_ssize_t fi = 0;
  for (Py_ssize_t li = 0; li < n_loc; fi++, li++) {
    PyObject *sym = PyTuple_GET_ITEM(local_syms, li);
    Py_INCREF(sym);
    PyTuple_SET_ITEM(fast_names, fi, sym);
  }
  for (Py_ssize_t ci = 0; ci < n_clo; fi++, ci++) {
    PyObject *sym = PyTuple_GET_ITEM(closure_syms, ci);
    Py_INCREF(sym);
    PyTuple_SET_ITEM(fast_names, fi, sym);
  }
  for (Py_ssize_t gi = 0; gi < n_glo; fi++, gi++) {
    PyObject *sym = PyTuple_GET_ITEM(global_syms, gi);
    Py_INCREF(sym);
    PyTuple_SET_ITEM(fast_names, fi, sym);
  }
  KosNamespaceSpec_FAST_NAMES(self) = fast_names;

  // Precompute the raw-flag bits: locals occupy raw slots, closures and
  // globals hold refs. Bits 0..(n_loc-1) are set, across the inline
  // words and, past 128 slots, the overflow words.
  size_t words = kos_ns_raw_more_words((size_t) total);
  if (words) {
    uint64_t *more = PyMem_Calloc(words, sizeof(uint64_t));
    if (! more) {
      Py_DECREF(self);
      return PyErr_NoMemory();
    }
    KosNamespaceSpec_FAST_RAW_MORE(self) = more;
  }
  for (Py_ssize_t li = 0; li < n_loc; li++) {
    uint64_t *word = (li < KOS_NS_RAW_INLINE)
        ? &KosNamespaceSpec_FAST_RAW(self)[li >> 6]
        : &KosNamespaceSpec_FAST_RAW_MORE(self)[(li - KOS_NS_RAW_INLINE) >> 6];
    *word |= 1ULL << (li & 63);
  }

  // SAYF("KosNamespaceSpec_New -> %R", self);
  return self;
}


/* === Namespace === */


static PyObject *ns_borrow_loose_ref_index(
    PyObject *namespace,
    PyObject *key,
    int *out_index) {

  *out_index = -1;

  PyObject *binds = KosNamespace_BINDS(namespace);
  if (binds) {
    PyObject *ref = PyDict_GetItemWithError(binds, key);
    if (ref || PyErr_Occurred())
      return ref;
  }

  // Not held in the dict (or dict-less): resolve against the fast slot. A
  // raw slot synthesizes the Py_None sentinel; a non-raw slot already
  // carries its Ref in fast[], so hand that back (borrowed). Either way
  // the found index rides along in *out_index, so a store never scans
  // the name table a second time.
  int index = ns_fast_index(namespace, key);
  if (index < 0)
    return NULL;
  *out_index = index;
  if (kos_ns_is_raw(namespace, (size_t) index))
    return Py_None;
  return KosNamespace_GET_FAST_VAL(namespace, (size_t) index);
}


static PyObject *ns_borrow_loose_ref(PyObject *namespace, PyObject *key) {
  int index;
  return ns_borrow_loose_ref_index(namespace, key, &index);
}


static PyObject *ns_borrow_ref(PyObject *namespace, PyObject *key) {
  PyObject *binds = KosNamespace_BINDS(namespace);

  if (binds) {
    PyObject *ref = PyDict_GetItemWithError(binds, key);
    if (ref)
      return ref;  // borrowed reference to a Ref instance
    if (PyErr_Occurred())
      return NULL;
  }

  // Not held in the dict (or dict-less): resolve against the fast slot.
  int index = ns_fast_index(namespace, key);
  if (index < 0)
    return NULL;

  // A non-raw slot already carries its Ref in fast[]; hand it back borrowed.
  if (! kos_ns_is_raw(namespace, (size_t) index))
    return KosNamespace_GET_FAST_VAL(namespace, (size_t) index);

  // Raw slot: promote it to a Ref in place.  A namespace that keeps a dict
  // (collector/module) also records the promotion there; a dict-less
  // function namespace carries the Ref solely in its fast slot.
  SAYF("converting binding %S from raw to reference", key);

  // Create ref with NULL value first so the error path never touches
  // the slot's raw value pointer.
  PyObject *ref = KosSymbolicRef_New(key, NULL);
  if (! ref)
    return NULL;

  if (binds && PyDict_SetItem(binds, key, ref)) {
    Py_DECREF(ref);  // safe: ref->value is NULL
    return NULL;
  }

  // Steal the raw value into the ref and donate our New reference to the
  // slot.  rc == 2 (dict + slot) with a dict, else rc == 1 (slot only);
  // either way the caller borrows.
  KosSymbolicRef_VALUE(ref) = KosNamespace_GET_FAST_VAL(namespace,
                                                         (size_t) index);
  KosNamespace_GET_FAST_VAL(namespace, (size_t) index) = ref;
  kos_ns_set_raw(namespace, (size_t) index, 0);

  return ref;
}


/* The cell a class body holds for a name of its enclosing scope: a
   collector's slots past its locals are the closure and global refs
   its spec resolved at construction. They are not the body's own
   bindings, so a method may close over them; the body reaches the
   enclosing function's cells only through them, since the stored
   parent link skips the call frame. The body may declare the same
   name itself, so the search starts past the locals. Borrowed, or
   NULL for a name the body did not capture (with the error set when
   a key comparison failed). */
static PyObject *ns_collector_captured_ref(
    PyObject *namespace,
    PyObject *key) {
  PyObject *names = KosNamespace_FAST_NAMES(namespace);
  if (! names)
    return NULL;
  Py_ssize_t count = PyTuple_GET_SIZE(names);
  for (Py_ssize_t index = KosNamespace_FAST_LOCALS(namespace);
       index < count; index++) {
    int same = Py_EQUALS(key, PyTuple_GET_ITEM(names, index));
    if (same < 0)
      return NULL;
    if (same)
      return KosNamespace_GET_FAST_VAL(namespace, (size_t) index);
  }
  return NULL;
}


/* The creating counterpart of KosNamespace_BorrowRef. A function, collector,
   or module namespace yields the Ref bound to key; a module namespace
   creates the binding on a miss and keeps it thereafter. Used by the
   paths that request a Ref to hold on to (closure resolution, the
   explicit ref forms, attribute stores); value reads and membership
   tests go through KosNamespace_BorrowRef, which never creates. */
static PyObject *ns_claim_ref(PyObject *namespace, PyObject *key) {
  if (KosNamespace_CheckExact(namespace)) {
    return ns_borrow_ref(namespace, key);

  } else if (KosCollectorNamespace_CheckExact(namespace)) {
    /* A class body's bindings are values the class dict copies out;
       the cells themselves die with the body, so a Ref to one would be
       false. A ref request falls through to the enclosing scope, which
       the body's own declarations never shadow. __class__ is the one
       cell that outlives the body (see KosNamespace_FindRef). */
    if (PyUnicode_Equal(key, str_class_name))
      return ns_borrow_ref(namespace, key);
    PyObject *captured = ns_collector_captured_ref(namespace, key);
    if (captured || PyErr_Occurred())
      return captured;
    PyObject *found = NULL;
    if (KosNamespace_FindRef(KosNamespace_PARENT(namespace), key, &found) < 0)
      return NULL;
    return found;

  } else if (KosModuleNamespace_CheckExact(namespace)) {
    return mdl_ns_borrow_ref(namespace, key);

  } else {
    return NULL;
  }
}


static PyObject *find_module_in_chain(PyObject *ns) {
  while (ns) {
    if (KosModuleNamespace_CheckExact(ns))
      return ns;
    if (! KosNamespace_Check(ns))
      return NULL;
    ns = KosNamespace_PARENT(ns);
  }
  return NULL;
}


/* Fast namespace init from a NamespaceSpec that has precomputed fast_names
   and bitmasks.  Skips tuple allocation and per-slot bit manipulation for
   locals; only does the runtime work needed for closure and global refs. */
static int ns_init_syms_from_nspec(
    PyObject *self,
    PyObject *parent,
    PyObject *nspec) {

  // Borrow cached fast_names and bitmasks from the nspec (just an incref).
  PyObject *fn = KosNamespaceSpec_FAST_NAMES(nspec);
  Py_XINCREF(fn);
  KosNamespace_FAST_NAMES(self) = fn;
  ns_copy_raw_bits(KosNamespace_FAST_RAW(self),
                   KosNamespace_FAST_RAW_MORE(self),
                   KosNamespaceSpec_FAST_RAW(nspec),
                   KosNamespaceSpec_FAST_RAW_MORE(nspec),
                   fn ? (size_t) PyTuple_GET_SIZE(fn) : 0);

  // Local slots are already NULL (zeroed before this call) and their raw bits
  // are set in the bitmask above.  No dict entries for locals (sentinel-free).
  PyObject *local_syms = KosNamespaceSpec_LOCAL_SYMS(nspec);
  size_t count = local_syms ? (size_t) PyTuple_GET_SIZE(local_syms) : 0;
  KosNamespace_FAST_LOCALS(self) = (uint32_t) count;

  // Closures: find ref in parent chain and bind in dict + fast slot.
  // The closure slots are already not-raw in the precomputed bitmask.
  PyObject *closure_syms = KosNamespaceSpec_CLOSURE_SYMS(nspec);
  if (closure_syms) {
    size_t target = (size_t) PyTuple_GET_SIZE(closure_syms);
    for (size_t i = 0; i < target; i++) {
      PyObject *ref_name = PyTuple_GET_ITEM(closure_syms, i);

      PyObject *ref = NULL;
      if (KosNamespace_FindRef(parent, ref_name, &ref))
        return -1;

      if (! ref) {
        PyErr_Format(PyExc_NameError, "No such reference: %S", ref_name);
        return -1;
      }

      // The Ref lives in the fast slot; name lookups resolve against
      // fast_names, so the template carries no per-name binds entry.
      Py_INCREF(ref);
      KosNamespace_GET_FAST_VAL(self, count) = ref;
      count++;
    }
  }

  // Globals: find ref in module namespace and bind into the fast slot.
  // The global slots are already not-raw in the precomputed bitmask, so
  // a global sym with no module to resolve it in refuses the namespace
  // rather than leaving a slot marked as holding a Ref empty.
  PyObject *global_syms = KosNamespaceSpec_GLOBAL_SYMS(nspec);
  if (global_syms && PyTuple_GET_SIZE(global_syms) > 0) {
    PyObject *module_ns = find_module_in_chain(parent);
    if (! module_ns) {
      PyErr_Format(PyExc_RuntimeError,
                   "global_syms require module in parent chain");
      return -1;
    }

    size_t target = (size_t) PyTuple_GET_SIZE(global_syms);
    for (size_t i = 0; i < target; i++) {
      PyObject *ref_name = PyTuple_GET_ITEM(global_syms, i);

      PyObject *ref = mdl_ns_borrow_ref(module_ns, ref_name);
      if (! ref) {
        PyErr_Format(PyExc_NameError, "No such reference in module: %S",
                     ref_name);
        return -1;
      }

      Py_INCREF(ref);
      KosNamespace_GET_FAST_VAL(self, count) = ref;
      count++;
    }
  }

  return 0;
}


/* The parent link a new namespace stores: the nearest ancestor that is
   not a call-frame namespace (one made by KosNamespace_Copy, which
   carries a template). A frame's names reach a nested scope only as
   captured cells, resolved from the live parent at construction, so the
   link skips the frame and the frame stays private to its call. */
static PyObject *ns_link_parent(PyObject *parent) {
  while (parent && KosNamespace_Check(parent)
         && KosNamespace_TEMPLATE(parent))
    parent = KosNamespace_PARENT(parent);
  return parent;
}


/* Allocate and initialize a namespace from a NamespaceSpec, using cached
   fast_names and bitmasks to skip per-slot work.  This is the hot path
   called on every function invocation. */
static PyObject *ns_newfull_from_nspec(
    PyTypeObject *type, PyObject *parent, PyObject *nspec) {

  if (parent == Py_None)
    parent = NULL;

  if (parent && ! KosNamespace_Check(parent)) {
    PyErr_Format(PyExc_TypeError,
                 "Namespace parent must be Namespace, not %R", parent);
    return NULL;
  }

  PyObject *fn = KosNamespaceSpec_FAST_NAMES(nspec);
  size_t count = fn ? (size_t) PyTuple_GET_SIZE(fn) : 0;

  PyObject *name = KosNamespaceSpec_NAME(nspec);
  if (! name || name == Py_None)
    name = _namespace_str;

  // Plain (function) namespaces resolve names against fast_names and keep no
  // binds dict; the collector type needs one for dynamic, slot-less names.
  PyObject *binds = NULL;
  if (type != &KosNamespaceType) {
    binds = PyDict_New();
    if (! binds)
      return NULL;
  }

  PyObject *self = ns_alloc(type, count);
  if (! self) {
    Py_XDECREF(binds);
    return NULL;
  }

  Py_INCREF(name);
  KosNamespace_NAME(self) = name;
  {
    PyObject *link = ns_link_parent(parent);
    Py_XINCREF(link);
    KosNamespace_PARENT(self) = link;
  }
  KosNamespace_BINDS(self) = binds;
  KosNamespace_FAST_COUNT(self) = (uint32_t) count;
  KosNamespace_FAST_NAMES(self) = NULL;

  /* closure and global syms resolve from the live parent, where the
     captured slots are; the stored link above may sit past it */
  if (ns_init_syms_from_nspec(self, parent, nspec))
    Py_CLEAR(self);

  return self;
}


static int ns_init_syms(PyObject *self, PyObject *parent,
                        PyObject *local_syms, PyObject *closure_syms,
                        PyObject *global_syms) {

  if (! self)
    return -1;

  size_t count = 0;
  PyObject *binds = KosNamespace_BINDS(self);
  Py_ssize_t total = (Py_ssize_t) KosNamespace_FAST_COUNT(self);

  // Pre-allocate the names tuple and store it immediately so that
  // ns_dealloc can Py_CLEAR it if we fail partway through.
  PyObject *fast_names = PyTuple_New(total);
  if (! fast_names)
    return -1;
  KosNamespace_FAST_NAMES(self) = fast_names;

  if (local_syms) {
    size_t target = PySequence_Fast_GET_SIZE(local_syms);
    for (size_t i = 0; i < target; i++) {
      PyObject *ref_name = PySequence_Fast_GET_ITEM(local_syms, i);

      // No dict entry for locals — ns_borrow_loose_ref synthesizes Py_None.
      KosNamespace_GET_FAST_VAL(self, count) = NULL;
      kos_ns_set_raw(self, count, 1);

      Py_INCREF(ref_name);
      PyTuple_SET_ITEM(fast_names, (Py_ssize_t) count, ref_name);
      count++;
    }
  }

  if (closure_syms) {
    size_t target = PySequence_Fast_GET_SIZE(closure_syms);
    for (size_t i = 0; i < target; i++) {
      PyObject *ref_name = PySequence_Fast_GET_ITEM(closure_syms, i);
      PyObject *ref = NULL;

      if (KosNamespace_FindRef(parent, ref_name, &ref))
        return -1;

      if (! ref) {
        PyErr_Format(PyExc_NameError, "No such reference: %S", ref_name);
        return -1;
      }

      if (PyDict_SetItem(binds, ref_name, ref) < 0)
        return -1;

      Py_INCREF(ref);
      KosNamespace_GET_FAST_VAL(self, count) = ref;
      kos_ns_set_raw(self, count, 0);

      Py_INCREF(ref_name);
      PyTuple_SET_ITEM(fast_names, (Py_ssize_t) count, ref_name);
      count++;
    }
  }

  if (global_syms && PyTuple_GET_SIZE(global_syms) > 0) {
    PyObject *module_ns = find_module_in_chain(parent);
    if (! module_ns) {
      PyErr_Format(PyExc_RuntimeError,
                   "global_syms require module in parent chain");
      return -1;
    }

    size_t target = PySequence_Fast_GET_SIZE(global_syms);
    for (size_t i = 0; i < target; i++) {
      PyObject *ref_name = PySequence_Fast_GET_ITEM(global_syms, i);
      PyObject *ref = mdl_ns_borrow_ref(module_ns, ref_name);
      if (! ref) {
        PyErr_Format(PyExc_NameError, "No such reference in module: %S",
                     ref_name);
        return -1;
      }

      if (PyDict_SetItem(binds, ref_name, ref) < 0)
        return -1;

      Py_INCREF(ref);
      KosNamespace_GET_FAST_VAL(self, count) = ref;
      kos_ns_set_raw(self, count, 0);

      Py_INCREF(ref_name);
      PyTuple_SET_ITEM(fast_names, (Py_ssize_t) count, ref_name);
      count++;
    }
  }

  return 0;
}


/* The fast slot index of key, or -1: with no error set when there is
   no slot, with the comparison's error set when a name comparison
   raised. A caller whose key may be any object tests PyErr_Occurred on
   a miss. */
static int ns_fast_index(PyObject *self, PyObject *key) {
  if (! KosNamespace_SupportsFast(self))
    return -1;
  PyObject *names = KosNamespace_FAST_NAMES(self);
  if (! names)
    return -1;
  Py_ssize_t count = PyTuple_GET_SIZE(names);
  for (Py_ssize_t index = 0; index < count; index++) {
    PyObject *name = PyTuple_GET_ITEM(names, index);
    if (! name)
      continue;
    int same = Py_EQUALS(key, name);
    if (same < 0)
      return -1;
    if (same)
      return (int) index;
  }
  return -1;
}


/* Deref through a loose ref already looked up for key: the Py_None
   sentinel reads the raw fast slot, a Ref derefs. New reference, or
   NULL with NameError when the binding is empty. */
static PyObject *ns_deref_loose(PyObject *self, PyObject *key,
                                PyObject *ref) {
  if (ref == Py_None) {
    int index = ns_fast_index(self, key);
    if (index < 0) {
      if (! PyErr_Occurred())
        PyErr_Format(PyExc_NameError, "No such reference: %S", key);
      return NULL;
    }

    PyObject *val = KosNamespace_GET_FAST_VAL(self, (size_t) index);
    if (val) {
      Py_INCREF(val);
    } else {
      PyErr_Format(PyExc_NameError, "Empty reference: %S", key);
    }
    return val;
  }

  return KosRef_Deref(ref);
}


/* Tier 3 read: the value stored in this namespace's own binding for
   key. Never consults builtins. New reference, or NULL with NameError
   when key is unbound or the binding is empty. */
static PyObject *ns_deref_symbol(PyObject *self, PyObject *key) {
  PyObject *ref = KosNamespace_BorrowLooseRef(self, key);
  if (! ref) {
    if (! PyErr_Occurred())
      PyErr_Format(PyExc_NameError, "No such reference: %S", key);
    return NULL;
  }
  return ns_deref_loose(self, key, ref);
}


/* Resolution: the value a symbol evaluated in this namespace sees.
   Tier 3 first; a module namespace miss falls through to its builtins
   (tier 1). Never creates a binding. New reference, or NULL with
   NameError. */
static PyObject *ns_resolve_symbol(PyObject *self, PyObject *key) {
  PyObject *ref = KosNamespace_BorrowLooseRef(self, key);
  if (ref)
    return ns_deref_loose(self, key, ref);
  if (PyErr_Occurred())
    return NULL;

  PyObject *val = mdl_ns_builtin(self, key);
  if (val) {
    Py_INCREF(val);
    return val;
  }

  PyErr_Format(PyExc_NameError, "No such reference: %S", key);
  return NULL;
}


/* Single-scan classification for a store: the loose ref (borrowed;
   Py_None sentinel = raw fast slot, otherwise a Ref) with the found
   fast index riding along in *out_index (-1 when the binding lives in
   BINDS, or for module namespaces which always yield a real Ref).
   NULL with no error set means undeclared. */
static PyObject *ns_loose_ref_index(
    PyObject *self,
    PyObject *key,
    int *out_index) {

  if (KosNamespace_CheckExact(self) ||
      KosCollectorNamespace_CheckExact(self))
    return ns_borrow_loose_ref_index(self, key, out_index);

  *out_index = -1;
  if (KosModuleNamespace_CheckExact(self))
    return mdl_ns_borrow_ref(self, key);
  return NULL;
}


/* Write val through a classified binding: the raw-slot sentinel takes
   a direct fast[] write at the carried index, a Ref stores (or clears,
   for NULL val) through the cell. */
static int ns_store_classified(
    PyObject *self,
    PyObject *ref,
    int index,
    PyObject *key,
    PyObject *val) {

  if (ref == Py_None) {
    if (index < 0) {
      PyErr_Format(PyExc_NameError, "No such reference: %S", key);
      return -1;
    }

    Py_XINCREF(val);
    Py_XSETREF(KosNamespace_GET_FAST_VAL(self, (size_t) index), val);
    // raw bit is already 1 when dict entry is Py_None; reaffirm it
    kos_ns_set_raw(self, (size_t) index, 1);

    return 0;
  }

  return val? KosRef_Store(ref, val): KosRef_Clear(ref);
}


/**
   Keep the dict storage of a module namespace in step with a binding
   CPython reads raw: a store mirrors the value, a clear removes the
   mirror. Only the names in mdl_mirrored_names are mirrored; every
   other name is answered by the relaying slots. Returns 0, or -1 with
   an exception set.
*/
static int mdl_ns_mirror(PyObject *self, PyObject *key, PyObject *val) {

  if (! KosModuleNamespace_CheckExact(self) || ! PyUnicode_Check(key))
    return 0;
  if (PyUnicode_GET_LENGTH(key) < 4
      || PyUnicode_READ_CHAR(key, 0) != '_'
      || PyUnicode_READ_CHAR(key, 1) != '_')
    return 0;

  int hit = PySequence_Contains(mdl_mirrored_names, key);
  if (hit <= 0)
    return hit;

  if (val)
    return PyDict_SetItem(self, key, val);
  if (PyDict_DelItem(self, key) < 0) {
    if (! PyErr_ExceptionMatches(PyExc_KeyError))
      return -1;
    PyErr_Clear();
  }
  return 0;
}


static int ns_store_symbol(PyObject *self, PyObject *key, PyObject *val) {

  int index = -1;
  PyObject *ref = ns_loose_ref_index(self, key, &index);

  if (! ref) {

    if (KosCollectorNamespace_CheckExact(self)) {
      PyObject *slot = KosSymbolicRef_New(key, val);
      if (! slot)
        return -1;
      int res = PyDict_SetItem(KosNamespace_BINDS(self), key, slot);
      Py_DECREF(slot);
      return res;
    }

    if (! PyErr_Occurred())
      PyErr_Format(PyExc_NameError, "No such reference: %S", key);
    return -1;
  }

  if (ns_store_classified(self, ref, index, key, val) < 0)
    return -1;
  return mdl_ns_mirror(self, key, val);
}


static int ns_bind_ref(PyObject *self, PyObject *key, PyObject *ref) {
  // SAYF("ns_bind_ref(%R, %R, %R)", self, key, ref);

  if (! KosRef_Check(ref)) {
    PyErr_Format(PyExc_TypeError, "namespace binding must be a ref, not: %R",
                 ref);
    return -1;
  }

  int fast = KosNamespace_FastIndex(self, key);
  // SAYF(" fast index %i", fast);
  if (fast < 0 && PyErr_Occurred())
    return -1;
  if (fast >= 0) {
    Py_INCREF(ref);
    Py_XSETREF(KosNamespace_GET_FAST_VAL(self, (size_t) fast), ref);
    kos_ns_set_raw(self, (size_t) fast, 0);
  }

  PyObject *binds = KosNamespace_BINDS(self);
  if (! binds) {
    // A dict-less function namespace already carries a slot-backed ref in
    // fast[]; only a slot-less (dynamic) name still needs a dict, created
    // lazily here.
    if (fast >= 0)
      return 0;
    binds = PyDict_New();
    if (! binds)
      return -1;
    KosNamespace_BINDS(self) = binds;
  }

  int res = PyDict_SetItem(binds, key, ref);
  SAYF("ns_bind_ref -> %i", res);
  return res;
}




static PyObject *ns_m_ref(PyObject *self, PyObject *key) {

  PyObject *result = ns_claim_ref(self, key);
  if (! result && ! PyErr_Occurred())
    PyErr_Format(PyExc_NameError, "no such reference: %R", key);

  Py_XINCREF(result);
  return result;
}


/* Tier 2: is key bound in this namespace. An emptied Ref and a raw
   fast slot both count. 1, 0, or -1 with an error set. */
static int ns_is_bound(PyObject *self, PyObject *key) {
  PyObject *const ref = KosNamespace_BorrowLooseRef(self, key);
  if (ref)
    return 1;
  return PyErr_Occurred()? -1: 0;
}


/* Tier 3: does this namespace's binding for key hold a value. 1, 0,
   or -1 with an error set. */
static int ns_has_value(PyObject *self, PyObject *key) {
  PyObject *const ref = KosNamespace_BorrowLooseRef(self, key);
  if (! ref)
    return PyErr_Occurred()? -1: 0;
  if (ref == Py_None) {
    int index = ns_fast_index(self, key);
    if (index < 0)
      return PyErr_Occurred()? -1: 0;
    return KosNamespace_GET_FAST_VAL(self, (size_t) index) != NULL;
  }
  return ! KosRef_IsEmpty(ref);
}


/* === Tiers ===

   A namespace answers at three tiers. Tier 2 is the set of names bound
   here, each to a Ref (or a raw fast slot standing in for one); the set
   is append-only, a name is emptied but never unbound. Tier 3 is the
   values those refs hold. A module namespace adds tier 1, its builtins
   dict, which a symbol falls through to when the module has no value.

   The Python mapping protocol answers at tier 3 only: `in` means the
   name holds a value, `[]` and get() read it, `del` empties it, and
   len(), keys(), values(), items(), and iteration range over the names
   holding a value. Tier 2 is reached through the refs view and ref(),
   bind(), bound(); tier 1 through builtins; and cross-tier lookup, what
   a symbol evaluated here would see, through resolve().

   A collector namespace's mapping ranges only over the names its body
   declared (the fast slots below its local count, raw or bound to a
   ref, and the dynamic dict entries), never the closure or global
   captures sharing the namespace; that set is what a class body hands
   to its metaclass. */


/* Which declared names a namespace's mapping ranges over: 1 when key
   is one of them, 0 when not, -1 with an error set. Only a collector
   narrows the set; every other namespace owns every declared name. */
static int ns_mapping_owns(PyObject *self, PyObject *key) {
  if (! KosCollectorNamespace_CheckExact(self))
    return 1;

  int index = ns_fast_index(self, key);
  if (index >= 0)
    return (uint32_t) index < KosNamespace_FAST_LOCALS(self);
  if (PyErr_Occurred())
    return -1;
  PyObject *binds = KosNamespace_BINDS(self);
  return binds? PyDict_Contains(binds, key): 0;
}


/* Tier 2 name list: every declared name, fast slots first then the
   dynamic dict entries, narrowed for a collector as ns_mapping_owns
   describes when gathered is set. New list, or NULL with error set. */
static PyObject *ns_declared_names(PyObject *self, int gathered) {
  PyObject *keys = PyList_New(0);
  if (! keys)
    return NULL;

  PyObject *names = KosNamespace_SupportsFast(self)
      ? KosNamespace_FAST_NAMES(self) : NULL;
  Py_ssize_t fn_count = names? PyTuple_GET_SIZE(names): 0;
  for (Py_ssize_t i = 0; i < fn_count; i++) {
    if (gathered && (uint32_t) i >= KosNamespace_FAST_LOCALS(self))
      continue;
    if (PyList_Append(keys, PyTuple_GET_ITEM(names, i)) < 0) {
      Py_DECREF(keys);
      return NULL;
    }
  }

  // a fast namespace may carry no dict at all
  PyObject *binds = KosNamespace_BINDS(self);
  PyObject *key = NULL;
  Py_ssize_t pos = 0;
  while (binds && PyDict_Next(binds, &pos, &key, NULL)) {
    if (fn_count) {
      int index = ns_fast_index(self, key);
      if (index >= 0)
        continue;
      if (PyErr_Occurred()) {
        Py_DECREF(keys);
        return NULL;
      }
    }
    if (PyList_Append(keys, key) < 0) {
      Py_DECREF(keys);
      return NULL;
    }
  }

  return keys;
}


enum ns_valued_mode { NS_VALUED_KEYS, NS_VALUED_VALUES, NS_VALUED_ITEMS };


/* Tier 3 list: the names the mapping ranges over that hold a value, as
   names, values, or (name, value) pairs. New list, or NULL with error
   set. */
static PyObject *ns_valued(PyObject *self, enum ns_valued_mode mode) {
  PyObject *names = ns_declared_names(
      self, KosCollectorNamespace_CheckExact(self));
  if (! names)
    return NULL;

  PyObject *out = PyList_New(0);
  if (! out) {
    Py_DECREF(names);
    return NULL;
  }

  Py_ssize_t count = PyList_GET_SIZE(names);
  for (Py_ssize_t i = 0; i < count; i++) {
    PyObject *key = PyList_GET_ITEM(names, i);
    PyObject *val = ns_deref_symbol(self, key);
    if (! val) {
      if (PyErr_ExceptionMatches(PyExc_NameError)) {
        PyErr_Clear();
        continue;
      }
      goto fail;
    }

    PyObject *item;
    if (mode == NS_VALUED_KEYS) {
      Py_DECREF(val);
      item = key;
      Py_INCREF(item);
    } else if (mode == NS_VALUED_VALUES) {
      item = val;
    } else {
      item = PyTuple_Pack(2, key, val);
      Py_DECREF(val);
      if (! item)
        goto fail;
    }

    int res = PyList_Append(out, item);
    Py_DECREF(item);
    if (res < 0)
      goto fail;
  }

  Py_DECREF(names);
  return out;

 fail:
  Py_DECREF(names);
  Py_DECREF(out);
  return NULL;
}


/* Tier 3 read for the mapping protocol. Returns the value, or NULL
   with NameError set when the name is outside the mapping, unbound, or
   empty; other lookup errors pass through. */
static PyObject *ns_mapping_deref(PyObject *self, PyObject *key) {
  int owns = ns_mapping_owns(self, key);
  if (owns < 0)
    return NULL;
  if (! owns) {
    PyErr_Format(PyExc_NameError, "No such reference: %S", key);
    return NULL;
  }
  return ns_deref_symbol(self, key);
}


static PyObject *ns_mapping_getitem(PyObject *self, PyObject *key) {
  PyObject *val = ns_mapping_deref(self, key);
  if (! val && PyErr_ExceptionMatches(PyExc_NameError)) {
    PyErr_Clear();
    PyErr_SetObject(PyExc_KeyError, key);
  }
  return val;
}


/* Store writes through the binding, claiming one on a module miss;
   delete empties an existing binding. A delete of an unbound name is a
   mapping miss and raises KeyError, claiming nothing. */
static int ns_mapping_setitem(PyObject *self, PyObject *key, PyObject *val) {
  if (! val) {
    int bound = ns_is_bound(self, key);
    if (bound < 0)
      return -1;
    if (! bound) {
      PyErr_SetObject(PyExc_KeyError, key);
      return -1;
    }
  }
  if (ns_store_symbol(self, key, val) == 0)
    return 0;
  if (! val && PyErr_ExceptionMatches(PyExc_NameError)) {
    PyErr_Clear();
    PyErr_SetObject(PyExc_KeyError, key);
  }
  return -1;
}


static Py_ssize_t ns_mapping_length(PyObject *self) {
  PyObject *keys = ns_valued(self, NS_VALUED_KEYS);
  if (! keys)
    return -1;
  Py_ssize_t count = PyList_GET_SIZE(keys);
  Py_DECREF(keys);
  return count;
}


static int ns_mapping_contains(PyObject *self, PyObject *key) {
  int owns = ns_mapping_owns(self, key);
  if (owns <= 0)
    return owns;
  return ns_has_value(self, key);
}


static PyObject *ns_mapping_iter(PyObject *self) {
  PyObject *keys = ns_valued(self, NS_VALUED_KEYS);
  if (! keys)
    return NULL;
  PyObject *it = PyObject_GetIter(keys);
  Py_DECREF(keys);
  return it;
}


static PyMappingMethods ns_as_mapping = {
  .mp_length = ns_mapping_length,
  .mp_subscript = ns_mapping_getitem,
  .mp_ass_subscript = ns_mapping_setitem,
};


static PySequenceMethods ns_seq = {
  .sq_contains = ns_mapping_contains,
};


static PyObject *ns_m_deref(PyObject *self, PyObject *key) {
  return ns_deref_symbol(self, key);
}


static PyObject *ns_m_get(PyObject *self, PyObject *args) {
  PyObject *name = NULL;
  PyObject *def = Py_None;

  if (! PyArg_UnpackTuple(args, "get", 1, 2, &name, &def))
    return NULL;

  PyObject *val = ns_mapping_deref(self, name);
  if (! val && PyErr_ExceptionMatches(PyExc_NameError)) {
    PyErr_Clear();
    Py_INCREF(def);
    return def;
  }
  return val;
}


/* What a symbol evaluated in this namespace sees: tier 3 of this
   namespace, then of each parent in turn, and at a module namespace
   its builtins. The first bound name wins even when empty. */
static PyObject *ns_m_resolve(PyObject *self, PyObject *args) {
  PyObject *name = NULL;
  PyObject *def = NULL;

  if (! PyArg_UnpackTuple(args, "resolve", 1, 2, &name, &def))
    return NULL;

  PyObject *val = NULL;
  PyObject *ns = self;
  while (ns) {
    PyObject *const ref = KosNamespace_BorrowLooseRef(ns, name);
    if (ref) {
      val = ns_deref_loose(ns, name, ref);
      goto done;
    }
    if (PyErr_Occurred())
      return NULL;

    val = mdl_ns_builtin(ns, name);
    if (val) {
      Py_INCREF(val);
      return val;
    }

    ns = KosNamespace_PARENT(ns);
  }

  PyErr_Format(PyExc_NameError, "No such reference: %S", name);

 done:
  if (! val && def && PyErr_ExceptionMatches(PyExc_NameError)) {
    PyErr_Clear();
    Py_INCREF(def);
    return def;
  }
  return val;
}


static PyObject *ns_m_store(PyObject *self, PyObject *args) {
  PyObject *key = NULL;
  PyObject *val = NULL;

  if (! PyArg_ParseTuple(args, "OO:Namespace.store", &key, &val))
    return NULL;

  if (ns_store_symbol(self, key, val))
    return NULL;

  Py_RETURN_NONE;
}


static PyObject *ns_m_clear(PyObject *self, PyObject *key) {
  if (ns_store_symbol(self, key, NULL))
    return NULL;

  Py_RETURN_NONE;
}


static PyObject *ns_m_bind(PyObject *self, PyObject *args) {
  PyObject *key = NULL;
  PyObject *ref = NULL;

  if (! PyArg_ParseTuple(args, "OO:Namespace.bind", &key, &ref))
    return NULL;

  if (self == NS_EMPTY) {
    PyErr_SetString(PyExc_TypeError,
                    "the empty namespace accepts no bindings");
    return NULL;
  }

  if (ns_bind_ref(self, key, ref))
    return NULL;

  Py_RETURN_NONE;
}


static PyObject *ns_m_contains(PyObject *self, PyObject *key) {
  int res = ns_mapping_contains(self, key);
  if (res < 0)
    return NULL;
  return PyBool_FromLong(res);
}


static PyObject *ns_m_bound(PyObject *self, PyObject *key) {
  int res = ns_is_bound(self, key);
  if (res < 0)
    return NULL;
  return PyBool_FromLong(res);
}


static PyObject *ns_m_raw(PyObject *self, PyObject *key) {
  PyObject *const ref = KosNamespace_BorrowLooseRef(self, key);
  if (! ref) {
    PyErr_Format(PyExc_NameError, "No such reference: %S", key);
    return NULL;

  } else if (ref == Py_None) {
    Py_RETURN_TRUE;

  } else {
    Py_RETURN_FALSE;
  }
}


static PyObject *ns_m_keys(PyObject *self) {
  return ns_valued(self, NS_VALUED_KEYS);
}


static PyObject *ns_m_values(PyObject *self) {
  return ns_valued(self, NS_VALUED_VALUES);
}


static PyObject *ns_m_items(PyObject *self) {
  return ns_valued(self, NS_VALUED_ITEMS);
}


static PyObject *ns_m_find(PyObject *self, PyObject *key) {
  PyObject *ref = Py_None;

  if (KosNamespace_FindRef(self, key, &ref)) {
    // error
    return NULL;

  } else {
    // we either found a new ref, or left it None
    Py_INCREF(ref);
    return ref;
  }
}


static PyObject *ns_m_copy(PyObject *self) {
  PyObject *ns = KosNamespace_Copy(self);
  if (! ns)
    return NULL;

  PyObject_GC_Track(ns);
  return ns;
}


/* === NamespaceRefs: the tier 2 view === */


typedef struct KosNamespaceRefs {
  PyObject_HEAD
  PyObject *ns;
} KosNamespaceRefs;


#define KosNamespaceRefs_NS(v) (((KosNamespaceRefs *) v)->ns)


static Py_ssize_t nsrefs_length(PyObject *self) {
  PyObject *keys = ns_declared_names(KosNamespaceRefs_NS(self), 0);
  if (! keys)
    return -1;
  Py_ssize_t count = PyList_GET_SIZE(keys);
  Py_DECREF(keys);
  return count;
}


/* The Ref bound to key. A raw fast slot is wrapped in a Ref on the
   way out, as ref() would. KeyError when key is unbound. */
static PyObject *nsrefs_getitem(PyObject *self, PyObject *key) {
  PyObject *ref = KosNamespace_BorrowRef(KosNamespaceRefs_NS(self), key);
  if (ref) {
    Py_INCREF(ref);
    return ref;
  }
  if (! PyErr_Occurred())
    PyErr_SetObject(PyExc_KeyError, key);
  return NULL;
}


static int nsrefs_contains(PyObject *self, PyObject *key) {
  return ns_is_bound(KosNamespaceRefs_NS(self), key);
}


static PyObject *nsrefs_keys(PyObject *self) {
  return ns_declared_names(KosNamespaceRefs_NS(self), 0);
}


static PyObject *nsrefs_iter(PyObject *self) {
  PyObject *keys = nsrefs_keys(self);
  if (! keys)
    return NULL;
  PyObject *it = PyObject_GetIter(keys);
  Py_DECREF(keys);
  return it;
}


/* (name, Ref) pairs, or Refs alone, for every bound name. */
static PyObject *nsrefs_collect(PyObject *self, int pairs) {
  PyObject *ns = KosNamespaceRefs_NS(self);
  PyObject *keys = ns_declared_names(ns, 0);
  if (! keys)
    return NULL;

  PyObject *out = PyList_New(0);
  if (! out) {
    Py_DECREF(keys);
    return NULL;
  }

  Py_ssize_t count = PyList_GET_SIZE(keys);
  for (Py_ssize_t i = 0; i < count; i++) {
    PyObject *key = PyList_GET_ITEM(keys, i);
    PyObject *ref = KosNamespace_BorrowRef(ns, key);
    if (! ref) {
      if (PyErr_Occurred())
        goto fail;
      continue;
    }
    PyObject *item = pairs? PyTuple_Pack(2, key, ref): ref;
    if (! pairs)
      Py_INCREF(item);
    if (! item)
      goto fail;
    int res = PyList_Append(out, item);
    Py_DECREF(item);
    if (res < 0)
      goto fail;
  }

  Py_DECREF(keys);
  return out;

 fail:
  Py_DECREF(keys);
  Py_DECREF(out);
  return NULL;
}


static PyObject *nsrefs_values(PyObject *self) {
  return nsrefs_collect(self, 0);
}


static PyObject *nsrefs_items(PyObject *self) {
  return nsrefs_collect(self, 1);
}


static PyObject *nsrefs_m_contains(PyObject *self, PyObject *key) {
  int res = nsrefs_contains(self, key);
  if (res < 0)
    return NULL;
  return PyBool_FromLong(res);
}


static void nsrefs_dealloc(PyObject *self) {
  PyObject_GC_UnTrack(self);
  Py_CLEAR(KosNamespaceRefs_NS(self));
  Py_TYPE(self)->tp_free(self);
}


static int nsrefs_traverse(PyObject *self, visitproc visit, void *arg) {
  Py_VISIT(KosNamespaceRefs_NS(self));
  return 0;
}


static int nsrefs_clear(PyObject *self) {
  Py_CLEAR(KosNamespaceRefs_NS(self));
  return 0;
}


static PyMappingMethods nsrefs_as_mapping = {
  .mp_length = nsrefs_length,
  .mp_subscript = nsrefs_getitem,
};


static PySequenceMethods nsrefs_as_sequence = {
  .sq_contains = nsrefs_contains,
};


static PyMethodDef nsrefs_methods[] = {
  { "keys", (PyCFunction) nsrefs_keys, METH_NOARGS,
    "R.keys() => list of every bound name, emptied bindings included" },

  { "values", (PyCFunction) nsrefs_values, METH_NOARGS,
    "R.values() => list of the Ref behind every bound name" },

  { "items", (PyCFunction) nsrefs_items, METH_NOARGS,
    "R.items() => list of (name, Ref) pairs for every bound name" },

  { "__contains__", (PyCFunction) nsrefs_m_contains, METH_O,
    "(K in R) => bool; True when K is bound, holding a value or not" },

  { NULL },
};


PyTypeObject KosNamespaceRefsType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "koskari.lib.NamespaceRefs",
  sizeof(KosNamespaceRefs),
  0,

  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_doc = "Read-only view of the refs bound in a namespace",
  .tp_dealloc = nsrefs_dealloc,
  .tp_traverse = nsrefs_traverse,
  .tp_clear = nsrefs_clear,
  .tp_as_mapping = &nsrefs_as_mapping,
  .tp_as_sequence = &nsrefs_as_sequence,
  .tp_iter = nsrefs_iter,
  .tp_methods = nsrefs_methods,
};


static PyObject *ns_get_refs(PyObject *self, void *_unused) {
  KosNamespaceRefs *view = PyObject_GC_New(KosNamespaceRefs,
                                           &KosNamespaceRefsType);
  if (! view)
    return NULL;
  Py_INCREF(self);
  view->ns = self;
  PyObject_GC_Track(view);
  return (PyObject *) view;
}


static PyGetSetDef ns_getset[] = {
  { "refs", ns_get_refs, NULL,
    "NamespaceRefs; the names bound in this namespace and their Refs",
    NULL },

  { NULL },
};


static PyMethodDef ns_methods[] = {
  { "ref", (PyCFunction) ns_m_ref, METH_O,
    "NS.ref(K) -> R\n"
    "the Ref bound to K in this namespace; a module namespace binds a"
    " fresh empty Ref on a miss" },

  { "deref", (PyCFunction) ns_m_deref, METH_O,
    "NS.deref(K) -> V\n"
    "the value K holds in this namespace; NameError when K is unbound"
    " or empty" },

  { "get", (PyCFunction) ns_m_get, METH_VARARGS,
    "NS.get(K[, default]) -> V\n"
    "the value K holds in this namespace, or default (None) when K is"
    " unbound or empty" },

  { "resolve", (PyCFunction) ns_m_resolve, METH_VARARGS,
    "NS.resolve(K[, default]) -> V\n"
    "what a symbol K evaluated here sees: this namespace, then each"
    " parent, then a module's builtins; NameError, or default when"
    " given, on a miss" },

  { "store", (PyCFunction) ns_m_store, METH_VARARGS,
    "NS.store(K, V) => None\n"
    "assigns a value to a binding in this namespace" },

  { "clear", (PyCFunction) ns_m_clear, METH_O,
    "NS.clear(K) => None\n"
    "removes the value from a binding in this namespace" },

  { "bind", (PyCFunction) ns_m_bind, METH_VARARGS,
    "NS.bind(K, R) => None\n"
    "sets the reference for a binding in this namespace" },

  { "bound", (PyCFunction) ns_m_bound, METH_O,
    "NS.bound(K) => bool\n"
    "True if K is bound in this namespace, holding a value or not" },

  { "raw", (PyCFunction) ns_m_raw, METH_O,
    "NS.raw(K) => bool\n"
    "True if the given binding is currently raw in this namespace" },

  { "keys", (PyCFunction) ns_m_keys, METH_NOARGS,
    "NS.keys() => list of the names holding a value" },

  { "values", (PyCFunction) ns_m_values, METH_NOARGS,
    "NS.values() => list of the values held" },

  { "items", (PyCFunction) ns_m_items, METH_NOARGS,
    "NS.items() => list of (name, value) pairs for the names holding"
    " a value" },

  { "__contains__", (PyCFunction) ns_m_contains, METH_O,
    "(K in NS) => bool; True when K holds a value here" },

  { "find", (PyCFunction) ns_m_find, METH_O,
    "NS.find(K) => R\n"
    "traverses this namespace and its parents to find and return a"
    " reference, or returns None if no binding is found" },

  { "copy", (PyCFunction) ns_m_copy, METH_NOARGS,
    "NS.copy() => Namespace\n"
    "copy the top-level namespace only (parent chain not copied)" },

  { NULL },
};


static PyMemberDef ns_members[] = {
  { "name", T_OBJECT, offsetof(KosNamespace, core.name), READONLY,
    "str; namespace name" },

  { "parent", T_OBJECT, offsetof(KosNamespace, core.parent), READONLY,
    "Namespace; parent namespace" },

  { NULL },
};


static PyObject *ns_newfull_type(
    PyTypeObject *type,
    PyObject *name,
    PyObject *parent,
    PyObject *local_syms,
    PyObject *closure_syms,
    PyObject *global_syms) {

  if (! name || name == Py_None)
    name = _namespace_str;

  if (! PyUnicode_Check(name)) {
    PyErr_Format(PyExc_TypeError, "Namespace name must be str,"
                 " not %R", name);
    return NULL;
  }

  if (parent == Py_None)
    parent = NULL;

  if (parent && ! KosNamespace_Check(parent)) {
    PyErr_Format(PyExc_TypeError, "Namespace parent must be Namespace,"
                 " not %R", parent);
    return NULL;
  }

  size_t count = 0;

  if (local_syms) {
    local_syms = PySequence_Fast(local_syms, "local_syms must"
                                 " be a sequence");
    if (! local_syms)
      return NULL;

    count += PySequence_Fast_GET_SIZE(local_syms);
  }

  if (closure_syms) {
    closure_syms = PySequence_Fast(closure_syms, "closure_syms must"
                                   " be a sequence");
    if (! closure_syms) {
      Py_XDECREF(local_syms);
      return NULL;
    }

    count += PySequence_Fast_GET_SIZE(closure_syms);
  }

  if (global_syms) {
    global_syms = PySequence_Fast(global_syms, "global_syms must"
                                  " be a sequence");
    if (! global_syms) {
      Py_XDECREF(local_syms);
      Py_XDECREF(closure_syms);
      return NULL;
    }

    count += PySequence_Fast_GET_SIZE(global_syms);
  }

  PyObject *binds = PyDict_New();
  if (! binds) {
    Py_XDECREF(local_syms);
    Py_XDECREF(closure_syms);
    Py_XDECREF(global_syms);
    return NULL;
  }

  PyObject *self = ns_alloc(type, count);
  if (! self) {
    Py_XDECREF(local_syms);
    Py_XDECREF(closure_syms);
    Py_XDECREF(global_syms);
    Py_DECREF(binds);
    return NULL;
  }

  Py_INCREF(name);
  KosNamespace_NAME(self) = name;

  Py_XINCREF(parent);
  KosNamespace_PARENT(self) = parent;

  KosNamespace_BINDS(self) = binds;

  KosNamespace_FAST_COUNT(self) = (uint32_t) count;
  KosNamespace_FAST_LOCALS(self) =
      local_syms ? (uint32_t) PySequence_Fast_GET_SIZE(local_syms) : 0;
  KosNamespace_FAST_NAMES(self) = NULL;

  if (ns_init_syms(self, parent, local_syms, closure_syms, global_syms))
    Py_CLEAR(self);

  Py_XDECREF(local_syms);
  Py_XDECREF(closure_syms);
  Py_XDECREF(global_syms);

  return self;
}


static PyObject *ns_new(PyTypeObject *type,
                        PyObject *args, PyObject *kwds) {

  PyObject *name = NULL;
  PyObject *parent = NULL;
  PyObject *local_syms = NULL;
  PyObject *closure_syms = NULL;
  PyObject *global_syms = NULL;

  if (kwds && PyDict_Size(kwds)) {
    PyErr_SetString(PyExc_TypeError, "Namespace takes no named arguments");
    return NULL;
  }

  if (! PyArg_ParseTuple(args, "|OOOOO:Namespace",
                         &name, &parent, &local_syms, &closure_syms,
                         &global_syms))
    return NULL;

  if (!name || name == Py_None) {
    name = NULL;

  } else if (! PyUnicode_Check(name)) {
    PyErr_Format(PyExc_TypeError, "Namespace name must be str, not %R", name);
    return NULL;
  }

  if (!parent || parent == Py_None) {
    parent = NULL;

  } else if (! KosNamespace_Check(parent)) {
    PyErr_Format(PyExc_TypeError, "Namespace parent must be Namespace, not %R",
                 parent);
    return NULL;
  }

  PyObject *ns = ns_newfull_type(type, name, parent, local_syms,
                                 closure_syms, global_syms);
  if (! ns)
    return NULL;

  /* The parent arrived already tracked and we hold no new reference to
     it, so only ns itself needs tracking. */
  PyObject_GC_Track(ns);
  return ns;
}


/* Release name, parent and fast_names: owned here when there is no
   template, borrowed through the template otherwise. Idempotent. */
static void ns_release_header(PyObject *self) {
  if (KosNamespace_TEMPLATE(self)) {
    KosNamespace_NAME(self) = NULL;
    KosNamespace_PARENT(self) = NULL;
    KosNamespace_FAST_NAMES(self) = NULL;
    Py_CLEAR(KosNamespace_TEMPLATE(self));
  } else {
    Py_CLEAR(KosNamespace_NAME(self));
    Py_CLEAR(KosNamespace_PARENT(self));
    Py_CLEAR(KosNamespace_FAST_NAMES(self));
  }
}


static void ns_dealloc(PyObject *self) {
  // SAYF("ns_dealloc(%R)", self);

  /* a revealed namespace must leave the collector before its fields
     are cleared or it is recycled to the freelist; a frame namespace
     was never tracked and skips the call */
  if (kos_gc_is_tracked(self))
    PyObject_GC_UnTrack(self);

  ns_release_header(self);
  Py_CLEAR(KosNamespace_BINDS(self));

  for (size_t i = KosNamespace_FAST_COUNT(self); i--; )
    Py_CLEAR(KosNamespace_GET_FAST_VAL(self, i));
  KosNamespace_FAST_COUNT(self) = 0;
  KosNamespace_FAST_LOCALS(self) = 0;

  // check if the namespace is valid for the freelist.
  if (Py_TYPE(self) == &KosNamespaceType &&
      Py_SIZE(self) == (Py_ssize_t) KOS_NS_MIN_SLOTS) {

    // if the freelist is not full, add the namespace to the freelist.
    if (ns_freecount < KOS_NS_FREELIST_MAX) {
      KosNamespace_FAST_RAW(self)[0] = 0;
      KosNamespace_FAST_RAW(self)[1] = 0;
      KosNamespace_PARENT(self) = (PyObject *) ns_freelist;
      ns_freelist = (KosNamespace *) self;
      ns_freecount++;
      KOS_TRACE_FL_RETURN("namespace", 0);
      return;
    }
    KOS_TRACE_FL_RETURN("namespace", 1);
  }

  Py_TYPE(self)->tp_free(self);
}


static int ns_traverse(PyObject *self, visitproc visit, void *arg) {
  if (KosNamespace_TEMPLATE(self)) {
    Py_VISIT(KosNamespace_TEMPLATE(self));
  } else {
    Py_VISIT(KosNamespace_NAME(self));
    Py_VISIT(KosNamespace_PARENT(self));
    Py_VISIT(KosNamespace_FAST_NAMES(self));
  }
  Py_VISIT(KosNamespace_BINDS(self));

  for (size_t i = KosNamespace_FAST_COUNT(self); i--; )
    Py_VISIT(KosNamespace_GET_FAST_VAL(self, i));

  return 0;
}


static int ns_clear(PyObject *self) {
  ns_release_header(self);
  Py_CLEAR(KosNamespace_BINDS(self));

  for (size_t i = KosNamespace_FAST_COUNT(self); i--; )
    Py_CLEAR(KosNamespace_GET_FAST_VAL(self, i));
  KosNamespace_FAST_COUNT(self) = 0;
  KosNamespace_FAST_LOCALS(self) = 0;

  return 0;
}


/* Every namespace is a GC object but the static empty one, which has
   no GC header: the collector's visit callbacks would read the bytes
   before it as one. tp_is_gc answering no is CPython's own way for a
   GC type whose static instance carries no header, as the nil types
   do it. */
static int ns_is_gc(PyObject *self) {
  return self != NS_EMPTY;
}


PyTypeObject KosNamespaceType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "Namespace",
  sizeof(KosNamespace),
  sizeof(PyObject *),

  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_free = PyObject_GC_Del,
  .tp_methods = ns_methods,
  .tp_members = ns_members,
  .tp_getset = ns_getset,
  .tp_new = ns_new,
  .tp_dealloc = ns_dealloc,
  .tp_traverse = ns_traverse,
  .tp_clear = ns_clear,
  .tp_is_gc = ns_is_gc,
  .tp_as_mapping = &ns_as_mapping,
  .tp_as_sequence = &ns_seq,
  .tp_iter = ns_mapping_iter,
};


PyObject *KosNamespace_New(PyObject *parent, PyObject *spec) {
  return KosNamespace_NewFull(KosNamespaceSpec_NAME(spec),
                              parent,
                              KosNamespaceSpec_LOCAL_SYMS(spec),
                              KosNamespaceSpec_CLOSURE_SYMS(spec),
                              KosNamespaceSpec_GLOBAL_SYMS(spec));
}


PyObject *KosNamespace_NewFull(
    PyObject *name,
    PyObject *parent,
    PyObject *local_syms,
    PyObject *closure_syms,
    PyObject *global_syms) {

  return ns_newfull_type(&KosNamespaceType,
                         name, parent,
                         local_syms, closure_syms, global_syms);
}


PyObject *KosNamespace_Empty(void) {
  return NS_EMPTY;
}


PyObject *KosNamespace_BorrowLooseRef(PyObject *namespace, PyObject *key) {
  if (KosNamespace_CheckExact(namespace) ||
      KosCollectorNamespace_CheckExact(namespace)) {
    return ns_borrow_loose_ref(namespace, key);

  } else if (KosModuleNamespace_CheckExact(namespace)) {
    return PyDict_GetItem(KosNamespace_BINDS(namespace), key);

  } else {
    return NULL;
  }
}


PyObject *KosNamespace_BorrowRef(PyObject *namespace, PyObject *key) {
  if (KosNamespace_CheckExact(namespace) ||
      KosCollectorNamespace_CheckExact(namespace)) {
    return ns_borrow_ref(namespace, key);

  } else if (KosModuleNamespace_CheckExact(namespace)) {
    return PyDict_GetItem(KosNamespace_BINDS(namespace), key);

  } else {
    return NULL;
  }
}


PyObject *KosNamespace_ClaimRef(PyObject *namespace, PyObject *key) {
  PyObject *result = ns_claim_ref(namespace, key);

  if (! result && ! PyErr_Occurred())
    PyErr_Format(PyExc_NameError, "no such reference: %R", key);

  return result;
}


PyObject *KosNamespace_Deref(PyObject *namespace, PyObject *key) {
  return ns_deref_symbol(namespace, key);
}


PyObject *KosNamespace_Resolve(PyObject *namespace, PyObject *key) {
  return ns_resolve_symbol(namespace, key);
}


int KosNamespace_FindRef(PyObject *namespace, PyObject *key,
                         PyObject **found) {

  // SAYF("KosNamespace_FindRef(%R, %R)", namespace, key);

  while (namespace) {
    if (! KosNamespace_Check(namespace)) {
      PyErr_Format(PyExc_TypeError, "expected Namespace instance,"
                   " found %R", namespace);
      return -1;
    }

    /* A class body (collector namespace) is not an enclosing scope for
       the functions defined in it: a method never closes over a name
       bound in its class body. There is one and only one exception,
       the name __class__, and it exists for (super). The class special
       declares __class__ in the body, methods that mention it (the
       (super) expansion does) close over that slot here, and
       kos_build_class fills it with the class object once the metaclass
       returns. This is CPython's __class__ cell and __classcell__
       protocol; no other name may cross this boundary. A cell the body
       captured from its enclosing scope is that scope's, not the
       body's, and passes through. */
    PyObject *seek;
    if (! KosCollectorNamespace_Check(namespace)
        || PyUnicode_Equal(key, str_class_name))
      seek = ns_claim_ref(namespace, key);
    else
      seek = ns_collector_captured_ref(namespace, key);

    if (! seek) {
      if (PyErr_Occurred())
        return -1;
    } else {
      *found = seek;
      return 0;
    }

    namespace = KosNamespace_PARENT(namespace);
  }

  // SAYF("KosNamespace_FindRef -> %R", result);
  return 0;
}


int KosNamespace_IsBound(PyObject *namespace, PyObject *key) {
  if (! KosNamespace_Check(namespace)) {
    PyErr_Format(PyExc_TypeError, "expected Namespace instance,"
                 " found %R", namespace);
    return -1;
  }

  return ns_is_bound(namespace, key);
}


int KosNamespace_HasValue(PyObject *namespace, PyObject *key) {
  if (! KosNamespace_Check(namespace)) {
    PyErr_Format(PyExc_TypeError, "expected Namespace instance,"
                 " found %R", namespace);
    return -1;
  }

  return ns_has_value(namespace, key);
}


int KosNamespace_Store(
    PyObject *namespace,
    PyObject *key,
    PyObject *val) {

  return ns_store_symbol(namespace, key, val);
}


int KosNamespace_Clear(PyObject *namespace, PyObject *key) {

  return ns_store_symbol(namespace, key, NULL);
}


int KosNamespace_Bind(
    PyObject *namespace,
    PyObject *key,
    PyObject *ref) {

  if (! KosNamespace_Check(namespace)) {
    PyErr_Format(PyExc_TypeError, "expected Namespace instance,"
                 " found %R", namespace);
    return -1;
  }

  return ns_bind_ref(namespace, key, ref);
}


int KosNamespace_FastIndex(PyObject *namespace, PyObject *key) {
  if (! PyUnicode_Check(key)) {
    PyErr_Format(PyExc_TypeError, "expected str, got %R", key);
    return -1;
  }

  if (! KosNamespace_SupportsFast(namespace))
    return -1;

  return ns_fast_index(namespace, key);
}


/* Convert the raw fast slot at index to a KosRef cell in place.
   The slot's raw value is stolen into the new ref's value field.
   binds[name] is updated from Py_None to the new ref.
   Returns 0 on success, -1 with error set on failure. */
static int ns_convert_to_ref(PyObject *namespace, size_t index) {
  PyObject *names = KosNamespace_FAST_NAMES(namespace);
  PyObject *name = PyTuple_GET_ITEM(names, (Py_ssize_t) index);

  // Create the ref with value=NULL so the error path is always safe.
  PyObject *ref = KosSymbolicRef_New(name, NULL);
  if (! ref)
    return -1;

  // Record the promotion in binds when the namespace keeps one.  On failure,
  // Py_DECREF(ref) is safe because ref->value is still NULL — no slot
  // reference has been touched.  A dict-less function namespace carries the
  // Ref in its fast slot alone.
  PyObject *binds = KosNamespace_BINDS(namespace);
  if (binds && PyDict_SetItem(binds, name, ref)) {
    Py_DECREF(ref);
    return -1;
  }

  // Steal the slot's counted reference into ref->value (no incref: the
  // slot's contribution to the raw value's refcount moves to ref->value).
  KosSymbolicRef_VALUE(ref) = KosNamespace_GET_FAST_VAL(namespace, index);

  // Install ref in the slot, donating our New reference to it.  ref.rc == 2
  // (dict + slot) with a dict, else rc == 1 (slot only).
  KosNamespace_GET_FAST_VAL(namespace, index) = ref;
  kos_ns_set_raw(namespace, index, 0);

  return 0;
}


PyObject *kos_ns_deref_empty_raw(PyObject *ns, size_t idx) {
  PyObject *names = KosNamespace_FAST_NAMES(ns);
  if (names && idx < (size_t) PyTuple_GET_SIZE(names))
    PyErr_Format(PyExc_NameError, "Empty reference: %S",
                 PyTuple_GET_ITEM(names, (Py_ssize_t) idx));
  else
    PyErr_Format(PyExc_NameError, "Empty reference at slot %zu", idx);
  return NULL;
}


PyObject *KosNamespace_FastDeref(PyObject *namespace, size_t index) {
  if (! KosNamespace_SupportsFast(namespace)) {
    PyErr_Format(PyExc_TypeError,
                 "KosNamespace_FastDeref requires fast namespace, not %R",
                 namespace);
    return NULL;
  }

  if (index >= KosNamespace_FAST_COUNT(namespace)) {
    PyErr_Format(PyExc_NameError, "Namespace fast index out of bounds, %zu",
                 index);
    return NULL;
  }

  if (kos_ns_is_raw(namespace, index)) {
    PyObject *value = KosNamespace_GET_FAST_VAL(namespace, index);
    if (! value) {
      PyObject *names = KosNamespace_FAST_NAMES(namespace);
      PyErr_Format(PyExc_NameError, "Empty reference: %S",
                   PyTuple_GET_ITEM(names, (Py_ssize_t) index));
    } else {
      Py_INCREF(value);
    }
    return value;

  } else {
    return KosRef_Deref(KosNamespace_GET_FAST_VAL(namespace, index));
  }
}


PyObject *KosNamespace_FastRef(PyObject *namespace, size_t index) {
  if (! KosNamespace_SupportsFast(namespace)) {
    PyErr_Format(PyExc_TypeError,
                 "KosNamespace_FastRef requires fast namespace, not %R",
                 namespace);
    return NULL;
  }

  if (index >= KosNamespace_FAST_COUNT(namespace)) {
    PyErr_Format(PyExc_NameError, "Namespace fast index out of bounds, %zu",
                 index);
    return NULL;
  }

  /* a class body's slots are not ref targets; the request resolves by
     name under the collector rule in ns_claim_ref */
  if (KosCollectorNamespace_CheckExact(namespace)) {
    PyObject *name = PyTuple_GET_ITEM(KosNamespace_FAST_NAMES(namespace),
                                      (Py_ssize_t) index);
    PyObject *ref = ns_claim_ref(namespace, name);
    if (! ref && ! PyErr_Occurred())
      PyErr_Format(PyExc_NameError, "no such reference: %R", name);
    return ref;
  }

  if (kos_ns_is_raw(namespace, index)) {
    if (ns_convert_to_ref(namespace, index) < 0)
      return NULL;
  }

  return KosNamespace_GET_FAST_VAL(namespace, index);
}


int KosNamespace_FastStore(
    PyObject *namespace,
    size_t index,
    PyObject *value) {

  if (! KosNamespace_SupportsFast(namespace)) {
    PyErr_Format(PyExc_TypeError,
                 "KosNamespace_FastStore requires fast namespace, not %R",
                 namespace);
    return -1;
  }

  if (index >= KosNamespace_FAST_COUNT(namespace)) {
    PyErr_Format(PyExc_NameError, "Namespace fast index out of bounds, %zu",
                 index);
    return -1;
  }

  if (kos_ns_is_raw(namespace, index)) {
    Py_INCREF(value);
    Py_XSETREF(KosNamespace_GET_FAST_VAL(namespace, index), value);
    return 0;

  } else {
    return KosRef_Store(KosNamespace_GET_FAST_VAL(namespace, index), value);
  }
}


int KosNamespace_FastClear(PyObject *namespace, size_t index) {
  if (! KosNamespace_SupportsFast(namespace)) {
    PyErr_Format(PyExc_TypeError,
                 "KosNamespace_FastClear requires fast namespace, not %R",
                 namespace);
    return -1;
  }

  if (index >= KosNamespace_FAST_COUNT(namespace)) {
    PyErr_Format(PyExc_NameError, "Namespace fast index out of bounds, %zu",
                 index);
    return -1;
  }

  if (kos_ns_is_raw(namespace, index)) {
    Py_CLEAR(KosNamespace_GET_FAST_VAL(namespace, index));
    return 0;

  } else {
    return KosRef_Clear(KosNamespace_GET_FAST_VAL(namespace, index));
  }
}


PyObject *KosNamespace_FastTake(PyObject *namespace, size_t index) {
  if (! KosNamespace_SupportsFast(namespace)) {
    PyErr_Format(PyExc_TypeError,
                 "KosNamespace_FastTake requires fast namespace, not %R",
                 namespace);
    return NULL;
  }

  if (index >= KosNamespace_FAST_COUNT(namespace)) {
    PyErr_Format(PyExc_NameError, "Namespace fast index out of bounds, %zu",
                 index);
    return NULL;
  }

  if (kos_ns_is_raw(namespace, index)) {
    PyObject *value = KosNamespace_GET_FAST_VAL(namespace, index);
    if (! value) {
      PyObject *names = KosNamespace_FAST_NAMES(namespace);
      PyErr_Format(PyExc_NameError, "Empty reference: %S",
                   PyTuple_GET_ITEM(names, (Py_ssize_t) index));
      return NULL;
    }
    KosNamespace_GET_FAST_VAL(namespace, index) = NULL;
    return value;

  } else {
    PyObject *ref = KosNamespace_GET_FAST_VAL(namespace, index);
    PyObject *value = KosRef_Deref(ref);
    if (! value)
      return NULL;
    if (KosRef_Clear(ref) < 0) {
      Py_DECREF(value);
      return NULL;
    }
    return value;
  }
}


int KosNamespace_FastBind(
    PyObject *namespace,
    size_t index,
    PyObject *ref) {

  if (! KosNamespace_SupportsFast(namespace)) {
    PyErr_Format(PyExc_TypeError,
                 "KosNamespace_FastBind requires fast namespace, not %R",
                 namespace);
    return -1;
  }

  if (index >= KosNamespace_FAST_COUNT(namespace)) {
    PyErr_Format(PyExc_NameError, "Namespace fast index out of bounds, %zu",
                 index);
    return -1;
  }

  if (! KosRef_Check(ref)) {
    PyErr_Format(PyExc_TypeError, "Namespace bindings must be reference,"
                 " not %R", ref);
    return -1;
  }

  PyObject *names = KosNamespace_FAST_NAMES(namespace);
  PyObject *name = PyTuple_GET_ITEM(names, (Py_ssize_t) index);

  Py_INCREF(ref);
  Py_XSETREF(KosNamespace_GET_FAST_VAL(namespace, index), ref);
  kos_ns_set_raw(namespace, index, 0);

  // The Ref lives in the fast slot; mirror it into binds only when present.
  PyObject *binds = KosNamespace_BINDS(namespace);
  if (binds)
    return PyDict_SetItem(binds, name, ref);
  return 0;
}


/* === CollectorNamespace === */


PyTypeObject KosCollectorNamespaceType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "CollectorNamespace",
  sizeof(KosCollectorNamespace),
  sizeof(PyObject *),

  .tp_base = &KosNamespaceType,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_traverse = ns_traverse,
  .tp_clear = ns_clear,
  .tp_methods = ns_methods,
  .tp_as_mapping = &ns_as_mapping,
  .tp_as_sequence = &ns_seq,
};



PyObject *KosCollectorNamespace_New(
    PyObject *parent,
    PyObject *spec) {

  return ns_newfull_from_nspec(&KosCollectorNamespaceType, parent, spec);
}


PyObject *KosCollectorNamespace_NewFull(
    PyObject *name,
    PyObject *parent,
    PyObject *local_syms,
    PyObject *closure_syms,
    PyObject *global_syms) {

  return ns_newfull_type(&KosCollectorNamespaceType,
                         name, parent,
                         local_syms, closure_syms, global_syms);
}


/* === ModuleNamespace === */


/* The builtin a module namespace would resolve name to, or NULL. A
   non-module namespace has no builtins and yields NULL. Borrowed. */
static PyObject *mdl_ns_builtin(PyObject *self, PyObject *name) {
  if (! KosModuleNamespace_CheckExact(self))
    return NULL;
  PyObject *blt = KosModuleNamespace_BUILTINS(self);
  return blt? PyDict_GetItem(blt, name): NULL;
}


/* The Ref bound to name, created on a miss and kept in BINDS from then
   on, seeded with the builtin of that name if there is one. This is the
   creating lookup behind ns_claim_ref, global binding at namespace
   init, and stores. Value reads and membership tests peek BINDS and
   then the builtins dict directly, and leave nothing behind. */
static PyObject *mdl_ns_borrow_ref(PyObject *self, PyObject *name) {

  PyObject *res = PyDict_GetItem(KosNamespace_BINDS(self), name);

  if (! res) {
    PyObject *blt = KosModuleNamespace_BUILTINS(self);
    PyObject *val = NULL;
    if (blt)
      val = PyDict_GetItem(blt, name);

    res = KosRef_New(name, val);
    if (! res)
      return NULL;

    if (PyDict_SetItem(KosNamespace_BINDS(self), name, res) < 0) {
      Py_DECREF(res);
      return NULL;
    }
    Py_DECREF(res);
  }

  // SAYF("mdl_ns_borrow_ref -> %R", res);
  return res;
}


static PyObject *mdl_ns_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  PyObject *name = NULL;
  PyObject *builtins = NULL;

  if (kwds && PyDict_Size(kwds)) {
    PyErr_SetString(PyExc_TypeError, "ModuleNamespace takes no"
                    " named arguments");
    return NULL;
  }

  if (! PyArg_ParseTuple(args, "OO:ModuleNamespace", &name, &builtins))
    return NULL;

  return KosModuleNamespace_New(name, builtins);
}


/* Release the core references a module namespace owns. A module is
   never a copy, so it owns its name and parent outright. */
static void mdl_ns_release_core(PyObject *self) {
  Py_CLEAR(KosModuleNamespace_BUILTINS(self));
  Py_CLEAR(KosNamespace_NAME(self));
  Py_CLEAR(KosNamespace_PARENT(self));
  Py_CLEAR(KosNamespace_TEMPLATE(self));
  Py_CLEAR(KosNamespace_BINDS(self));
}


static void mdl_ns_dealloc(PyObject *self) {
  /* the dict base untracks again, which is a no-op once untracked */
  PyObject_GC_UnTrack(self);
  mdl_ns_release_core(self);
  PyDict_Type.tp_dealloc(self);
}


static int mdl_ns_traverse(PyObject *self, visitproc visit, void *arg) {
  Py_VISIT(KosModuleNamespace_BUILTINS(self));
  Py_VISIT(KosNamespace_NAME(self));
  Py_VISIT(KosNamespace_PARENT(self));
  Py_VISIT(KosNamespace_TEMPLATE(self));
  Py_VISIT(KosNamespace_BINDS(self));
  return PyDict_Type.tp_traverse(self, visit, arg);
}


static int mdl_ns_clear(PyObject *self) {
  mdl_ns_release_core(self);
  return PyDict_Type.tp_clear(self);
}


/* Construction is complete at tp_new; the dict base's tp_init would
   otherwise run on the constructor arguments and refuse them. */
static int mdl_ns_init(PyObject *self, PyObject *args, PyObject *kwds) {
  (void) self;
  (void) args;
  (void) kwds;
  return 0;
}


/* Identity hash, comparison and repr, as the other namespace kinds
   have; the dict base would refuse the hash and print the scratch
   dict. */
static Py_hash_t mdl_ns_hash(PyObject *self) {
  return PyBaseObject_Type.tp_hash(self);
}


/* Identity comparison to match the identity hash. The dict base would
   compare the scratch storage, never the bindings, and it would also
   take the reflected comparison against a plain dict, so equality and
   inequality are answered here outright instead of left to it. */
static PyObject *mdl_ns_richcompare(PyObject *self, PyObject *other, int op) {
  if (op == Py_EQ)
    return PyBool_FromLong(self == other);
  if (op == Py_NE)
    return PyBool_FromLong(self != other);
  Py_RETURN_NOTIMPLEMENTED;
}


static PyObject *mdl_ns_repr(PyObject *self) {
  return PyBaseObject_Type.tp_repr(self);
}


/* The dict methods exec'd code and the stdlib reach on a globals dict,
   answered through the module's bindings rather than the scratch
   storage dict's own versions would read or write. */

static PyObject *mdl_ns_m_copy(PyObject *self) {
  PyObject *out = PyDict_New();
  if (! out)
    return NULL;
  /* the iter override routes PyDict_Update through the relay */
  if (PyDict_Update(out, self) < 0) {
    Py_DECREF(out);
    return NULL;
  }
  return out;
}


static PyObject *mdl_ns_m_reversed(PyObject *self) {
  PyObject *keys = ns_valued(self, NS_VALUED_KEYS);
  if (! keys)
    return NULL;
  if (PyList_Reverse(keys) < 0) {
    Py_DECREF(keys);
    return NULL;
  }
  PyObject *it = PyObject_GetIter(keys);
  Py_DECREF(keys);
  return it;
}


static PyObject *mdl_ns_m_setdefault(PyObject *self, PyObject *args) {
  PyObject *key = NULL;
  PyObject *def = Py_None;

  if (! PyArg_UnpackTuple(args, "setdefault", 1, 2, &key, &def))
    return NULL;

  PyObject *val = ns_mapping_deref(self, key);
  if (val)
    return val;
  if (! PyErr_ExceptionMatches(PyExc_NameError))
    return NULL;
  PyErr_Clear();

  if (ns_store_symbol(self, key, def) < 0)
    return NULL;
  Py_INCREF(def);
  return def;
}


static PyObject *mdl_ns_m_pop(PyObject *self, PyObject *args) {
  PyObject *key = NULL;
  PyObject *def = NULL;

  if (! PyArg_UnpackTuple(args, "pop", 1, 2, &key, &def))
    return NULL;

  PyObject *val = ns_mapping_deref(self, key);
  if (! val) {
    if (! PyErr_ExceptionMatches(PyExc_NameError))
      return NULL;
    PyErr_Clear();
    if (! def) {
      PyErr_SetObject(PyExc_KeyError, key);
      return NULL;
    }
    Py_INCREF(def);
    return def;
  }
  if (ns_store_symbol(self, key, NULL) < 0) {
    Py_DECREF(val);
    return NULL;
  }
  return val;
}


/* Store every item of src into the module's bindings by dict.update's
   rule: a source with a keys method is a mapping, read through keys()
   and __getitem__; anything else is an iterable of pairs. An error
   from either read propagates. Items store as they are read, so a
   failure partway leaves the earlier ones stored, as dict.update does. */
static int mdl_ns_store_from(PyObject *self, PyObject *src) {

  PyObject *keys = PyObject_GetAttrString(src, "keys");
  PyObject *items;
  Py_ssize_t n, i;

  if (keys) {
    Py_DECREF(keys);
    items = PyMapping_Keys(src);
    if (! items)
      return -1;

    n = PyList_GET_SIZE(items);
    for (i = 0; i < n; i++) {
      PyObject *key = PyList_GET_ITEM(items, i);
      PyObject *val = PyObject_GetItem(src, key);
      int r;

      if (! val) {
        Py_DECREF(items);
        return -1;
      }
      r = ns_store_symbol(self, key, val);
      Py_DECREF(val);
      if (r < 0) {
        Py_DECREF(items);
        return -1;
      }
    }
    Py_DECREF(items);
    return 0;
  }

  if (! PyErr_ExceptionMatches(PyExc_AttributeError))
    return -1;
  PyErr_Clear();

  items = PySequence_List(src);
  if (! items)
    return -1;

  n = PyList_GET_SIZE(items);
  for (i = 0; i < n; i++) {
    PyObject *pair = PySequence_Tuple(PyList_GET_ITEM(items, i));
    if (! pair || PyTuple_GET_SIZE(pair) != 2) {
      Py_XDECREF(pair);
      if (! PyErr_Occurred())
        PyErr_SetString(PyExc_ValueError,
                        "update sequence element is not a pair");
      Py_DECREF(items);
      return -1;
    }
    int r = ns_store_symbol(self, PyTuple_GET_ITEM(pair, 0),
                            PyTuple_GET_ITEM(pair, 1));
    Py_DECREF(pair);
    if (r < 0) {
      Py_DECREF(items);
      return -1;
    }
  }
  Py_DECREF(items);
  return 0;
}


static PyObject *mdl_ns_m_update(
    PyObject *self,
    PyObject *args,
    PyObject *kwds) {

  PyObject *src = NULL;

  if (! PyArg_UnpackTuple(args, "update", 0, 1, &src))
    return NULL;
  if (src && mdl_ns_store_from(self, src) < 0)
    return NULL;
  if (kwds && mdl_ns_store_from(self, kwds) < 0)
    return NULL;
  Py_RETURN_NONE;
}


/* dict | other and other | dict over the bindings: a plain dict copy
   of the module side takes part in the union. */
static PyObject *mdl_ns_nb_or(PyObject *left, PyObject *right) {
  PyObject *l = left;
  PyObject *r = right;
  PyObject *res;

  if (KosModuleNamespace_CheckExact(left)) {
    l = mdl_ns_m_copy(left);
    if (! l)
      return NULL;
  } else {
    Py_INCREF(l);
  }
  if (KosModuleNamespace_CheckExact(right)) {
    r = mdl_ns_m_copy(right);
    if (! r) {
      Py_DECREF(l);
      return NULL;
    }
  } else {
    Py_INCREF(r);
  }
  res = PyNumber_Or(l, r);
  Py_DECREF(l);
  Py_DECREF(r);
  return res;
}


static PyObject *mdl_ns_nb_ior(PyObject *self, PyObject *other) {
  if (mdl_ns_store_from(self, other) < 0)
    return NULL;
  Py_INCREF(self);
  return self;
}


static PyNumberMethods mdl_ns_as_number = {
  .nb_or = mdl_ns_nb_or,
  .nb_inplace_or = mdl_ns_nb_ior,
};


static PyMemberDef mdl_ns_members[] = {
  { "name", T_OBJECT, offsetof(KosModuleNamespace, core.name), READONLY,
    "str; namespace name" },

  { "parent", T_OBJECT, offsetof(KosModuleNamespace, core.parent), READONLY,
    "Namespace; parent namespace" },

  { NULL },
};


static PyObject *mdl_ns_get_builtins(PyObject *self, void *_unused) {
  PyObject *blt = KosModuleNamespace_BUILTINS(self);
  if (! blt)
    Py_RETURN_NONE;
  return PyDictProxy_New(blt);
}


static PyGetSetDef mdl_ns_getset[] = {
  { "refs", ns_get_refs, NULL,
    "NamespaceRefs; the names bound in this namespace and their Refs",
    NULL },

  { "builtins", mdl_ns_get_builtins, NULL,
    "mappingproxy; the builtins a symbol falls through to when this"
    " module holds no value for it",
    NULL },

  { NULL },
};


static PyMethodDef mdl_ns_methods[] = {
  { "ref", (PyCFunction) ns_m_ref, METH_O,
    "NS.ref(K) -> R\n"
    "the Ref bound to K in this module; on a miss binds a fresh Ref"
    " holding the builtin of that name, or empty when there is none" },

  { "deref", (PyCFunction) ns_m_deref, METH_O,
    "NS.deref(K) -> V\n"
    "the value K holds in this module; NameError when K is unbound or"
    " empty, builtins not consulted" },

  { "get", (PyCFunction) ns_m_get, METH_VARARGS,
    "NS.get(K[, default]) -> V\n"
    "the value K holds in this module, or default (None) when K is"
    " unbound or empty; builtins not consulted" },

  { "resolve", (PyCFunction) ns_m_resolve, METH_VARARGS,
    "NS.resolve(K[, default]) -> V\n"
    "what a symbol K evaluated in this module sees: its value here,"
    " else the builtin; NameError, or default when given, on a miss" },

  { "store", (PyCFunction) ns_m_store, METH_VARARGS,
    "NS.store(K, V) => None\n"
    "assigns a value to a binding in this namespace" },

  { "clear", (PyCFunction) ns_m_clear, METH_O,
    "NS.clear(K) => None\n"
    "removes the value from a binding in this namespace" },

  { "bind", (PyCFunction) ns_m_bind, METH_VARARGS,
    "NS.bind(K, R) => None\n"
    "sets the reference for a binding in this namespace" },

  { "bound", (PyCFunction) ns_m_bound, METH_O,
    "NS.bound(K) => bool\n"
    "True if K is bound in this module, holding a value or not;"
    " builtins do not count" },

  { "keys", (PyCFunction) ns_m_keys, METH_NOARGS,
    "NS.keys() => list of the names holding a value" },

  { "values", (PyCFunction) ns_m_values, METH_NOARGS,
    "NS.values() => list of the values held" },

  { "items", (PyCFunction) ns_m_items, METH_NOARGS,
    "NS.items() => list of (name, value) pairs for the names holding"
    " a value" },

  { "__contains__", (PyCFunction) ns_m_contains, METH_O,
    "(K in NS) => bool; True when K holds a value here" },

  { "find", (PyCFunction) ns_m_find, METH_O,
    "NS.find(K) => R\n"
    "traverses this namespace and its parents to find and return a"
    " reference, or returns None if no binding is found" },

  { "raw", (PyCFunction) ns_m_raw, METH_O,
    "NS.raw(K) => bool\n"
    "True if the given binding is currently raw in this namespace; a"
    " module binding never is" },

  { "copy", (PyCFunction) mdl_ns_m_copy, METH_NOARGS,
    "NS.copy() => dict\n"
    "a plain dict of the names holding a value and their values" },

  { "__reversed__", (PyCFunction) mdl_ns_m_reversed, METH_NOARGS,
    "reversed(NS) => iterator over the names holding a value, last first" },

  { "setdefault", (PyCFunction) mdl_ns_m_setdefault, METH_VARARGS,
    "NS.setdefault(K[, default]) -> V\n"
    "the value K holds, else bind K to default (None) and return it" },

  { "pop", (PyCFunction) mdl_ns_m_pop, METH_VARARGS,
    "NS.pop(K[, default]) -> V\n"
    "the value K holds, emptying the binding; default, or KeyError"
    " without one, when K holds no value" },

  { "update", (PyCFunction) mdl_ns_m_update, METH_VARARGS | METH_KEYWORDS,
    "NS.update([mapping], **kwds) => None\n"
    "store every item into the module's bindings" },

  { NULL },
};


/* The core must sit at one offset in every kind, and the two heads
   must be one size, so a function namespace pays nothing for the
   module namespace being a dict. */
_Static_assert(offsetof(KosNamespace, core) == offsetof(KosModuleNamespace, core),
               "namespace core offset differs between kinds");
_Static_assert(offsetof(KosNamespace, core) == offsetof(KosNamespaceAny, core),
               "namespace core offset differs from the any-kind view");
_Static_assert(sizeof(KosNamespaceHead) == sizeof(PyDictObject),
               "the fast head and the dict head differ in size");


PyTypeObject KosModuleNamespaceType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "koskari.lib.ModuleNamespace",
  sizeof(KosModuleNamespace),
  0,

  /* a dict subclass: exec and eval take it as globals, and the
     mapping slots and methods below relay every read to the module's
     bindings, never to the dict storage */
  .tp_base = &PyDict_Type,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_methods = mdl_ns_methods,
  .tp_members = mdl_ns_members,
  .tp_getset = mdl_ns_getset,
  .tp_as_mapping = &ns_as_mapping,
  .tp_as_sequence = &ns_seq,
  .tp_as_number = &mdl_ns_as_number,
  .tp_iter = ns_mapping_iter,
  .tp_hash = mdl_ns_hash,
  .tp_richcompare = mdl_ns_richcompare,
  .tp_repr = mdl_ns_repr,
  .tp_new = mdl_ns_new,
  .tp_init = mdl_ns_init,
  .tp_dealloc = mdl_ns_dealloc,
  .tp_traverse = mdl_ns_traverse,
  .tp_clear = mdl_ns_clear,
};


PyObject *KosModuleNamespace_New(PyObject *name, PyObject *builtins) {
  // SAYF("KosModuleNamespace_New(%R, %R)", name, builtins);

  if (! name || name == Py_None)
    name = _module_str;

  if (! PyUnicode_Check(name)) {
    PyErr_Format(PyExc_TypeError, "Namespace name must be str,"
                 " not %R", name);
    return NULL;
  }

  /* exact: a module namespace is a dict subclass, and the builtins
     are read with the raw dict API, which would see only its scratch */
  if (! PyDict_CheckExact(builtins)) {
    PyErr_Format(PyExc_TypeError, "ModuleNamespace builtins must be a"
                 " dict, not %R", builtins);
    return NULL;
  }

  PyObject *binds = PyDict_New();
  if (! binds)
    return NULL;

  /* the dict base allocates and initialises the dict part and tracks
     the object from birth; the core is zero until filled here */
  PyObject *args = PyTuple_New(0);
  if (! args) {
    Py_DECREF(binds);
    return NULL;
  }
  PyObject *self = PyDict_Type.tp_new(&KosModuleNamespaceType, args, NULL);
  Py_DECREF(args);
  if (! self) {
    Py_DECREF(binds);
    return NULL;
  }

  Py_INCREF(name);
  KosNamespace_NAME(self) = name;

  KosNamespace_PARENT(self) = NULL;
  KosNamespace_TEMPLATE(self) = NULL;
  KosNamespace_BINDS(self) = binds;
  KosNamespace_FAST_RAW(self)[0] = 0;
  KosNamespace_FAST_RAW(self)[1] = 0;

  Py_INCREF(builtins);
  KosModuleNamespace_BUILTINS(self) = builtins;

  // SAYF("KosModuleNamespace_New -> %R", self);
  return self;
}


/* === Module === */


static PyObject *mdl_get_name(PyObject *self, void *_unused) {
  PyObject *ns = KosModule_NAMESPACE(self);
  PyObject *name = KosNamespace_NAME(ns);
  Py_INCREF(name);
  return name;
}


static PyObject *mdl_get_namespace(PyObject *self, void *_unused) {
  PyObject *ns = KosModule_NAMESPACE(self);
  Py_INCREF(ns);
  return ns;
}


static PyGetSetDef mdl_getset[] = {

  { "__name__", mdl_get_name, NULL,
    "str; name of the underlying namespace",
    NULL },

  { "__dict__",  mdl_get_namespace, NULL,
    "Namespace; the namespace backing this module",
    NULL },

  { NULL, },
};


static PyObject *mdl_m_dir(PyObject *self) {
  return ns_valued(KosModule_NAMESPACE(self), NS_VALUED_KEYS);
}


static PyMethodDef mdl_methods[] = {

  { "__dir__", (PyCFunction) mdl_m_dir, METH_NOARGS,
    "M.__dir__() => List[str]", },

  { NULL, },
};


static PyObject *mdl_getattro(PyObject *self, PyObject *name) {
  if (! PyUnicode_CheckExact(name)) {
    PyErr_Format(PyExc_TypeError, "attribute name must be a str, not %R",
                 name);
    return NULL;
  }

  PyObject *descr = PyType_Lookup(&KosModuleType, name);
  if (descr) {
    descrgetfunc fun = Py_TYPE(descr)->tp_descr_get;
    if (fun)
      return fun(descr, self, (PyObject *) &KosModuleType);
  }

  /* tier 3 of the namespace only: a module's attributes are the
     values it holds, never its builtins */
  PyObject *ns = KosModule_NAMESPACE(self);
  PyObject *ref = KosNamespace_BorrowRef(ns, name);
  if (ref) {
    if (KosRef_IsEmpty(ref)) {
      PyErr_Format(PyExc_AttributeError, "%R object has no attribute %R",
                   self, name);
      return NULL;
    }
    return KosRef_Deref(ref);
  }

  if (! PyErr_Occurred())
    PyErr_Format(PyExc_AttributeError, "%R object has no attribute %R",
                 self, name);
  return NULL;
}


static int mdl_setattro(PyObject *self, PyObject *name, PyObject *value) {
  if (! PyUnicode_CheckExact(name)) {
    PyErr_Format(PyExc_TypeError, "attribute name must be a str, not %R",
                 name);
    return -1;
  }

  PyObject *descr = PyType_Lookup(&KosModuleType, name);
  if (descr) {
    descrsetfunc fun = Py_TYPE(descr)->tp_descr_set;
    if (fun) {
      return fun(descr, self, value);

    } else {
      PyErr_Format(PyExc_AttributeError, "%R object attribute %R is read-only",
                   self, name);
      return -1;
    }
  }

  PyObject *ns = KosModule_NAMESPACE(self);
  PyObject *ref = ns_claim_ref(ns, name);
  if (! ref) {
    if (! PyErr_Occurred())
      PyErr_Format(PyExc_AttributeError, "%R object has no attribute %R",
                   self, name);
    return -1;
  }

  if ((value? KosRef_Store(ref, value): KosRef_Clear(ref)) < 0)
    return -1;
  return mdl_ns_mirror(ns, name, value);
}


static PyObject *mdl_new(PyTypeObject *type,
                         PyObject *args, PyObject *kwds) {

  PyObject *namespace = NULL;

  if (kwds && PyDict_Size(kwds)) {
    PyErr_SetString(PyExc_TypeError, "Module takes no named arguments");
    return NULL;
  }

  if (! PyArg_ParseTuple(args, "O:Module", &namespace))
    return NULL;

  return KosModule_New(namespace);
}


static void mdl_dealloc(PyObject *self) {
  PyObject_GC_UnTrack(self);
  Py_CLEAR(KosModule_NAMESPACE(self));
  Py_TYPE(self)->tp_free(self);
}


static int mdl_traverse(PyObject *self, visitproc visit, void *arg) {
  Py_VISIT(KosModule_NAMESPACE(self));
  return 0;
}


static int mdl_clear(PyObject *self) {
  Py_CLEAR(KosModule_NAMESPACE(self));
  return 0;
}


PyTypeObject KosModuleType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "koskari.lib.Module",
  sizeof(KosModule),
  0,

  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_new = mdl_new,
  .tp_dealloc = mdl_dealloc,
  .tp_traverse = mdl_traverse,
  .tp_clear = mdl_clear,
  .tp_getset = mdl_getset,
  .tp_methods = mdl_methods,
  .tp_getattro = mdl_getattro,
  .tp_setattro = mdl_setattro,
};


PyObject *KosModule_New(PyObject *namespace) {

  // SAYF("KosModule_New(%R)", namespace);

  if (! KosNamespace_Check(namespace)) {
    PyErr_Format(PyExc_TypeError, "Module's namespace must be a Namespace,"
                 " not %R", namespace);
    return NULL;
  }

  PyObject *self = PyObject_GC_New(PyObject, &KosModuleType);
  if (! self)
    return NULL;

  Py_INCREF(namespace);
  KosModule_NAMESPACE(self) = namespace;
  PyObject_GC_Track(self);

  return self;
}


/* === SlotMap === */


static void slotmap_dealloc(PyObject *self) {

  KosSlotMap *m = (KosSlotMap *) self;

  Py_XDECREF(m->names);
  Py_XDECREF(m->layout);
  Py_TYPE(self)->tp_free(self);
}


PyTypeObject KosSlotMapType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "koskari.lib.slot_map",
  offsetof(KosSlotMap, indices),
  sizeof(int32_t),

  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_dealloc = slotmap_dealloc,
};


PyObject *KosSlotMap_New(PyObject *ns, PyObject *names) {

  if (! names || ! PyTuple_Check(names)) {
    PyErr_SetString(PyExc_TypeError, "SlotMap requires a names tuple");
    return NULL;
  }

  Py_ssize_t n = PyTuple_GET_SIZE(names);
  KosSlotMap *m = PyObject_NewVar(KosSlotMap, &KosSlotMapType, n);
  if (! m)
    return NULL;
  Py_INCREF(names);
  m->names = names;
  m->layout = kos_ns_layout(ns);
  Py_XINCREF(m->layout);

  for (Py_ssize_t i = 0; i < n; i++) {
    int idx = KosNamespace_FastIndex(ns, PyTuple_GET_ITEM(names, i));
    if (idx < 0 && PyErr_Occurred()) {
      Py_DECREF(m);
      return NULL;
    }
    m->indices[i] = (int32_t) idx;
  }
  return (PyObject *) m;
}


int KosSlotMap_Valid(PyObject *map, PyObject *ns) {

  KosSlotMap *m = (KosSlotMap *) map;
  PyObject *layout = kos_ns_layout(ns);

  if (m->layout == layout)
    return 1;
  if (! layout || ! m->layout)
    return 0;

  /* Another tuple identity for the same layout: compare contents,
     identity-first, and adopt it. */
  Py_ssize_t n = PyTuple_GET_SIZE(layout);
  if (n != PyTuple_GET_SIZE(m->layout))
    return 0;
  for (Py_ssize_t i = 0; i < n; i++) {
    PyObject *a = PyTuple_GET_ITEM(layout, i);
    PyObject *b = PyTuple_GET_ITEM(m->layout, i);
    if (a == b)
      continue;
    /* An entry stays NULL for a global sym with no parent chain. */
    if (! a || ! b)
      return 0;
    int eq = PyObject_RichCompareBool(a, b, Py_EQ);
    if (eq < 0)
      return -1;
    if (! eq)
      return 0;
  }

  Py_INCREF(layout);
  Py_SETREF(m->layout, layout);
  return 1;
}


/* === Unit lifecycle === */


int koskari_namespace_init(PyObject *mod) {
  if (! mod)
    return -1;

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  if (PyType_Ready(&KosNamespaceType) ||
      PyType_Ready(&KosNamespaceSpecType) ||
      PyType_Ready(&KosCollectorNamespaceType) ||
      PyType_Ready(&KosModuleNamespaceType) ||
      PyType_Ready(&KosModuleType) ||
      PyType_Ready(&KosNamespaceRefsType) ||
      PyType_Ready(&KosSlotMapType))
    return -1;

  STR_CONST(_namespace_str, "<namespace>");
  STR_CONST(str_class_name, "__class__");
  if (! str_class_name)
    return -1;

  /* the empty namespace's owned fields; the object itself is static */
  Py_INCREF(_namespace_str);
  KosNamespace_NAME(NS_EMPTY) = _namespace_str;
  KosNamespace_BINDS(NS_EMPTY) = PyDict_New();
  KosNamespace_FAST_NAMES(NS_EMPTY) = PyTuple_New(0);
  if (! KosNamespace_BINDS(NS_EMPTY) || ! KosNamespace_FAST_NAMES(NS_EMPTY))
    return -1;
  STR_CONST(_module_str, "<module>");
  mdl_mirrored_names = Py_BuildValue(
      "(sssss)", "__name__", "__package__", "__spec__", "__file__",
      "__loader__");
  if (! mdl_mirrored_names)
    return -1;

  PyDict_SetItemString(dict, "Namespace",
                       (PyObject *) &KosNamespaceType);

  PyDict_SetItemString(dict, "NamespaceSpec",
                       (PyObject *) &KosNamespaceSpecType);

  PyDict_SetItemString(dict, "CollectorNamespace",
                       (PyObject *) &KosCollectorNamespaceType);

  PyDict_SetItemString(dict, "ModuleNamespace",
                       (PyObject *) &KosModuleNamespaceType);

  PyDict_SetItemString(dict, "Module",
                       (PyObject *) &KosModuleType);

  PyDict_SetItemString(dict, "NamespaceRefs",
                       (PyObject *) &KosNamespaceRefsType);

  if (PyModule_AddIntConstant(mod, "NAMESPACE_FAST_SLOT_MAX",
                              KosNamespace_FAST_SLOT_MAX))
    return -1;

  return 0;
}


int koskari_namespace_clear(PyObject *mod) {

  while (ns_freelist) {
    KosNamespace *ns = ns_freelist;
    ns_freelist = (KosNamespace *) KosNamespace_PARENT(ns);

    // KosNamespace_PARENT(ns) = NULL;
    PyObject_GC_Del(ns);
  }
  ns_freecount = 0;

  ns_release_header(NS_EMPTY);
  Py_CLEAR(KosNamespace_BINDS(NS_EMPTY));
  Py_CLEAR(_namespace_str);
  Py_CLEAR(str_class_name);
  Py_CLEAR(_module_str);
  Py_CLEAR(mdl_mirrored_names);

  return 0;
}


// The end.
