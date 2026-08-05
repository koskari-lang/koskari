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


#ifndef KOSKARI_NAMESPACE_H
#define KOSKARI_NAMESPACE_H


#include <Python.h>
#include <stdint.h>


/* === NamespaceSpec === */


extern PyTypeObject KosNamespaceSpecType;


typedef struct KosNamespaceSpec {
  PyObject_HEAD

  PyObject *ns_name;
  PyObject *local_syms;
  PyObject *closure_syms;
  PyObject *global_syms;

  /* Cached at construction time, shared across all calls via incref. */
  PyObject *fast_names;  // tuple of local+closure+global syms (capped at FAST_SLOT_MAX)
  uint64_t fast_raw_lo;  // initial raw-flag bits: locals 0-63 are set
  uint64_t fast_raw_hi;  // initial raw-flag bits: locals 64-127 are set
} KosNamespaceSpec;


PyObject *KosNamespaceSpec_New(
    PyObject *name,
    PyObject *local_syms,
    PyObject *closure_syms,
    PyObject *global_syms);


#define KosNamespaceSpec_NAME(nspec)            \
  (((KosNamespaceSpec *) nspec)->ns_name)

#define KosNamespaceSpec_LOCAL_SYMS(nspec)      \
  (((KosNamespaceSpec *) nspec)->local_syms)

#define KosNamespaceSpec_CLOSURE_SYMS(nspec)    \
  (((KosNamespaceSpec *) nspec)->closure_syms)

#define KosNamespaceSpec_GLOBAL_SYMS(nspec)     \
  (((KosNamespaceSpec *) nspec)->global_syms)

#define KosNamespaceSpec_FAST_NAMES(nspec)      \
  (((KosNamespaceSpec *) nspec)->fast_names)

#define KosNamespaceSpec_FAST_RAW_LO(nspec)     \
  (((KosNamespaceSpec *) nspec)->fast_raw_lo)

#define KosNamespaceSpec_FAST_RAW_HI(nspec)     \
  (((KosNamespaceSpec *) nspec)->fast_raw_hi)

#define KosNamespaceSpec_CheckExact(obj)                \
  ((obj) && ((obj)->ob_type == &KosNamespaceSpecType))


/* === Namespace === */


extern PyTypeObject KosNamespaceType;


/* Maximum number of fast slots per namespace. Two uint64_t fields cover
   bits 0-63 and 64-127 respectively. */
#define KosNamespace_FAST_SLOT_MAX  128

/* Minimum allocation size for freelisted KosNamespace objects.  All
   namespaces with fast_count <= this value share one pool.  Tune by
   reading KOS_TRACE_FREELIST alloc_misses at runtime. */
#ifndef KOS_NS_MIN_SLOTS
#define KOS_NS_MIN_SLOTS 32
#endif

/* Maximum depth of the KosNamespace freelist. */
#define KOS_NS_FREELIST_MAX 256


typedef struct KosNamespace {
  PyObject_VAR_HEAD

  PyObject *name;       // PyUnicodeObject

  PyObject *parent;     // KosNamespace
  PyObject *binds;      // PyDictObject

  size_t fast_count;    // number of fast slots
  PyObject *fast_names; // PyTupleObject of slot names, shared across copies
  uint64_t fast_raw_lo; // raw-flag bits for slots 0-63
  uint64_t fast_raw_hi; // raw-flag bits for slots 64-127
  PyObject *fast[0];    // slot values: raw value (if raw bit set) or KosRef
} KosNamespace;


PyObject *KosNamespace_New(
    PyObject *parent,
    PyObject *spec);


PyObject *KosNamespace_NewFull(
    PyObject *name,
    PyObject *parent,
    PyObject *local_syms,
    PyObject *closure_syms,
    PyObject *global_syms);


/**
   Build a namespace from the given NamespaceSpec and parent.
   Same as NSPEC.get_namespace(parent). New reference or NULL on error.
*/
PyObject *KosNamespace_FromSpec(PyObject *nspec, PyObject *parent);


