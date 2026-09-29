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
#include "hints.h"
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
  PyObject *fast_names;      // tuple of local+closure+global syms, every name
  uint64_t fast_raw[2];      // initial raw-flag bits for slots 0-127: locals set
  uint64_t *fast_raw_more;   // bits for slots 128 and up, PyMem-owned, or NULL
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

#define KosNamespaceSpec_FAST_RAW(nspec)        \
  (((KosNamespaceSpec *) nspec)->fast_raw)

#define KosNamespaceSpec_FAST_RAW_MORE(nspec)   \
  (((KosNamespaceSpec *) nspec)->fast_raw_more)

#define KosNamespaceSpec_CheckExact(obj)                \
  ((obj) && ((obj)->ob_type == &KosNamespaceSpecType))


/* === Namespace === */


extern PyTypeObject KosNamespaceType;


/* Fast slots a FAST-style operand can address: the immediates are 16
   bits. Storage itself is unbounded; the compiler keeps a name whose slot
   index would not fit as a VAR access. */
#define KosNamespace_FAST_SLOT_MAX  65536

/* Raw-flag bits held inline in the namespace and spec structs: two words
   cover slots 0-127. Slots past that use fast_raw_more, which a
   namespace keeps in its own allocation right after the slot array. */
#define KOS_NS_RAW_INLINE  128

/* Words of overflow raw-flag storage a namespace of count slots needs */
static inline size_t kos_ns_raw_more_words(size_t count) {
  return count > KOS_NS_RAW_INLINE
      ? (count - KOS_NS_RAW_INLINE + 63) / 64
      : 0;
}

/* Minimum allocation size for freelisted KosNamespace objects.  All
   namespaces with fast_count <= this value share one pool.  Tune by
   reading KOS_TRACE_FREELIST alloc_misses at runtime. */
#ifndef KOS_NS_MIN_SLOTS
#define KOS_NS_MIN_SLOTS 32
#endif

/* Maximum depth of the KosNamespace freelist. */
#define KOS_NS_FREELIST_MAX KOS_FREELIST_CAP(256)


