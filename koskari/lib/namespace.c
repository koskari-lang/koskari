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

   author: Christopher O'Brien <obriencj@gmail.com>
   license: GPL v.3
*/


#include "lib.h"
#include "debug.h"
#include "tracing.h"


/* === String constants === */


static PyObject *_namespace_str = NULL;  // "<namespace>"
static PyObject *_module_str = NULL;     // "<module>"


// forward declarations
static PyObject *mdl_ns_borrow_ref(PyObject *self, PyObject *name);
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
   parent are NULL/zero. The caller must initialise all header fields. */
static PyObject *ns_alloc(PyTypeObject *type, size_t count) {

  if (type != &KosNamespaceType || count > (size_t) KOS_NS_MIN_SLOTS) {
    PyObject *p = PyObject_NewVar(PyObject, type, (Py_ssize_t) count);
    if (! p)
      return NULL;

    if (count > 0)
      memset(((KosNamespace *) p)->fast, 0, count * sizeof(PyObject *));

    return p;
  }

  if (ns_freelist) {
    KosNamespace *ns = ns_freelist;
    ns_freelist = (KosNamespace *) KosNamespace_PARENT(ns);
    ns_freecount--;

    KosNamespace_PARENT(ns) = NULL;
    KOS_TRACE_FL_ALLOC("namespace", 1);

    PyObject *p = (PyObject *) ns;
    Py_SET_REFCNT(p, 1);
    // Py_SET_TYPE(p, type);
    // Py_SET_SIZE(p, (Py_ssize_t) KOS_NS_MIN_SLOTS);
    return p;
  }

  KOS_TRACE_FL_ALLOC("namespace", 0);
  PyObject *p = PyObject_NewVar(PyObject, type, (Py_ssize_t) KOS_NS_MIN_SLOTS);
  if (! p)
    return NULL;

  memset(((KosNamespace *) p)->fast, 0,
         (size_t) KOS_NS_MIN_SLOTS * sizeof(PyObject *));
  return p;
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

  PyObject *binds = PyDict_New();
  if (! binds)
    return NULL;

  PyObject *self = ns_alloc(&KosNamespaceType, count);
  if (! self) {
    Py_DECREF(binds);
    return NULL;
  }

  Py_INCREF(KosNamespace_NAME(template_ns));
  KosNamespace_NAME(self) = KosNamespace_NAME(template_ns);

  Py_XINCREF(parent);
  KosNamespace_PARENT(self) = parent;

  KosNamespace_BINDS(self) = binds;
  KosNamespace_FAST_COUNT(self) = count;

  // Share the immutable names tuple from the template (just an incref).
  PyObject *fast_names = KosNamespace_FAST_NAMES(template_ns);
  Py_XINCREF(fast_names);
  KosNamespace_FAST_NAMES(self) = fast_names;

  // Copy raw-flag bitfields wholesale.
  KosNamespace_FAST_RAW_LO(self) = KosNamespace_FAST_RAW_LO(template_ns);
  KosNamespace_FAST_RAW_HI(self) = KosNamespace_FAST_RAW_HI(template_ns);

  for (size_t i = 0; i < count; i++) {
    PyObject *ref_name = fast_names? PyTuple_GET_ITEM(fast_names, (Py_ssize_t) i): NULL;

    if (! ref_name) {
      SAYF("[KosNamespace_Copy] slot %zu: ref_name is NULL", (unsigned long) i);
      goto fail;
    }

    SAYF("[KosNamespace_Copy] slot %zu: name=%S raw=%d",
         (unsigned long) i, ref_name, kos_ns_is_raw(template_ns, i));

    if (kos_ns_is_raw(template_ns, i)) {
      // Raw local slot — no dict entry. ns_borrow_loose_ref synthesizes
      // Py_None for these when they are queried by name.
      KosNamespace_GET_FAST_VAL(self, i) = NULL;

    } else {
      PyObject *ref = KosNamespace_GET_FAST_VAL(template_ns, i);
      if (! ref) {
        SAYF("[KosNamespace_Copy] slot %zu: closure ref is NULL",
             (unsigned long) i);
        goto fail;
      }

      Py_INCREF(ref);
      KosNamespace_GET_FAST_VAL(self, i) = ref;
      if (PyDict_SetItem(binds, ref_name, ref) < 0) {
        Py_DECREF(ref);
        goto fail;
      }

      SAYF("[KosNamespace_Copy] slot %zu: ref=%R", (unsigned long) i, ref);
    }
  }

  SAYF("[KosNamespace_Copy] done self=%R", self);
  return self;

 fail:
  SAY("[KosNamespace_Copy] fail");
  Py_DECREF(self);
  return NULL;
}


static PyObject *nspec_m_namespace(PyObject *self, PyObject *parent) {

  return KosNamespace_FromSpec(self, parent);
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

  Py_TYPE(self)->tp_free(self);
}


static int nspec_traverse(PyObject *self, visitproc visit, void *arg) {
  Py_VISIT(KosNamespaceSpec_NAME(self));
  Py_VISIT(KosNamespaceSpec_LOCAL_SYMS(self));
  Py_VISIT(KosNamespaceSpec_CLOSURE_SYMS(self));
  Py_VISIT(KosNamespaceSpec_GLOBAL_SYMS(self));
  Py_VISIT(KosNamespaceSpec_FAST_NAMES(self));
  return 0;
}