/**
   Copy the top-level namespace only. Does not copy the parent chain.
   Returns a new namespace with the same shape and closure refs; local
   slots are left empty. New reference or NULL on error.
*/
PyObject *KosNamespace_Copy(PyObject *template_ns);


/**
   Looks up Ref matching key.

   Borrowed reference, or NULL with error set
 */
PyObject *KosNamespace_Ref(PyObject *ns, PyObject *key);


/**
   Looks up Ref matching key.

   Borrowed reference, or NULL. Does not set error if no match is found,
   but may set an error if some other failure occured in the lookup.
 */
PyObject *KosNamespace_REF(PyObject *ns, PyObject *key);


/**
   Looks up Ref matching key.

   Borrowed reference, Py_None, or NULL. Does not set error if no
   match is found, but may set an error if some other failure occurred
   in the lookup.

   The Py_None value is used as a sentinel to indicate that the
   namespace has the binding, but it is in fast raw mode. The
   binding's value will be lazily wrapped in a KosSymbolicRef instance
   if accessed via KosNamespace_REF, KosNamespace_Ref, or
   KosNamespace_FastRef
 */
PyObject *KosNamespace_LOOSE_REF(PyObject *ns, PyObject *key);


/**
   Looks up Ref matching key. If it exists deref it.

   New reference, or NULL with error set
*/
PyObject *KosNamespace_Deref(PyObject *ns, PyObject *key);


/**
   Looks up Ref matching key. If not found in ns, check the parent of
   ns, and so forth.

   If a match is discovered, a borrowed reference is stored in the found
   pointer. Otherwise it is left unchanged.

   returns -1 if an error occurs, 0 otherwise.
 */
int KosNamespace_FindRef(PyObject *ns, PyObject *key, PyObject **found);


/**
   Test whether a Ref with the matching key is in this namespace.

   0 if key is not in ns, 1 if it is.
 */
int KosNamespace_Contains(PyObject *ns, PyObject *key);


int KosNamespace_Store(PyObject *ns, PyObject *key, PyObject *val);


/**
   Store every name:value in dict into the namespace's declared
   bindings, exactly as per-name Store would: raw fast slots take a
   direct write, celled bindings store through their Ref, and a name
   with no declared binding raises NameError. Not transactional: on
   error, entries already stored remain. Returns 0, or -1 with an
   exception set.
 */
int KosNamespace_StoreDict(PyObject *ns, PyObject *dict);


/**
   Vectorcall-style form of StoreDict: names is a tuple[str] and
   values a borrowed value array aligned to it (a stack segment works
   as-is). Same semantics per entry, in names order.
 */
int KosNamespace_StoreVector(
    PyObject *ns,
    PyObject *names,
    PyObject *const *values);


int KosNamespace_Clear(PyObject *ns, PyObject *key);


/**
   Binds a name to a reference in this namespace
 */
int KosNamespace_Bind(PyObject *ns, PyObject *key, PyObject *ref);


/**
   find the fast index of a reference based on its key.

   returns index, or -1 if index not found, does not set error
 */
int KosNamespace_FastIndex(PyObject *ns, PyObject *key);


/**
   borrowed reference to a KosRef instance at fast index

   returns NULL and sets err if index is out of bounds
 */
PyObject *KosNamespace_FastRef(PyObject *ns, size_t index);


/**
   new reference to the binding's value

   returns NULL and sets err if index is out of bounds, or if
   reference was empty
 */
PyObject *KosNamespace_FastDeref(PyObject *ns, size_t index);


/**
   stores value ad fast index

   returns 0 for success, or -1 and sets err if index is out of bounds
 */
int KosNamespace_FastStore(
    PyObject *ns,
    size_t index,
    PyObject *value);


/**
   clears a reference at fast index

   returns 0 for success, or -1 and sets err if index is out of bounds
 */