typedef struct KosNamespace {
  PyObject_VAR_HEAD

  PyObject *name;       // PyUnicodeObject

  PyObject *parent;     // KosNamespace
  PyObject *binds;      // PyDictObject
  PyObject *template_ns; // the namespace this one was copied from, or NULL.
                         // When set, name, parent and fast_names are borrowed
                         // through it (it owns them for at least as long as
                         // this copy holds it); when NULL they are owned here.

  size_t fast_count;       // number of fast slots
  PyObject *fast_names;    // PyTupleObject of slot names, shared across copies
  uint64_t fast_raw[2];    // raw-flag bits for slots 0-127
  uint64_t *fast_raw_more; // bits for slots 128 and up: &fast[fast_count],
                           // inside this allocation, or NULL when unneeded
  PyObject *fast[0];       // slot values: raw value (if raw bit set) or KosRef
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
   The empty namespace: no parent, no symbols, no bindings, so a store
   through it raises NameError, and Namespace.bind refuses it. The
   fill of every native frame, which introduces no scope of its own but
   must never leave the live namespace register NULL. One immortal
   instance, created on first use; a borrowed reference, which holders
   take and release like any other. NULL with PyErr set on failure.
*/
PyObject *KosNamespace_Empty(void);


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
   Looks up Ref matching key. A module namespace creates the binding
   on a miss and keeps it; this is a request for a Ref to hold.

   Borrowed reference, or NULL with error set
 */
PyObject *KosNamespace_Ref(PyObject *ns, PyObject *key);


/**
   Looks up Ref matching key. Never creates a binding: a module
   namespace miss yields NULL even when a builtin of that name exists.

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
   Tier 3 read: the value stored in this namespace's own binding for
   key. Never consults builtins and never creates a binding.

   New reference, or NULL with NameError set when key is unbound here
   or the binding is empty.
*/
PyObject *KosNamespace_Deref(PyObject *ns, PyObject *key);


/**
   Resolution: the value a symbol evaluated in this namespace sees.
   Tier 3 first; a module namespace miss falls through to its builtins
   (tier 1). Never creates a binding.

   New reference, or NULL with NameError set.
*/
PyObject *KosNamespace_Resolve(PyObject *ns, PyObject *key);


/**
   Looks up Ref matching key. If not found in ns, check the parent of
   ns, and so forth.

   If a match is discovered, a borrowed reference is stored in the found
   pointer. Otherwise it is left unchanged.

   returns -1 if an error occurs, 0 otherwise.
 */
int KosNamespace_FindRef(PyObject *ns, PyObject *key, PyObject **found);


/**
   Tier 2 test: is key bound in this namespace. An emptied Ref and a
   raw fast slot both count; builtins do not.

   1 if bound, 0 if not, -1 with an error set.
 */
int KosNamespace_IsBound(PyObject *ns, PyObject *key);


/**
   Tier 3 test: does this namespace's binding for key hold a value.
   Builtins do not count.

   1 if a value is held, 0 if not, -1 with an error set.
 */
int KosNamespace_HasValue(PyObject *ns, PyObject *key);


int KosNamespace_Store(PyObject *ns, PyObject *key, PyObject *val);


int KosNamespace_Clear(PyObject *ns, PyObject *key);


/**
   Binds a name to a reference in this namespace
 */
int KosNamespace_Bind(PyObject *ns, PyObject *key, PyObject *ref);


/**
   find the fast index of a reference based on its key.

   A negative result means there is no fast slot for key. That is not
   in itself a failure, so a caller that can meet a real error --
   a key that is not a str -- must test PyErr_Occurred and propagate.

   returns index, or -1 if there is no slot or the lookup failed
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

#define KosNamespace_TEMPLATE(ns)            \
  (((KosNamespace *) ns)->template_ns)

#define KosNamespace_FAST_COUNT(ns)             \
  (((KosNamespace *) ns)->fast_count)

#define KosNamespace_FAST_NAMES(ns)             \
  (((KosNamespace *) ns)->fast_names)

#define KosNamespace_FAST_RAW(ns)               \
  (((KosNamespace *) ns)->fast_raw)

#define KosNamespace_FAST_RAW_MORE(ns)          \
  (((KosNamespace *) ns)->fast_raw_more)

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


/**
   Attribute the references of an untracked namespace to the holder
   being traversed. A namespace is born untracked, so a frame or parked
   namespace that was never revealed is invisible to the collector: the
   holder that introduced it must report what it holds. One level only:
   the parent is either revealed, and the collector visits it on its
   own, or introduced by another holder, which reports it. Reporting a
   namespace from more than one holder counts its references once per
   holder and frees live objects. Returns the first non-zero visit
   result, else 0.
*/
int KosNamespace_TraverseUntracked(PyObject *ns, visitproc visit, void *arg);


/* === SlotMap === */


/**
   A names tuple resolved once against a namespace layout: per name
   the fast index, or -1 for a name the layout gives no fast slot,
   which then binds by name. The layout is the namespace's fast-names
   tuple (NULL for a namespace without fast slots). A caller that runs
   the same code repeatedly keeps the map and revalidates it per use
   with KosSlotMap_Valid, so the name scan happens once per layout.
   The map is immutable apart from layout adoption (see Valid) and is
   not GC-tracked: it references only str tuples.
 */
typedef struct KosSlotMap {
  PyObject_VAR_HEAD        /* ob_size: name count */
  PyObject *names;         /* tuple[str]; owned */
  PyObject *layout;        /* fast-names tuple resolved against, or
                              NULL; owned */
  int32_t indices[1];      /* per name: fast index, or -1 */
} KosSlotMap;


extern PyTypeObject KosSlotMapType;


/**
   Resolve names (a tuple[str]) against the layout of ns. New
   reference, or NULL with an exception set.
 */
PyObject *KosSlotMap_New(PyObject *ns, PyObject *names);


/**
   Whether map's indices hold for ns. 1 on the same layout; also 1
   when ns carries another fast-names tuple with the same contents,
   which sibling function objects built from one code do, in which
   case the map adopts that tuple so the next check is an identity
   hit. 0 when the layouts differ, -1 with an exception set.
 */
int KosSlotMap_Valid(PyObject *map, PyObject *ns);


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


/* The word holding slot idx's raw bit: inline for the first 128 slots,
   the trailing overflow area past that. */
static inline uint64_t *kos_ns_raw_word(PyObject *ns, size_t idx) {
  if (idx < KOS_NS_RAW_INLINE)
    return &KosNamespace_FAST_RAW(ns)[idx >> 6];
  return &KosNamespace_FAST_RAW_MORE(ns)[(idx - KOS_NS_RAW_INLINE) >> 6];
}


static inline int kos_ns_is_raw(PyObject *ns, size_t idx) {
  return (int) ((*kos_ns_raw_word(ns, idx) >> (idx & 63)) & 1ULL);
}


static inline void kos_ns_set_raw(PyObject *ns, size_t idx, int raw) {
  uint64_t *word = kos_ns_raw_word(ns, idx);
  uint64_t bit = 1ULL << (idx & 63);
  if (raw)
    *word |= bit;
  else
    *word &= ~bit;
}


/**
  Restore a call namespace to the state KosNamespace_Copy produced from
  its template: every slot the template holds raw is emptied and marked
  raw again. That releases a raw local, and also a cell a closure
  promoted the slot to during the call, which the closure keeps alone.
  Slots the template holds as refs (closure and global cells) are left
  as they are. Used by the per-function frame cache when a frame is
  stored, and by the self-tail rebind.
*/
KOS_OP_INLINE void KosNamespace_ResetToTemplate(PyObject *ns) {

  PyObject *template_ns = KosNamespace_TEMPLATE(ns);
  size_t count = KosNamespace_FAST_COUNT(ns);

  for (size_t i = 0; i < count; i++) {
    if (kos_ns_is_raw(template_ns, i)) {
      Py_CLEAR(KosNamespace_GET_FAST_VAL(ns, i));
      kos_ns_set_raw(ns, i, 1);
    }
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
  /* The symbolic arm of KosRef_Deref, inline: a plain or constant ref
     holds its value in the one field, and a bound one is the common
     case for a closure slot. Everything else, an empty ref included,
     takes the out-of-line dispatch. */
  {
    PyTypeObject *tp = Py_TYPE(slot);
    if (likely(tp == &KosRefType || tp == &KosConstantRefType)) {
      PyObject *value = KosSymbolicRef_VALUE(slot);
      if (likely(value)) {
        Py_INCREF(value);
        return value;
      }
    }
  }
  return KosRef_Deref(slot);
}


/**
   Borrowed peek at a fast slot: the bound value of a raw slot or of a
   plain or constant ref, or NULL when the slot would need the general
   deref (empty, or another ref kind). For a handler that can act on the
   value without running code that could rebind the slot.
*/
static inline PyObject *kos_ns_fast_peek(PyObject *ns, size_t idx) {
  PyObject *slot = KosNamespace_GET_FAST_VAL(ns, idx);
  if (kos_ns_is_raw(ns, idx))
    return slot;
  if (slot) {
    PyTypeObject *tp = Py_TYPE(slot);
    if (tp == &KosRefType || tp == &KosConstantRefType)
      return KosSymbolicRef_VALUE(slot);
  }
  return NULL;
}


/**
   kos_ns_fast_peek with the slot's raw bit returned as well, for a
   handler that goes on to store and wants the one read.
*/
static inline PyObject *kos_ns_fast_peek_raw(
    PyObject *ns, size_t idx, int *raw) {

  PyObject *slot = KosNamespace_GET_FAST_VAL(ns, idx);
  *raw = kos_ns_is_raw(ns, idx);
  if (*raw)
    return slot;
  if (slot) {
    PyTypeObject *tp = Py_TYPE(slot);
    if (tp == &KosRefType || tp == &KosConstantRefType)
      return KosSymbolicRef_VALUE(slot);
  }
  return NULL;
}


/* Bind var to the fast slot's value, borrowed on a peek hit. On a
   miss (an empty slot, or a ref kind the peek does not read) bind
   var_ref to the owned general deref, which raises NameError for an
   empty slot, and var to it; var_ref is NULL on the hit path and is
   what the handler releases.
   For a handler that runs no user code while it holds var. on_error runs with the error set. */
#define KOS_NS_FAST_BORROW(var, var_ref, ns, idx, on_error)             \
  do {                                                                  \
    (var_ref) = NULL;                                                   \
    (var) = kos_ns_fast_peek((ns), (size_t) (idx));                     \
    if (unlikely(! (var))) {                                            \
      (var_ref) = kos_ns_fast_deref((ns), (size_t) (idx));              \
      if (unlikely(! (var_ref))) {                                      \
        on_error;                                                       \
      }                                                                 \
      (var) = (var_ref);                                                \
    }                                                                   \
  } while (0)


/* Turn a borrowed operand into a held one: var_ref takes a reference
   to var when it does not own one yet. For a handler about to run
   code that can rebind the slot var was peeked from. */
#define KOS_NS_HOLD(var, var_ref)                                       \
  do {                                                                  \
    if (! (var_ref)) {                                                  \
      Py_INCREF(var);                                                   \
      (var_ref) = (var);                                                \
    }                                                                   \
  } while (0)


/* KOS_NS_FAST_BORROW for an operand fetched after another operand was
   borrowed. The general deref of a ref kind the peek does not read can
   run user code (an attribute or item ref getter, a dynamic ref), and
   that code can rebind the slot an earlier operand was peeked from.
   The hold statement runs before that deref and holds each earlier
   operand (KOS_NS_HOLD); the peek-hit path runs no code and holds
   nothing. */
#define KOS_NS_FAST_BORROW_HOLDING(var, var_ref, ns, idx, hold, on_error) \
  do {                                                                  \
    (var_ref) = NULL;                                                   \
    (var) = kos_ns_fast_peek((ns), (size_t) (idx));                     \
    if (unlikely(! (var))) {                                            \
      hold;                                                             \
      (var_ref) = kos_ns_fast_deref((ns), (size_t) (idx));              \
      if (unlikely(! (var_ref))) {                                      \
        on_error;                                                       \
      }                                                                 \
      (var) = (var_ref);                                                \
    }                                                                   \
  } while (0)


/* An owned fast-slot read fetched after other operands were borrowed:
   the peek-hit path takes its reference without running code; the
   general deref path runs the hold statement first, as
   KOS_NS_FAST_BORROW_HOLDING does. var is NULL with the error set on
   failure, after on_error. */
#define KOS_NS_FAST_DEREF_HOLDING(var, ns, idx, hold, on_error)         \
  do {                                                                  \
    (var) = kos_ns_fast_peek((ns), (size_t) (idx));                     \
    if (likely(var)) {                                                  \
      Py_INCREF(var);                                                   \
    } else {                                                            \
      hold;                                                             \
      (var) = kos_ns_fast_deref((ns), (size_t) (idx));                  \
      if (unlikely(! (var))) {                                          \
        on_error;                                                       \
      }                                                                 \
    }                                                                   \
  } while (0)


static inline int kos_ns_fast_store(PyObject *ns, size_t idx, PyObject *val) {
  if (kos_ns_is_raw(ns, idx)) {
    PyObject *old = KosNamespace_GET_FAST_VAL(ns, idx);
    Py_INCREF(val);
    KosNamespace_GET_FAST_VAL(ns, idx) = val;
    Py_XDECREF(old);
    return 0;
  } else {
    return KosRef_Store(KosNamespace_GET_FAST_VAL(ns, idx), val);
  }
}


/**
   Store an OWNED value into a fast slot, consuming the reference on
   every path: a raw slot takes it without a refcount round trip, a ref
   slot stores through the ref and releases it. The old value is
   released after the slot holds the new one.
*/
static inline int kos_ns_fast_store_steal(
    PyObject *ns,
    size_t idx,
    PyObject *val) {

  if (kos_ns_is_raw(ns, idx)) {
    PyObject *old = KosNamespace_GET_FAST_VAL(ns, idx);
    KosNamespace_GET_FAST_VAL(ns, idx) = val;
    Py_XDECREF(old);
    return 0;
  } else {
    int res = KosRef_Store(KosNamespace_GET_FAST_VAL(ns, idx), val);
    Py_DECREF(val);
    return res;
  }
}


/* Layout of ns as a KosSlotMap records it: the fast-names tuple, or
   NULL for a namespace without fast slots. Borrowed. */
static inline PyObject *kos_ns_layout(PyObject *ns) {
  if (KosNamespace_SupportsFast(ns))
    return KosNamespace_FAST_NAMES(ns);
  return NULL;
}


/* KosSlotMap_Valid with the identity hit inline: the common case is
   the same layout object every time. */
static inline int kos_slotmap_holds(PyObject *map, PyObject *ns) {
  if (((KosSlotMap *) map)->layout == kos_ns_layout(ns))
    return 1;
  return KosSlotMap_Valid(map, ns);
}


/**
   Store values, an array aligned to map's names, into ns through the
   resolved indices: a fast index takes the slot write (raw or through
   the slot's Ref), -1 stores by name. Each non-NULL entry is stolen
   and set to NULL; a NULL entry is skipped and its name left as it
   was. map must hold for ns (kos_slotmap_holds). Not transactional:
   on error the entries already stored remain, every entry is still
   consumed, and -1 is returned with an exception set.
*/
static inline int kos_ns_store_slotmap(
    PyObject *ns,
    PyObject *map,
    PyObject **values) {

  KosSlotMap *m = (KosSlotMap *) map;
  Py_ssize_t n = Py_SIZE(m);
  int rc = 0;

  for (Py_ssize_t i = 0; i < n; i++) {
    PyObject *val = values[i];
    if (! val)
      continue;
    values[i] = NULL;
    if (rc < 0) {
      Py_DECREF(val);
      continue;
    }
    int32_t idx = m->indices[i];
    if (idx >= 0) {
      rc = kos_ns_fast_store_steal(ns, (size_t) idx, val);
    } else {
      rc = KosNamespace_Store(ns, PyTuple_GET_ITEM(m->names, i), val);
      Py_DECREF(val);
    }
  }
  return rc;
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