static int nspec_clear(PyObject *self) {
  Py_CLEAR(KosNamespaceSpec_NAME(self));
  Py_CLEAR(KosNamespaceSpec_LOCAL_SYMS(self));
  Py_CLEAR(KosNamespaceSpec_CLOSURE_SYMS(self));
  Py_CLEAR(KosNamespaceSpec_GLOBAL_SYMS(self));
  Py_CLEAR(KosNamespaceSpec_FAST_NAMES(self));
  return 0;
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
  .tp_traverse = nspec_traverse,
  .tp_clear = nspec_clear,
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
  KosNamespaceSpec_FAST_RAW_LO(self) = 0;
  KosNamespaceSpec_FAST_RAW_HI(self) = 0;

  // Build cached fast_names tuple (local+closure+global, capped at FAST_SLOT_MAX).
  Py_ssize_t n_loc = PyTuple_GET_SIZE(local_syms);
  Py_ssize_t n_clo = PyTuple_GET_SIZE(closure_syms);
  Py_ssize_t n_glo = PyTuple_GET_SIZE(global_syms);
  Py_ssize_t total = n_loc + n_clo + n_glo;
  Py_ssize_t fn_count = (total > (Py_ssize_t) KosNamespace_FAST_SLOT_MAX)
                        ? (Py_ssize_t) KosNamespace_FAST_SLOT_MAX
                        : total;

  PyObject *fast_names = PyTuple_New(fn_count);
  if (! fast_names) {
    Py_DECREF(self);
    return NULL;
  }

  Py_ssize_t fi = 0;
  for (; fi < fn_count && fi < n_loc; fi++) {
    PyObject *sym = PyTuple_GET_ITEM(local_syms, fi);
    Py_INCREF(sym);
    PyTuple_SET_ITEM(fast_names, fi, sym);
  }

  for (Py_ssize_t ci = 0; fi < fn_count && ci < n_clo; fi++, ci++) {
    PyObject *sym = PyTuple_GET_ITEM(closure_syms, ci);
    Py_INCREF(sym);
    PyTuple_SET_ITEM(fast_names, fi, sym);
  }

  for (Py_ssize_t gi = 0; fi < fn_count && gi < n_glo; fi++, gi++) {
    PyObject *sym = PyTuple_GET_ITEM(global_syms, gi);
    Py_INCREF(sym);
    PyTuple_SET_ITEM(fast_names, fi, sym);
  }
  KosNamespaceSpec_FAST_NAMES(self) = fast_names;

  // Precompute raw-flag bitmask: locals occupy raw slots, closures/globals
  // do not. Bits 0..(n_fast_local-1) are set.
  Py_ssize_t n_fast_local = (n_loc < fn_count) ? n_loc : fn_count;
  uint64_t raw_lo = 0, raw_hi = 0;
  if (n_fast_local > 0) {
    if (n_fast_local <= 64) {
      raw_lo = (n_fast_local < 64)
               ? (1ULL << (uint64_t) n_fast_local) - 1ULL
               : ~0ULL;
    } else {
      raw_lo = ~0ULL;
      uint64_t hi = (uint64_t) (n_fast_local - 64);
      raw_hi = (hi < 64) ? (1ULL << hi) - 1ULL : ~0ULL;
    }
  }
  KosNamespaceSpec_FAST_RAW_LO(self) = raw_lo;
  KosNamespaceSpec_FAST_RAW_HI(self) = raw_hi;

  // SAYF("KosNamespaceSpec_New -> %R", self);
  return self;
}


/* === Namespace === */


static PyObject *ns_borrow_loose_ref(PyObject *namespace, PyObject *key) {
  PyObject *ref = PyDict_GetItemWithError(KosNamespace_BINDS(namespace), key);
  if (ref || PyErr_Occurred())
    return ref;

  // Key absent from dict: synthesize Py_None sentinel for raw fast slots.
  int index = ns_fast_index(namespace, key);
  if (index >= 0 && kos_ns_is_raw(namespace, (size_t) index))
    return Py_None;
  return NULL;
}