int KosNamespace_FastClear(PyObject *ns, size_t index);


/**
   Deref and clear a fast slot in one step. On raw slots the value
   pointer is stolen without an extra INCREF. Returns a new reference
   to the taken value, or NULL with err set if the slot is empty.
 */
PyObject *KosNamespace_FastTake(PyObject *ns, size_t index);


/**
   replaces reference at fast index with a new reference

   returns 0 for success, or -1 and sets err if index is out of bounds
 */
int KosNamespace_FastBind(
    PyObject *ns,
    size_t index,
    PyObject *ref);


#define KosNamespace_NAME(ns)               \
  (((KosNamespace *) ns)->name)

#define KosNamespace_PARENT(ns)             \
  (((KosNamespace *) ns)->parent)

#define KosNamespace_BINDS(ns)               \
  (((KosNamespace *) ns)->binds)

#define KosNamespace_FAST_COUNT(ns)             \
  (((KosNamespace *) ns)->fast_count)

#define KosNamespace_FAST_NAMES(ns)             \
  (((KosNamespace *) ns)->fast_names)

#define KosNamespace_FAST_RAW_LO(ns)            \
  (((KosNamespace *) ns)->fast_raw_lo)

#define KosNamespace_FAST_RAW_HI(ns)            \
  (((KosNamespace *) ns)->fast_raw_hi)

#define KosNamespace_GET_FAST_VAL(ns, i)        \
  (((KosNamespace *) ns)->fast[(i)])

#define KosNamespace_Check(obj)                                         \
  ((obj) && PyType_IsSubtype((obj)->ob_type, &KosNamespaceType))

#define KosNamespace_CheckExact(obj)                    \
  ((obj) && ((obj)->ob_type == &KosNamespaceType))


/**
   Reveal a namespace to the cyclic collector: GC-track it and every
   untracked ancestor.

   Namespaces are GC-allocated but born untracked, keeping the call
   fast path invisible to the collector. Any operation that lets a
   namespace reference escape into storage (LOCALS, function creation
   capturing a parent, continuation capture, Python exposure) must
   reveal it first, since a stored reference is what makes a reference
   cycle through the namespace possible.

   Invariant: a tracked namespace's parent chain is tracked, so the
   walk stops at the first tracked ancestor. Idempotent; safe on NULL.
*/
void KosNamespace_TrackChain(PyObject *ns);


/**
   KosNamespace_TrackChain plus a new reference to ns, ready for
   storage. Safe on NULL (returns NULL).
*/
PyObject *KosNamespace_Reveal(PyObject *ns);


/* === CollectorNamespace === */


extern PyTypeObject KosCollectorNamespaceType;


typedef struct KosCollectorNamespace {
  KosNamespace super;

} KosCollectorNamespace;


/**
  A KosCollectorNamespace acts just like a Namespace except that it
  will not give up its refs via KosNamespace_FindRef, preventing
  child namespaces from taking closures over its references. The most
  common use case is in the collection of members for a class
  definition, where we do not want the class members to have closures
  over each other, as that behavior conflicts heavily with the Python
  class model of members.
*/
PyObject *KosCollectorNamespace_New(
    PyObject *parent,
    PyObject *spec);


PyObject *KosCollectorNamespace_NewFull(
    PyObject *name,
    PyObject *parent,
    PyObject *local_syms,
    PyObject *closure_syms,
    PyObject *global_syms);


#define KosCollectorNamespace_Check(obj)                                \
  ((obj) && PyType_IsSubtype((obj)->ob_type, &KosCollectorNamespaceType))

#define KosCollectorNamespace_CheckExact(obj)                   \
  ((obj) && ((obj)->ob_type == &KosCollectorNamespaceType))

#define KosNamespace_SupportsFast(ns)                                   \
  (KosNamespace_CheckExact(ns) || KosCollectorNamespace_CheckExact(ns))