static PyObject *ns_borrow_ref(PyObject *namespace, PyObject *key) {
  PyObject *binds = KosNamespace_BINDS(namespace);
  PyObject *ref = PyDict_GetItemWithError(binds, key);

  if (! ref) {
    if (PyErr_Occurred())
      return NULL;

    // Key absent from dict: check for a raw fast slot to promote lazily.
    int index = ns_fast_index(namespace, key);
    if (index < 0 || ! kos_ns_is_raw(namespace, (size_t) index))
      return NULL;

    SAYF("converting binding %S from raw to reference", key);

    // Create ref with NULL value first so the error path never touches
    // the slot's raw value pointer.
    ref = KosSymbolicRef_New(key, NULL);
    if (! ref)
      return NULL;

    if (PyDict_SetItem(binds, key, ref)) {
      Py_DECREF(ref);  // safe: ref->value is NULL
      return NULL;
    }

    // Dict holds one reference (SetItem). Steal raw value into ref and
    // donate our New reference to the slot.
    KosSymbolicRef_VALUE(ref) = KosNamespace_GET_FAST_VAL(namespace,
                                                           (size_t) index);
    KosNamespace_GET_FAST_VAL(namespace, (size_t) index) = ref;
    kos_ns_set_raw(namespace, (size_t) index, 0);
    // ref.rc == 2: dict (SetItem) + slot (New). No decref.
  }

  // NULL or a borrowed reference to a Ref instance
  return ref;
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

  PyObject *binds = KosNamespace_BINDS(self);

  // Borrow cached fast_names and bitmasks from the nspec (just an incref).
  PyObject *fn = KosNamespaceSpec_FAST_NAMES(nspec);
  Py_XINCREF(fn);
  KosNamespace_FAST_NAMES(self) = fn;
  KosNamespace_FAST_RAW_LO(self) = KosNamespaceSpec_FAST_RAW_LO(nspec);
  KosNamespace_FAST_RAW_HI(self) = KosNamespaceSpec_FAST_RAW_HI(nspec);

  // Local slots are already NULL (zeroed before this call) and their raw bits
  // are set in the bitmask above.  No dict entries for locals (sentinel-free).
  PyObject *local_syms = KosNamespaceSpec_LOCAL_SYMS(nspec);
  size_t count = local_syms ? (size_t) PyTuple_GET_SIZE(local_syms) : 0;

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

      if (PyDict_SetItem(binds, ref_name, ref) < 0)
        return -1;

      Py_INCREF(ref);
      KosNamespace_GET_FAST_VAL(self, count) = ref;
      count++;
    }
  }

  // Globals: find ref in module namespace and bind in dict + fast slot.
  // The global slots are already not-raw in the precomputed bitmask.
  PyObject *global_syms = KosNamespaceSpec_GLOBAL_SYMS(nspec);
  if (global_syms && PyTuple_GET_SIZE(global_syms) > 0 && parent) {
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

      if (PyDict_SetItem(binds, ref_name, ref) < 0)
        return -1;

      Py_INCREF(ref);
      KosNamespace_GET_FAST_VAL(self, count) = ref;
      count++;
    }
  }

  return 0;
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

  PyObject *binds = PyDict_New();
  if (! binds)
    return NULL;

  PyObject *self = ns_alloc(type, count);
  if (! self) {
    Py_DECREF(binds);
    return NULL;
  }

  Py_INCREF(name);
  KosNamespace_NAME(self) = name;
  Py_XINCREF(parent);
  KosNamespace_PARENT(self) = parent;
  KosNamespace_BINDS(self) = binds;
  KosNamespace_FAST_COUNT(self) = count;
  KosNamespace_FAST_NAMES(self) = NULL;
  KosNamespace_FAST_RAW_LO(self) = 0;
  KosNamespace_FAST_RAW_HI(self) = 0;

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

  if (global_syms && PyTuple_GET_SIZE(global_syms) > 0 && parent) {
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


static Py_ssize_t ns_len(PyObject *self) {
  // fast_count is the authoritative binding count for fast namespaces;
  // the binds dict omits raw-slot entries (no Py_None sentinels).
  return (Py_ssize_t) KosNamespace_FAST_COUNT(self);
}


static int ns_fast_index(PyObject *self, PyObject *key) {
  PyObject *names = KosNamespace_FAST_NAMES(self);
  if (! names)
    return -1;
  Py_ssize_t count = PyTuple_GET_SIZE(names);
  for (Py_ssize_t index = 0; index < count; index++) {
    PyObject *name = PyTuple_GET_ITEM(names, index);
    if (name && Py_EQUALS(key, name))
      return (int) index;
  }
  return -1;
}


static PyObject *ns_deref_symbol(PyObject *self, PyObject *key) {

  PyObject *ref = KosNamespace_LOOSE_REF(self, key);

  if (! ref) {
    if (! PyErr_Occurred())
      PyErr_Format(PyExc_NameError, "No such reference: %S", key);
    return NULL;

  } else if (ref == Py_None) {
    int index = ns_fast_index(self, key);
    if (index < 0) {
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

  } else {
    return KosRef_Deref(ref);
  }
}


static int ns_store_symbol(PyObject *self, PyObject *key, PyObject *val) {

  PyObject *ref = KosNamespace_LOOSE_REF(self, key);

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

  } else if (ref == Py_None) {
    int index = ns_fast_index(self, key);
    if (index < 0) {
      PyErr_Format(PyExc_NameError, "No such reference: %S", key);
      return -1;
    }

    Py_XINCREF(val);
    Py_XDECREF(KosNamespace_GET_FAST_VAL(self, (size_t) index));
    KosNamespace_GET_FAST_VAL(self, (size_t) index) = val;
    // raw bit is already 1 when dict entry is Py_None; reaffirm it
    kos_ns_set_raw(self, (size_t) index, 1);

    return 0;

  } else {
    return val? KosRef_Store(ref, val): KosRef_Clear(ref);
  }
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
  if (fast >= 0) {
    Py_INCREF(ref);
    Py_XDECREF(KosNamespace_GET_FAST_VAL(self, (size_t) fast));
    KosNamespace_GET_FAST_VAL(self, (size_t) fast) = ref;
    kos_ns_set_raw(self, (size_t) fast, 0);
  }

  fast = PyDict_SetItem(KosNamespace_BINDS(self), key, ref);
  SAYF("ns_bind_ref -> %i", fast);
  return fast;
}


static PyMappingMethods ns_as_mapping = {
  .mp_length = ns_len,
  .mp_subscript = ns_deref_symbol,
  .mp_ass_subscript = ns_store_symbol,
};


static PyObject *ns_m_ref(PyObject *self, PyObject *key) {

  PyObject *result = KosNamespace_REF(self, key);
  if (! result && ! PyErr_Occurred())
    PyErr_Format(PyExc_NameError, "no such reference: %R", key);

  Py_XINCREF(result);
  return result;
}


static int ns_contains(PyObject *self, PyObject *key) {
  PyObject *const ref = KosNamespace_REF(self, key);
  if (! ref && PyErr_Occurred()) {
    return -1;
  } else {
    return !!ref;
  }
}


static PySequenceMethods ns_seq = {
  .sq_contains = ns_contains,
};


static PyObject *ns_m_deref(PyObject *self, PyObject *key) {
  return ns_deref_symbol(self, key);
}


static PyObject *ns_m_get(PyObject *self, PyObject *args) {
  PyObject *name = NULL;
  PyObject *def = Py_None;

  if (! PyArg_UnpackTuple(args, "get", 1, 2, &name, &def))
    return NULL;

  while (self) {
    // Use LOOSE_REF so raw fast slots (no dict entry) are also found.
    PyObject *const bound = KosNamespace_LOOSE_REF(self, name);
    if (PyErr_Occurred())
      return NULL;
    if (bound) {
      PyObject *val = ns_deref_symbol(self, name);
      if (! val) {
        PyErr_Clear();
        Py_INCREF(def);
        return def;
      }
      return val;
    }

    self = KosNamespace_PARENT(self);
  }

  Py_INCREF(def);
  return def;
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

  if (ns_bind_ref(self, key, ref))
    return NULL;

  Py_RETURN_NONE;
}


/*
static PyObject *ns_m_reset(PyObject *self) {
  if (! KosNamespace_Reset(self))
    return NULL;

  Py_RETURN_NONE;
}
*/


/*
static PyObject *ns_m_get_name(PyObject *self) {
  PyObject *name = KosNamespace_NAME(self);
  if (name) {
    Py_XINCREF(name);
    return name;

  } else {
    Py_RETURN_NONE;
  }
}


static PyObject *ns_m_get_parent(PyObject *self) {
  PyObject *parent = KosNamespace_PARENT(self);
  if (parent) {
    Py_XINCREF(parent);
    return parent;

  } else {
    Py_RETURN_NONE;
  }
}
*/


static PyObject *ns_m_contains(PyObject *self, PyObject *key) {
  return PyLong_FromLong(KosNamespace_Contains(self, key));
}


static PyObject *ns_m_raw(PyObject *self, PyObject *key) {
  PyObject *const ref = KosNamespace_LOOSE_REF(self, key);
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
  return PyDict_Keys(KosNamespace_BINDS(self));
}


static PyObject *ns_m_items(PyObject *self) {
  return PyDict_Items(KosNamespace_BINDS(self));
}


/* keys() for fast namespaces: returns all bound names including raw-slot
   locals that are absent from the binds dict. */
static PyObject *ns_m_keys_fast(PyObject *self) {
  PyObject *names = KosNamespace_FAST_NAMES(self);
  if (names)
    return PySequence_List(names);
  return PyList_New(0);
}


/* items() for fast namespaces: returns (name, value) pairs for all fast
   slots, skipping any that are currently uninitialized. */
static PyObject *ns_m_items_fast(PyObject *self) {
  PyObject *names = KosNamespace_FAST_NAMES(self);
  Py_ssize_t count = names ? PyTuple_GET_SIZE(names) : 0;
  PyObject *items = PyList_New(0);
  if (! items)
    return NULL;
  for (Py_ssize_t i = 0; i < count; i++) {
    PyObject *key = PyTuple_GET_ITEM(names, i);
    PyObject *val = ns_deref_symbol(self, key);
    if (! val) {
      if (PyErr_ExceptionMatches(PyExc_NameError)) {
        PyErr_Clear();
        continue;
      }
      Py_DECREF(items);
      return NULL;
    }
    PyObject *pair = PyTuple_Pack(2, key, val);
    Py_DECREF(val);
    if (! pair || PyList_Append(items, pair) < 0) {
      Py_XDECREF(pair);
      Py_DECREF(items);
      return NULL;
    }
    Py_DECREF(pair);
  }
  return items;
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
  return KosNamespace_Copy(self);
}


static PyMethodDef ns_methods[] = {
  { "ref", (PyCFunction) ns_m_ref, METH_O,
    "NS.ref(K) -> R\n"
    "returns the reference for a binding in this namespace" },

  { "deref", (PyCFunction) ns_m_deref, METH_O,
    "NS.deref(K) -> V\n"
    "dereferences a binding in this namespace" },

  { "get", (PyCFunction) ns_m_get, METH_VARARGS,
    "NS.get(K[, default]) -> V\n"
    "value from this namespace chain without materializing a ref;"
    " on failure returns default or None without leaving an exception"
    " set" },

  { "store", (PyCFunction) ns_m_store, METH_VARARGS,
    "NS.store(K, V) => None\n"
    "assigns a value to a binding in this namespace" },

  { "clear", (PyCFunction) ns_m_clear, METH_O,
    "NS.clear(K) => None\n"
    "removes the value from a binding in this namespace" },

  { "bind", (PyCFunction) ns_m_bind, METH_VARARGS,
    "NS.bind(K, R) => None\n"
    "sets the reference for a binding in this namespace" },

  { "bound", (PyCFunction) ns_m_contains, METH_O,
    "NS.bound(K) => bool\n"
    "True if the given name has a binding in this namespace" },

  { "raw", (PyCFunction) ns_m_raw, METH_O,
    "NS.raw(K) => bool\n"
    "True if the given binding is currently raw in this namespace" },

  /*
  { "reset", (PyCFunction) ns_m_reset, METH_NOARGS,
    "NS.reset() => None" },
  */

  { "keys", (PyCFunction) ns_m_keys_fast, METH_NOARGS,
    "NS.keys() => list of all bound names including uninitialised locals" },

  { "items", (PyCFunction) ns_m_items_fast, METH_NOARGS,
    "NS.items() => list of (name, value) pairs for initialised bindings" },

  { "__contains__", (PyCFunction) ns_m_contains, METH_O,
    "(K in NS) => bool" },

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
  { "name", T_OBJECT, offsetof(KosNamespace, name), READONLY,
    "str; namespace name" },

  { "parent", T_OBJECT, offsetof(KosNamespace, parent), READONLY,
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
                 " not %R", name);
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

  if (count > KosNamespace_FAST_SLOT_MAX) {
    PyErr_Format(PyExc_ValueError,
                 "namespace fast slot count %zu exceeds maximum %d",
                 count, KosNamespace_FAST_SLOT_MAX);
    Py_XDECREF(local_syms);
    Py_XDECREF(closure_syms);
    Py_XDECREF(global_syms);
    return NULL;
  }

  PyObject *binds = PyDict_New();
  if (! binds) {
    Py_XDECREF(local_syms);
    Py_XDECREF(closure_syms);
    Py_XDECREF(global_syms);
    return NULL;
  }

  PyObject *self = PyObject_NewVar(PyObject, type, count);
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

  KosNamespace_FAST_COUNT(self) = count;
  KosNamespace_FAST_NAMES(self) = NULL;
  KosNamespace_FAST_RAW_LO(self) = 0;
  KosNamespace_FAST_RAW_HI(self) = 0;

  for (size_t i = count; i--; )
    KosNamespace_GET_FAST_VAL(self, i) = NULL;

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

  return ns_newfull_type(type, name, parent, local_syms, closure_syms,
                         global_syms);
}


static void ns_dealloc(PyObject *self) {
  // SAYF("ns_dealloc(%R)", self);

  Py_CLEAR(KosNamespace_NAME(self));
  Py_CLEAR(KosNamespace_PARENT(self));
  Py_CLEAR(KosNamespace_BINDS(self));
  Py_CLEAR(KosNamespace_FAST_NAMES(self));

  for (size_t i = KosNamespace_FAST_COUNT(self); i--; )
    Py_CLEAR(KosNamespace_GET_FAST_VAL(self, i));
  KosNamespace_FAST_COUNT(self) = 0;

  // check if the namespace is valid for the freelist.
  if (Py_TYPE(self) == &KosNamespaceType &&
      Py_SIZE(self) == (Py_ssize_t) KOS_NS_MIN_SLOTS) {

    // if the freelist is not full, add the namespace to the freelist.
    if (ns_freecount < KOS_NS_FREELIST_MAX) {
      KosNamespace_FAST_RAW_LO(self) = 0;
      KosNamespace_FAST_RAW_HI(self) = 0;
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
  Py_VISIT(KosNamespace_NAME(self));
  Py_VISIT(KosNamespace_PARENT(self));
  Py_VISIT(KosNamespace_BINDS(self));
  Py_VISIT(KosNamespace_FAST_NAMES(self));

  for (size_t i = KosNamespace_FAST_COUNT(self); i--; )
    Py_VISIT(KosNamespace_GET_FAST_VAL(self, i));

  return 0;
}


static int ns_clear(PyObject *self) {
  Py_CLEAR(KosNamespace_NAME(self));
  Py_CLEAR(KosNamespace_PARENT(self));
  Py_CLEAR(KosNamespace_BINDS(self));
  Py_CLEAR(KosNamespace_FAST_NAMES(self));

  for (size_t i = KosNamespace_FAST_COUNT(self); i--; )
    Py_CLEAR(KosNamespace_GET_FAST_VAL(self, i));
  KosNamespace_FAST_COUNT(self) = 0;

  return 0;
}


PyTypeObject KosNamespaceType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "Namespace",
  sizeof(KosNamespace),
  sizeof(PyObject *),

  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_methods = ns_methods,
  .tp_members = ns_members,
  .tp_new = ns_new,
  .tp_dealloc = ns_dealloc,
  .tp_traverse = ns_traverse,
  .tp_clear = ns_clear,
  .tp_as_mapping = &ns_as_mapping,
  .tp_as_sequence = &ns_seq,
  // .tp_repr = ns_repr,
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


PyObject *KosNamespace_LOOSE_REF(PyObject *namespace, PyObject *key) {
  if (KosNamespace_CheckExact(namespace) ||
      KosCollectorNamespace_CheckExact(namespace)) {
    return ns_borrow_loose_ref(namespace, key);

  } else if (KosModuleNamespace_CheckExact(namespace)) {
    return mdl_ns_borrow_ref(namespace, key);

  } else {
    return NULL;
  }
}


PyObject *KosNamespace_REF(PyObject *namespace, PyObject *key) {
  if (KosNamespace_CheckExact(namespace) ||
      KosCollectorNamespace_CheckExact(namespace)) {
    return ns_borrow_ref(namespace, key);

  } else if (KosModuleNamespace_CheckExact(namespace)) {
    return mdl_ns_borrow_ref(namespace, key);

  } else {
    return NULL;
  }
}


PyObject *KosNamespace_Ref(PyObject *namespace, PyObject *key) {
  PyObject *result = KosNamespace_REF(namespace, key);

  if (! result && ! PyErr_Occurred())
    PyErr_Format(PyExc_NameError, "no such reference: %R", key);

  return result;
}


PyObject *KosNamespace_Deref(PyObject *namespace, PyObject *key) {
  PyObject *ref = KosNamespace_Ref(namespace, key);
  return ref? KosRef_Deref(ref): NULL;
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

    if (! KosCollectorNamespace_Check(namespace)) {
      // the collectornamespace doesn't allow you to take a closure
      // over its references. The rest can be searched though.

      PyObject *seek = KosNamespace_REF(namespace, key);
      if (! seek) {
        if (PyErr_Occurred())
          return -1;
      } else {
        *found = seek;
        return 0;
      }
    }

    namespace = KosNamespace_PARENT(namespace);
  }

  // SAYF("KosNamespace_FindRef -> %R", result);
  return 0;
}


int KosNamespace_Contains(PyObject *namespace, PyObject *key) {
  if (! KosNamespace_Check(namespace)) {
    PyErr_Format(PyExc_TypeError, "expected Namespace instance,"
                 " found %R", namespace);
    return -1;
  }

  PyObject *const ref = KosNamespace_REF(namespace, key);

  if (! ref && PyErr_Occurred()) {
    return -1;
  } else {
    return !!ref;
  }
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

  // Insert into binds first.  On failure, Py_DECREF(ref) is safe because
  // ref->value is still NULL — no slot reference has been touched.
  if (PyDict_SetItem(KosNamespace_BINDS(namespace), name, ref)) {
    Py_DECREF(ref);
    return -1;
  }

  // Steal the slot's counted reference into ref->value (no incref: the
  // slot's contribution to the raw value's refcount moves to ref->value).
  KosSymbolicRef_VALUE(ref) = KosNamespace_GET_FAST_VAL(namespace, index);

  // Install ref in the slot, donating our New reference to it.
  // ref.rc == 2: dict (from SetItem) + slot (from New).
  KosNamespace_GET_FAST_VAL(namespace, index) = ref;
  kos_ns_set_raw(namespace, index, 0);

  return 0;
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
    Py_XDECREF(KosNamespace_GET_FAST_VAL(namespace, index));
    KosNamespace_GET_FAST_VAL(namespace, index) = value;
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
  Py_XDECREF(KosNamespace_GET_FAST_VAL(namespace, index));
  KosNamespace_GET_FAST_VAL(namespace, index) = ref;
  kos_ns_set_raw(namespace, index, 0);

  return PyDict_SetItem(KosNamespace_BINDS(namespace), name, ref);
}


/* === CollectorNamespace === */


static Py_ssize_t collector_mp_length(PyObject *self) {
  Py_ssize_t count = 0;

  // Raw fast slots are locals treated as declarations.
  PyObject *names = KosNamespace_FAST_NAMES(self);
  Py_ssize_t fn_count = names ? PyTuple_GET_SIZE(names) : 0;
  for (Py_ssize_t i = 0; i < fn_count; i++) {
    if (kos_ns_is_raw(self, (size_t) i))
      count++;
  }

  // Dict entries not in fast_names are dynamic declarations.
  PyObject *binds = KosNamespace_BINDS(self);
  PyObject *key = NULL;
  Py_ssize_t pos = 0;
  while (PyDict_Next(binds, &pos, &key, NULL)) {
    if (ns_fast_index(self, key) < 0)
      count++;
  }

  return count;
}


static PyObject *collector_m_keys(PyObject *self) {

  PyObject *keys = PyList_New(0);
  if (! keys)
    return NULL;

  // Raw fast slots are locals treated as declarations.
  PyObject *names = KosNamespace_FAST_NAMES(self);
  Py_ssize_t fn_count = names ? PyTuple_GET_SIZE(names) : 0;
  for (Py_ssize_t i = 0; i < fn_count; i++) {
    if (! kos_ns_is_raw(self, (size_t) i))
      continue;
    if (PyList_Append(keys, PyTuple_GET_ITEM(names, i)) < 0) {
      Py_DECREF(keys);
      return NULL;
    }
  }

  // Dict entries not in fast_names are dynamic declarations.
  PyObject *binds = KosNamespace_BINDS(self);
  PyObject *key = NULL;
  Py_ssize_t pos = 0;
  while (PyDict_Next(binds, &pos, &key, NULL)) {
    if (ns_fast_index(self, key) >= 0)
      continue;
    if (PyList_Append(keys, key) < 0) {
      Py_DECREF(keys);
      return NULL;
    }
  }

  return keys;
}


static PyObject *collector_m_items(PyObject *self) {

  PyObject *items = PyList_New(0);
  if (! items)
    return NULL;

  // Raw fast slots are locals treated as declarations.
  PyObject *names = KosNamespace_FAST_NAMES(self);
  Py_ssize_t fn_count = names ? PyTuple_GET_SIZE(names) : 0;
  for (Py_ssize_t i = 0; i < fn_count; i++) {
    if (! kos_ns_is_raw(self, (size_t) i))
      continue;
    PyObject *key = PyTuple_GET_ITEM(names, i);
    PyObject *val = ns_deref_symbol(self, key);
    if (! val) {
      if (PyErr_ExceptionMatches(PyExc_NameError)) {
        PyErr_Clear();
        continue;
      }
      Py_DECREF(items);
      return NULL;
    }
    PyObject *pair = PyTuple_Pack(2, key, val);
    Py_DECREF(val);
    if (! pair || PyList_Append(items, pair) < 0) {
      Py_XDECREF(pair);
      Py_DECREF(items);
      return NULL;
    }
    Py_DECREF(pair);
  }

  // Dict entries not in fast_names are dynamic declarations.
  PyObject *binds = KosNamespace_BINDS(self);
  PyObject *key = NULL;
  Py_ssize_t pos = 0;
  while (PyDict_Next(binds, &pos, &key, NULL)) {
    if (ns_fast_index(self, key) >= 0)
      continue;
    PyObject *val = ns_deref_symbol(self, key);
    if (! val) {
      Py_DECREF(items);
      return NULL;
    }
    PyObject *pair = PyTuple_Pack(2, key, val);
    Py_DECREF(val);
    if (! pair || PyList_Append(items, pair) < 0) {
      Py_XDECREF(pair);
      Py_DECREF(items);
      return NULL;
    }
    Py_DECREF(pair);
  }

  return items;
}


static PyMappingMethods collector_as_mapping = {
  .mp_length = collector_mp_length,
  .mp_subscript = ns_deref_symbol,
  .mp_ass_subscript = ns_store_symbol,
};


static PyMethodDef collector_methods[] = {
  { "ref", (PyCFunction) ns_m_ref, METH_O,
    "NS.ref(K) -> R\n"
    "returns the reference for a binding in this namespace" },

  { "deref", (PyCFunction) ns_m_deref, METH_O,
    "NS.deref(K) -> V\n"
    "dereferences a binding in this namespace" },

  { "get", (PyCFunction) ns_m_get, METH_VARARGS,
    "NS.get(K[, default]) -> V\n"
    "value from this namespace chain without materializing a ref;"
    " on failure returns default or None without leaving an exception"
    " set" },

  { "store", (PyCFunction) ns_m_store, METH_VARARGS,
    "NS.store(K, V) => None\n"
    "assigns a value to a binding in this namespace" },

  { "clear", (PyCFunction) ns_m_clear, METH_O,
    "NS.clear(K) => None\n"
    "removes the value from a binding in this namespace" },

  { "bind", (PyCFunction) ns_m_bind, METH_VARARGS,
    "NS.bind(K, R) => None\n"
    "sets the reference for a binding in this namespace" },

  { "bound", (PyCFunction) ns_m_contains, METH_O,
    "NS.bound(K) => bool\n"
    "True if the given name has a binding in this namespace" },

  { "raw", (PyCFunction) ns_m_raw, METH_O,
    "NS.raw(K) => bool\n"
    "True if the given binding is currently raw in this namespace" },

  { "keys", (PyCFunction) collector_m_keys, METH_NOARGS,
    "NS.keys() => list of declaration names only" },

  { "items", (PyCFunction) collector_m_items, METH_NOARGS,
    "NS.items() => list of declaration (name, value) pairs only" },

  { "__contains__", (PyCFunction) ns_m_contains, METH_O,
    "(K in NS) => bool" },

  { "find", (PyCFunction) ns_m_find, METH_O,
    "NS.find(K) => R\n"
    "traverses this namespace and its parents to find and return a"
    " reference, or returns None if no binding is found" },

  { "copy", (PyCFunction) ns_m_copy, METH_NOARGS,
    "NS.copy() => Namespace\n"
    "copy the top-level namespace only (parent chain not copied)" },

  { NULL },
};


PyTypeObject KosCollectorNamespaceType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "CollectorNamespace",
  sizeof(KosCollectorNamespace),
  sizeof(PyObject *),

  .tp_base = &KosNamespaceType,
  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_methods = collector_methods,
  .tp_as_mapping = &collector_as_mapping,
  .tp_as_sequence = &ns_seq,
  // .tp_repr = ns_repr,
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


static PyObject *mdl_ns_borrow_ref(PyObject *self, PyObject *name) {

  // SAYF("mdl_ns_borrow_ref(%R, %R)", self, name);
  PyObject *res = PyDict_GetItem(KosNamespace_BINDS(self), name);

  if (! res) {
    PyObject *blt = KosModuleNamespace_BUILTINS(self);
    PyObject *val = NULL;
    if (blt)
      val = PyDict_GetItem(blt, name);

    res = KosRef_New(name, val);

    PyDict_SetItem(KosNamespace_BINDS(self), name, res);
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


static void mdl_ns_dealloc(PyObject *self) {
  // SAYF("mdl_ns_dealloc(%R)", self);
  Py_CLEAR(KosModuleNamespace_BUILTINS(self));
  ns_dealloc(self);
}


static int mdl_ns_traverse(PyObject *self, visitproc visit, void *arg) {
  Py_VISIT(KosModuleNamespace_BUILTINS(self));
  return ns_traverse(self, visit, arg);
}


static int mdl_ns_clear(PyObject *self) {
  Py_CLEAR(KosModuleNamespace_BUILTINS(self));
  return ns_clear(self);
}


static PyObject *mdl_ns_m_get(PyObject *self, PyObject *args) {
  PyObject *name = NULL;
  PyObject *def = Py_None;
  PyObject *blt = NULL;
  PyObject *val = NULL;

  if (! PyArg_UnpackTuple(args, "get", 1, 2, &name, &def))
    return NULL;

  PyObject *const bound = PyDict_GetItem(KosNamespace_BINDS(self), name);
  if (bound) {
    val = ns_deref_symbol(self, name);
    if (! val) {
      PyErr_Clear();
      Py_INCREF(def);
      return def;
    }
    return val;
  }

  blt = KosModuleNamespace_BUILTINS(self);
  if (! blt) {
    Py_INCREF(def);
    return def;
  }

  val = PyDict_GetItem(blt, name);
  if (! val) {
    Py_INCREF(def);
    return def;
  }

  Py_INCREF(val);
  return val;
}


static PyMethodDef mdl_ns_methods[] = {
  { "ref", (PyCFunction) ns_m_ref, METH_O,
    "NS.ref(K) -> R\n"
    "returns the reference for a binding in this namespace" },

  { "deref", (PyCFunction) ns_m_deref, METH_O,
    "NS.deref(K) -> V\n"
    "dereferences a binding in this namespace" },

  { "store", (PyCFunction) ns_m_store, METH_VARARGS,
    "NS.store(K, V) => None\n"
    "assigns a value to a binding in this namespace" },

  { "clear", (PyCFunction) ns_m_clear, METH_O,
    "NS.clear(K) => None\n"
    "removes the value from a binding in this namespace" },

  { "bind", (PyCFunction) ns_m_bind, METH_VARARGS,
    "NS.bind(K, R) => None\n"
    "sets the reference for a binding in this namespace" },

  { "bound", (PyCFunction) ns_m_contains, METH_O,
    "NS.bound(K) => bool\n"
    "True if the given name has a binding in this namespace" },

  { "raw", (PyCFunction) ns_m_raw, METH_O,
    "NS.raw(K) => bool\n"
    "True if the given binding is currently raw in this namespace" },

  { "keys", (PyCFunction) ns_m_keys, METH_NOARGS,
    "NS.keys() => iter" },

  { "items", (PyCFunction) ns_m_items, METH_NOARGS,
    "NS.items() => iter" },

  { "__contains__", (PyCFunction) ns_m_contains, METH_O,
    "(K in NS) => bool" },

  { "find", (PyCFunction) ns_m_find, METH_O,
    "NS.find(K) => R\n"
    "traverses this namespace and its parents to find and return a"
    " reference, or returns None if no binding is found" },

  { "copy", (PyCFunction) ns_m_copy, METH_NOARGS,
    "NS.copy() => Namespace\n"
    "copy the top-level namespace only (parent chain not copied)" },

  { "get", (PyCFunction) mdl_ns_m_get, METH_VARARGS,
    "NS.get(K[, default]) -> V\n"
    "value from binds or builtins dict without materializing a ref;"
    " on failure returns default or None without leaving an exception"
    " set" },

  { NULL },
};


PyTypeObject KosModuleNamespaceType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "ModuleNamespace",
  sizeof(KosModuleNamespace),
  0,

  .tp_base = &KosNamespaceType,
  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_methods = mdl_ns_methods,
  .tp_new = mdl_ns_new,
  .tp_dealloc = mdl_ns_dealloc,
  .tp_traverse = mdl_ns_traverse,
  .tp_clear = mdl_ns_clear,
  // .tp_repr = ns_repr,
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

  if (! PyDict_Check(builtins)) {
    PyErr_Format(PyExc_TypeError, "ModuleNamespace builtins must be a"
                 " mapping, not %R", builtins);
    return NULL;
  }

  PyObject *binds = PyDict_New();
  if (! binds)
    return NULL;

  PyObject *self = PyObject_New(PyObject, &KosModuleNamespaceType);

  Py_INCREF(name);
  KosNamespace_NAME(self) = name;

  KosNamespace_PARENT(self) = NULL;
  KosNamespace_BINDS(self) = binds;
  KosNamespace_FAST_COUNT(self) = 0;
  KosNamespace_FAST_NAMES(self) = NULL;
  KosNamespace_FAST_RAW_LO(self) = 0;
  KosNamespace_FAST_RAW_HI(self) = 0;

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
  PyObject *ns = KosModule_NAMESPACE(self);
  if (KosNamespace_SupportsFast(ns))
    return ns_m_keys_fast(ns);
  return PyDict_Keys(KosNamespace_BINDS(ns));
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

  PyObject *ref = KosNamespace_REF(KosModule_NAMESPACE(self), name);
  if (! ref || KosRef_IsEmpty(ref)) {
    if (! PyErr_Occurred())
      PyErr_Format(PyExc_AttributeError, "%R object has no attribute %R",
                   self, name);
    return NULL;
  }

  return KosRef_Deref(ref);
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
                   self, value);
      return -1;
    }
  }

  PyObject *ref = KosNamespace_REF(KosModule_NAMESPACE(self), name);
  if (! ref) {
    if (! PyErr_Occurred())
      PyErr_Format(PyExc_AttributeError, "%R object has no attribute %R",
                   self, name);
    return -1;
  }

  return value? KosRef_Store(ref, value): KosRef_Clear(ref);
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

  "Module",
  sizeof(KosModule),
  0,

  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_new = mdl_new,
  .tp_dealloc = mdl_dealloc,
  .tp_traverse = mdl_traverse,
  .tp_clear = mdl_clear,
  .tp_getset = mdl_getset,
  .tp_methods = mdl_methods,
  .tp_getattro = mdl_getattro,
  .tp_setattro = mdl_setattro,
  // .tp_repr = ns_repr,
};


PyObject *KosModule_New(PyObject *namespace) {

  // SAYF("KosModule_New(%R)", namespace);

  if (! KosNamespace_Check(namespace)) {
    PyErr_Format(PyExc_TypeError, "Module's namespace must be a Namespace,"
                 " not %R", namespace);
    return NULL;
  }

  PyObject *self = PyObject_New(PyObject, &KosModuleType);
  if (! self)
    return NULL;

  Py_INCREF(namespace);
  KosModule_NAMESPACE(self) = namespace;

  return self;
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
      PyType_Ready(&KosModuleType))
    return -1;

  STR_CONST(_namespace_str, "<namespace>");
  STR_CONST(_module_str, "<module>");

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
    PyObject_Free(ns);
  }
  ns_freecount = 0;

  Py_CLEAR(_namespace_str);
  Py_CLEAR(_module_str);

  return 0;
}


// The end.