/* === Raw-bit helpers (available to all includers) === */


static inline int kos_ns_is_raw(PyObject *ns, size_t idx) {
  if (idx < 64)
    return (int) ((KosNamespace_FAST_RAW_LO(ns) >> idx) & 1ULL);
  else
    return (int) ((KosNamespace_FAST_RAW_HI(ns) >> (idx - 64)) & 1ULL);
}


static inline void kos_ns_set_raw(PyObject *ns, size_t idx, int raw) {
  uint64_t bit;
  if (idx < 64) {
    bit = 1ULL << idx;
    if (raw)
      KosNamespace_FAST_RAW_LO(ns) |= bit;
    else
      KosNamespace_FAST_RAW_LO(ns) &= ~bit;
  } else {
    bit = 1ULL << (idx - 64);
    if (raw)
      KosNamespace_FAST_RAW_HI(ns) |= bit;
    else
      KosNamespace_FAST_RAW_HI(ns) &= ~bit;
  }
}


/* === Opcode-only fast inline helpers === */
/*
   The following inlines are ONLY for use in opcode handlers where the
   compiler guarantees the namespace type and slot index are valid.
   Do NOT call them from public API functions or namespace.c internals.
*/

#include "ref.h"
#include "hints.h"


/* Out-of-line cold path for kos_ns_fast_deref: raise NameError for an empty
   raw slot.  Always returns NULL.  Kept out of line so the error string and
   formatting do not bloat every FAST-family handler in the dispatch loop. */
PyObject *kos_ns_deref_empty_raw(PyObject *ns, size_t idx);


static inline PyObject *kos_ns_fast_deref(PyObject *ns, size_t idx) {
  PyObject *slot = KosNamespace_GET_FAST_VAL(ns, idx);
  if (kos_ns_is_raw(ns, idx)) {
    if (likely(slot != NULL)) {
      Py_INCREF(slot);
      return slot;
    }
    return kos_ns_deref_empty_raw(ns, idx);
  }
  return KosRef_Deref(slot);
}


static inline int kos_ns_fast_store(PyObject *ns, size_t idx, PyObject *val) {
  if (kos_ns_is_raw(ns, idx)) {
    Py_INCREF(val);
    Py_XDECREF(KosNamespace_GET_FAST_VAL(ns, idx));
    KosNamespace_GET_FAST_VAL(ns, idx) = val;
    return 0;
  } else {
    return KosRef_Store(KosNamespace_GET_FAST_VAL(ns, idx), val);
  }
}


/* === ModuleNamespace === */


extern PyTypeObject KosModuleNamespaceType;


typedef struct KosModuleNamespace {
  KosNamespace super;
  PyObject *builtins;  // PyDictObject
} KosModuleNamespace;


PyObject *KosModuleNamespace_New(
    PyObject *name,
    PyObject *builtins);


#define KosModuleNamespace_BUILTINS(mdl)        \
  (((KosModuleNamespace *) mdl)->builtins)

#define KosModuleNamespace_CheckExact(obj)     \
  ((obj) && ((obj)->ob_type == &KosModuleNamespaceType))


/* === Module === */


extern PyTypeObject KosModuleType;


typedef struct KosModule {
  PyObject_VAR_HEAD

  PyObject *ns;         // KosNamespace
} KosModule;


/**
  A Module is an object whose attributes are backed by lookups in a
  KosNamespace instance (including KosModuleNamespace)

  A Module has one additional attribute, __dict__, which is the
  underlying KosNamespace
 */
PyObject *KosModule_New(PyObject *namespace);


#define KosModule_NAMESPACE(mdl)                \
  (((KosModule *) mdl)->ns)

#define KosModule_CheckExact(obj)     \
  ((obj) && ((obj)->ob_type == &KosModuleType))


/* === Unit lifecycle === */


int koskari_namespace_init(PyObject *mod);
int koskari_namespace_clear(PyObject *mod);


#endif


// The end.
