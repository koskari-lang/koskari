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
   koskari/lib/eval/eval_cache.c

   Per-site opcode cache mechanisms. This file is #included into eval.c and
   is not a standalone translation unit -- it shares eval.c's statics.

   It holds one mechanism today: monomorphic caching of attribute/method
   resolution for the CALL_ATTR* family (and, later, the attribute-read
   opcodes). Resolution is keyed on the receiver type and guarded by the
   type's version tag. A site that keeps missing its guard -- a polymorphic
   or megamorphic call site -- degrades to a bypass rather than thrash.

   The public entry point kos_resolve_named_attr keeps the -1/0/1 return
   contract (error / send-self / omit-self); the cache slot is an internal
   detail that never escapes it, so callers are insulated from whatever
   mechanism lives behind it.

   See design/archive/attr-lookup-cache.md.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
*/


/* __slots__ object-member type tag and the read-only member flag; both
   moved from structmember.h (<=3.11) to descrobject.h as Py_T_OBJECT_EX and
   Py_READONLY (3.12+). Same values (16 and 1). */
#if PY_MINOR_VERSION >= 12
# define KOS_T_OBJECT_EX Py_T_OBJECT_EX
# define KOS_READONLY Py_READONLY
#else
# include <structmember.h>
# define KOS_T_OBJECT_EX T_OBJECT_EX
# define KOS_READONLY READONLY
#endif


/* === Attribute resolution helpers === */


/**
   True if descr's type is a method descriptor: calling descr(obj, *args)
   is equivalent to calling descr.__get__(obj, type(obj))(*args), so it
   can be returned unbound and called with the receiver prepended,
   without running __get__ or allocating a bound method. The type
   records this itself (Py_TPFLAGS_METHOD_DESCRIPTOR), the same fact
   CPython's own method calls read: PyFunction, KosFunction, the Demand
   family, builtin method descriptors.
*/
static int is_plain_instance_method(PyObject *descr) {
  return PyType_HasFeature(Py_TYPE(descr), Py_TPFLAGS_METHOD_DESCRIPTOR);
}


/**
   True if descr has __set__ or __delete__ (data descriptor).
*/
static int descr_is_data(PyObject *descr) {
  PyTypeObject *tp = Py_TYPE(descr);
  return tp->tp_descr_set != NULL;
}


/**
   Get instance dict from obj, or NULL if it has none. Must never
   materialize: _PyObject_GetDictPtr builds the dict of a values-mode
   managed instance as a side effect, permanently disabling its
   inline-values fast paths, so managed types are peeked through the
   compat layer instead. For a values-mode instance this returns NULL --
   callers that need the stored attributes go through instance_attr_peek,
   which consults the inline values first.
*/
static PyObject *instance_dict_get(PyObject *obj) {
#if PY_MINOR_VERSION >= 11
  if (kos_type_has_managed_dict(Py_TYPE(obj)))
    return kos_managed_dict_peek(obj);
#endif

  PyObject **dictptr = _PyObject_GetDictPtr(obj);
  if (! dictptr)
    return NULL;
  return *dictptr;
}


/**
   Peek at an instance attribute: the stored value (borrowed) or NULL. A
   values-mode instance is read in place -- every attribute of such an
   instance lives at a shared-keys index, so absence from the keys is
   absence from the instance -- and that read runs no code. Otherwise
   the (never materialized) instance dict is consulted, and a non-str
   key there with the name's hash runs its __eq__: a NULL result with
   an error set is that probe's exception, which the caller reports.
*/
static PyObject *instance_attr_peek(PyObject *obj, PyObject *name) {
#if PY_MINOR_VERSION >= 11
  PyTypeObject *tp = Py_TYPE(obj);
  if (kos_type_has_inline_values(tp)) {
    int active;
    uint32_t cap;
    PyObject **slots = kos_inline_value_slots(obj, &active, &cap);
    if (active) {
      /* Bound the type-wide keys index by this instance's own values
         capacity: 3.13+ sizes each instance at creation, so a
         later-grown keys index can exceed the buffer. Out of range
         means the name cannot live inline on this instance, and with
         values valid that means it is absent. */
      uint32_t ix;
      if (kos_type_value_index(tp, name, &ix) && ix < cap)
        return slots[ix];
      return NULL;
    }
  }
#endif

  PyObject *dict = instance_dict_get(obj);
  if (! dict)
    return NULL;
  return PyDict_GetItemWithError(dict, name);
}


/* === Full (slow) resolution === */


/**
   Resolve name on obj by full Python semantics, and report how the result
   could be cached. Returns -1 on error (exception set), 0 = send self
   (unbound class method), 1 = omit self.

   On success sets *out (new ref). *mode is the cacheable classification and
   *cache_descr the borrowed class-side descriptor to store, or NULL. Only
   the modes that run no __get__ during resolution are reported as cacheable
   (DIRECT_METHOD_GUARDED method descriptor, DIRECT_OBJECT_GUARDED
   plain attribute, INSTANCE_DICT);
   anything that invokes a descriptor or custom hook is reported as
   GENERIC_LOOKUP so the caller does not cache a value it cannot safely
   reproduce from a guarded descriptor. A class attribute whose type is
   mutable is also GENERIC_LOOKUP: that type can gain __get__ or
   __set__ without moving the receiver type's version.

   :param obj: object to resolve on (borrowed)
   :param name: attribute name (borrowed, PyUnicode)
   :param out: resolved attribute (new ref on success)
   :param mode: out; cacheable classification
   :param cache_descr: out; borrowed descriptor to cache, or NULL
*/
static int resolve_named_attr_slow(
    PyObject *obj,
    PyObject *name,
    PyObject **out,
    KosMethodMode *mode,
    PyObject **cache_descr) {

  PyTypeObject *tp = Py_TYPE(obj);

  *mode = KOS_METHOD_GENERIC_LOOKUP;
  *cache_descr = NULL;

  /* Custom __getattribute__: cannot replicate; delegate to tp_getattro. */
  if (tp->tp_getattro != PyObject_GenericGetAttr) {
    *out = PyObject_GetAttr(obj, name);
    if (! *out)
      return -1;
    return 1;
  }

  /* The class attribute is held for the whole resolution: the instance
     dict probe below can run a key's __eq__, and one that drops the
     attribute from the class would free it under us, as CPython's
     generic lookup guards against too. */
  PyObject *descr = PyType_Lookup(tp, name);
  Py_XINCREF(descr);
  int res = -1;

  /* Data descriptor with a getter: overrides instance dict; __get__ runs
     per access. Not cached in this version -- its __get__ may run
     arbitrary Python. A __set__-only descriptor has no getter and falls
     through to the instance dict and the plain class attribute arm, as
     PyObject_GenericGetAttr does. */
  if (descr && descr_is_data(descr) && Py_TYPE(descr)->tp_descr_get) {
    PyObject *val = Py_TYPE(descr)->tp_descr_get(descr, obj, (PyObject *) tp);
    if (val) {
      *out = val;
      res = 1;
    }
    goto done;
  }

  /* Instance attribute: the value is stored directly, no descriptor. */
  {
    PyObject *val = instance_attr_peek(obj, name);
    if (val) {
      Py_INCREF(val);
      *out = val;
      *mode = KOS_METHOD_INSTANCE_DICT;
      res = 1;
      goto done;
    }
    if (PyErr_Occurred())
      goto done;
  }

  /* Non-data descriptor / class attribute. */
  if (descr) {
    if (is_plain_instance_method(descr)) {
      /* Return the function unbound; caller invokes fn(obj, *args). No
         __get__, no bound-method allocation -- safe and worth caching,
         under a mode that records the classification for the hit. */
      Py_INCREF(descr);
      *out = descr;
      *mode = KOS_METHOD_DIRECT_METHOD_GUARDED;
      *cache_descr = descr;
      res = 0;
      goto done;
    }
    if (Py_TYPE(descr)->tp_descr_get != NULL) {
      /* Method descriptor / classmethod / staticmethod: __get__ runs.
         Return the result but do not cache (mode stays GENERIC_LOOKUP). */
      PyObject *val = Py_TYPE(descr)->tp_descr_get(descr, obj, (PyObject *) tp);
      if (val) {
        *out = val;
        res = 1;
      }
      goto done;
    }
    /* Plain class attribute, no __get__: return as-is; safe to cache. */
    Py_INCREF(descr);
    *out = descr;
    *mode = KOS_METHOD_DIRECT_OBJECT_GUARDED;
    *cache_descr = descr;
    res = 1;
    goto done;
  }

  koskari_Err_SetMissingAttribute(obj, name);

 done:
  /* The guard is tp's version, which a class attribute of a mutable
     type does not move when that type gains __get__ or __set__. */
  if (descr && ! kos_type_is_immutable(Py_TYPE(descr))) {
    *mode = KOS_METHOD_GENERIC_LOOKUP;
    *cache_descr = NULL;
  }
  Py_XDECREF(descr);
  return res;
}


/* === Monomorphic method cache === */


/* Consecutive guard misses at a site before it degrades to a bypass. */
#define KOS_METHOD_MISS_LIMIT 8

/* method_slot_hit result: the guarded slot cannot serve this access (value
   moved, instance shadow), so resolve slow and re-populate. */
#define KOS_CACHE_HIT_FALL (-2)


/**
   Drop the owned refs of a method slot, leaving them NULL. Safe on a slot
   that holds none.
*/
static void method_slot_clear(KosCacheSlot *slot) {
  Py_CLEAR(slot->u.method.type);
  Py_CLEAR(slot->u.method.resolved);
}


/**
   Fill a slot with a fresh method resolution. Sets kind last, after every
   field is valid. tp and descr become owned refs (descr may be NULL).
*/
static void method_slot_populate(
    KosCacheSlot *slot,
    PyTypeObject *tp,
    uint64_t ver,
    KosMethodMode mode,
    PyObject *descr,
    uint8_t misses) {

  Py_INCREF(tp);
  slot->u.method.type = tp;
  Py_XINCREF(descr);
  slot->u.method.resolved = descr;
  slot->u.method.type_version = ver;
  slot->u.method.mode = mode;
  slot->u.method.misses = misses;
  slot->u.method.values_keys = NULL;   /* force a fresh name scan */
  slot->u.method.values_nentries = 0;
  slot->u.method.value_ix = KOS_VALUE_ABSENT;
  slot->kind = KOS_CACHE_METHOD;
}


/**
   Guard a live method slot against obj's type: identity plus version.
*/
static int method_guard_ok(PyTypeObject *tp, KosCacheSlot *slot) {
  return tp == slot->u.method.type
      && kos_type_version(tp) == slot->u.method.type_version;
}


#if PY_MINOR_VERSION >= 11
/**
   The inline-values index of name on tp, memoized in the method slot.
   Shared-keys entries are append-only, so a found index holds for the life
   of that keys object; an absent verdict is guarded by the entry count,
   since the keys can gain the name later without bumping the type version.
   The memoized keys pointer is only compared -- every read goes through the
   freshly fetched keys. Returns KOS_VALUE_ABSENT when name is not an inline
   attribute of tp instances.
*/
static uint32_t method_values_index(
    KosCacheSlot *slot,
    PyTypeObject *tp,
    PyObject *name) {

  KosSharedKeys *keys =
      (KosSharedKeys *) ((PyHeapTypeObject *) tp)->ht_cached_keys;
  if (! keys || keys->dk_kind == 0)
    return KOS_VALUE_ABSENT;

  if (slot->u.method.values_keys == (void *) keys
      && slot->u.method.values_nentries == (uint32_t) keys->dk_nentries)
    return slot->u.method.value_ix;

  uint32_t ix;
  if (! kos_type_value_index(tp, name, &ix))
    ix = KOS_VALUE_ABSENT;

  slot->u.method.values_keys = (void *) keys;
  slot->u.method.values_nentries = (uint32_t) keys->dk_nentries;
  slot->u.method.value_ix = ix;
  return ix;
}
#endif


/**
   Produce the result from a live, guard-passing method slot without a fresh
   MRO walk. Returns 0/1 (send_self), -1 on error, or KOS_CACHE_HIT_FALL when
   the slot cannot serve this access and the caller must resolve slow.
*/
static int method_slot_hit(
    KosCacheSlot *slot,
    PyObject *obj,
    PyTypeObject *tp,
    PyObject *name,
    PyObject **out) {

  PyObject *descr = slot->u.method.resolved;
  uint8_t mode = slot->u.method.mode;

  switch (mode) {
  case KOS_METHOD_DIRECT_OBJECT_GUARDED:
  case KOS_METHOD_DIRECT_METHOD_GUARDED: {
    /* An instance attribute may shadow the class attribute. The probe
       can run a key's __eq__, and one that re-runs this site with a
       receiver of another type refills the slot: the slot's descr is
       held and its mode read before the probe, and the answer is made
       from those, never from the slot as the probe left it. */
    int shadowed;
    Py_INCREF(descr);

#if PY_MINOR_VERSION >= 11
    int active = 0;
    uint32_t cap = 0;
    PyObject **values = NULL;
    if (kos_type_has_inline_values(tp))
      values = kos_inline_value_slots(obj, &active, &cap);
    if (active) {
      /* ix is type-wide; the receiver's own capacity bounds the read */
      uint32_t ix = method_values_index(slot, tp, name);
      shadowed = (ix != KOS_VALUE_ABSENT && ix < cap
                  && values[ix] != NULL);
    } else {
      shadowed = instance_attr_peek(obj, name) != NULL;
    }
#else
    shadowed = instance_attr_peek(obj, name) != NULL;
#endif

    if (shadowed || PyErr_Occurred()) {
      Py_DECREF(descr);
      return PyErr_Occurred() ? -1 : KOS_CACHE_HIT_FALL;
    }
    /* The mode recorded the classification at resolution: a method
       descriptor is returned unbound with the receiver sent, a plain
       class attribute as is (cached DIRECT never holds a __get__
       descr). The reference taken above is the caller's. */
    *out = descr;
    return mode == KOS_METHOD_DIRECT_METHOD_GUARDED ? 0 : 1;
  }
  case KOS_METHOD_INSTANCE_DICT: {
    PyObject *val;

#if PY_MINOR_VERSION >= 11
    int active = 0;
    uint32_t cap = 0;
    PyObject **values = NULL;
    if (kos_type_has_inline_values(tp))
      values = kos_inline_value_slots(obj, &active, &cap);
    if (active) {
      /* ix is type-wide; the receiver's own capacity bounds the read */
      uint32_t ix = method_values_index(slot, tp, name);
      val = (ix != KOS_VALUE_ABSENT && ix < cap) ? values[ix] : NULL;
    } else {
      val = instance_attr_peek(obj, name);
    }
#else
    val = instance_attr_peek(obj, name);
#endif

    if (val) {
      Py_INCREF(val);
      *out = val;
      return 1;
    }
    if (PyErr_Occurred())
      return -1;
    return KOS_CACHE_HIT_FALL;   /* value gone -> slow */
  }
  case KOS_METHOD_DATA_DESCRIPTOR:
  case KOS_METHOD_GENERIC_LOOKUP:
  case KOS_METHOD_MEGAMORPHIC:
    break;
  }
  return KOS_CACHE_HIT_FALL;
}


/**
   Cold path for kos_resolve_named_attr: cache miss, populate, churn bypass,
   and full resolution. Outlined (KOS_NOINLINE) so its bulk never inlines
   into the caller -- the hot dispatch loop keeps only the guarded fast path
   (the wrapper below). It re-checks the guard so a caller that reached here
   after a fall-through is classified correctly. Same -1/0/1 contract.
*/
KOS_NOINLINE static int kos_resolve_named_attr_cold(
    PyObject *obj,
    PyObject *name,
    KosCacheSlot *slot,
    PyObject **out) {

  PyTypeObject *tp = Py_TYPE(obj);

  /* Classify the non-hit outcome for tracing. Gated so non-trace builds pay
     nothing; every path here already failed the wrapper's fast hit. */
#if KOS_TRACE_OPCACHE
  if (! slot)
    KOS_TRACE_OPCACHE_INC(KOS_OC_METHOD_UNCACHED);
  else if (slot->kind == KOS_CACHE_METHOD
           && slot->u.method.mode == KOS_METHOD_MEGAMORPHIC)
    KOS_TRACE_OPCACHE_INC(KOS_OC_METHOD_BYPASS);
  else
    KOS_TRACE_OPCACHE_INC(KOS_OC_METHOD_MISS);
#endif
  int bypass = 0;
  int occupied_miss = 0;

  /* Try the cache. */
  if (slot && slot->kind == KOS_CACHE_METHOD) {
    if (slot->u.method.mode == KOS_METHOD_MEGAMORPHIC) {
      bypass = 1;                       /* churned site: always slow */
    } else if (method_guard_ok(tp, slot)) {
      int r = method_slot_hit(slot, obj, tp, name, out);
      if (r != KOS_CACHE_HIT_FALL) {
        if (r >= 0)
          slot->u.method.misses = 0;    /* stable: reset the churn counter */
        return r;
      }
      /* Guard held but the slot could not serve this access (value moved
         or shadowed): re-resolve and re-populate below. Not a type miss. */
    } else {
      occupied_miss = 1;                /* type changed under the guard */
    }
  }

  /* Slow path. */
  KosMethodMode mode;
  PyObject *descr = NULL;
  int sv = resolve_named_attr_slow(obj, name, out, &mode, &descr);
  if (sv < 0)
    return -1;

  /* Update the slot. */
  if (slot && ! bypass) {
    uint64_t ver = kos_type_version_assign(tp);
    int cacheable = (ver != 0) && (mode != KOS_METHOD_GENERIC_LOOKUP);

    if (! cacheable) {
      /* Cannot guard this result: drop any stale entry, leave the slot cold. */
      if (slot->kind == KOS_CACHE_METHOD) {
        method_slot_clear(slot);
        slot->kind = KOS_CACHE_EMPTY;
      }
    } else if (occupied_miss) {
      uint8_t n = (uint8_t) (slot->u.method.misses + 1);
      if (n >= KOS_METHOD_MISS_LIMIT) {
        /* Churn: stop caching this site. */
        method_slot_clear(slot);
        slot->u.method.type_version = 0;
        slot->u.method.mode = KOS_METHOD_MEGAMORPHIC;
        slot->u.method.misses = 0;
        slot->kind = KOS_CACHE_METHOD;
      } else {
        method_slot_clear(slot);
        method_slot_populate(slot, tp, ver, mode, descr, n);
      }
    } else {
      /* Cold, or a guard-ok fall-through: (re)populate fresh. */
      method_slot_clear(slot);
      method_slot_populate(slot, tp, ver, mode, descr, 0);
    }
  }

  return sv;
}


/**
   Resolve name on obj, using the per-site slot when provided. Fast path only:
   a guarded cache hit returns inline with no call. Every other case (no slot,
   cold slot, guard miss, value moved, bypass) tail-calls the outlined cold
   path, so the miss/populate/slow bulk stays out of the caller and the hot
   dispatch loop. The slot is an internal detail; the -1/0/1 contract holds.

   :param obj: object to resolve on (borrowed)
   :param name: attribute name (borrowed, PyUnicode)
   :param slot: per-site cache slot, or NULL to resolve without caching
   :param out: resolved attribute (new ref on success)
   :return: -1 on error; 0 = send self; 1 = omit self
*/
KOS_OP_INLINE int kos_resolve_named_attr(
    PyObject *obj,
    PyObject *name,
    KosCacheSlot *slot,
    PyObject **out) {

  if (slot != NULL
      && slot->kind == KOS_CACHE_METHOD
      && slot->u.method.mode != KOS_METHOD_MEGAMORPHIC) {
    PyTypeObject *tp = Py_TYPE(obj);
    if (method_guard_ok(tp, slot)) {
      int r = method_slot_hit(slot, obj, tp, name, out);
      if (r != KOS_CACHE_HIT_FALL) {
        if (r >= 0) {
          slot->u.method.misses = 0;
          KOS_TRACE_OPCACHE_INC(KOS_OC_METHOD_HIT);
        }
        return r;
      }
    }
  }
  return kos_resolve_named_attr_cold(obj, name, slot, out);
}


/* === Attribute-storage location cache (baseline) === */

/*
   Where the method cache resolves a *handle* (a class-side callable), this
   resolves a *location*: where an instance attribute lives, so load, store,
   and delete at any site for the same type+name can share one resolved
   (mode, index). The payload is a plain integer -- no owned refs -- so the
   FIELD slot is ref-free (see the layout in bytecode.h). FAST_ATTR,
   VAR_ATTR, their STORE rows and the attribute inplace electors consume
   the resolved (mode, index).
*/


/**
   Classify the storage location of name on obj: the access mode, and *index
   (a value slot / byte offset / dict-key hint, per the mode). The result is a
   location for the caller to load/store/delete through, not a value.

   A custom get/setattro or a data descriptor other than a writable
   object member falls to GENERIC, correct via PyObject_GetAttr/SetAttr.
   A __slots__ member resolves to SLOT with its byte offset, and on 3.11
   and later a managed-dict type with inline values resolves to
   INSTANCE_VALUE with the value index (compat*.h supplies the layout
   access). A class attribute whose type is mutable is GENERIC, since
   that type can gain __get__ or __set__ without moving the receiver
   type's version. Anything else is GENERIC.
*/
static KosFieldMode resolve_attr_location_slow(
    PyObject *obj,
    PyObject *name,
    uint32_t *index) {

  PyTypeObject *tp = Py_TYPE(obj);
  *index = 0;

  /* Cache a location only when both load and store go through the generic
     paths; a custom tp_getattro or tp_setattro can intercept either. */
  if (tp->tp_getattro != PyObject_GenericGetAttr
      || tp->tp_setattro != PyObject_GenericSetAttr)
    return KOS_FIELD_GENERIC;

  /* A data descriptor intercepts the access. A __slots__ object member is a
     data descriptor with a fixed byte offset (SLOT); it takes precedence over
     any instance dict, so the offset is authoritative for load and store.
     A READONLY member's setter raises, and the one mode serves both
     directions, so it stays GENERIC. Every other data descriptor
     (property, ...) computes per access and stays GENERIC. */
  PyObject *descr = PyType_Lookup(tp, name);   /* borrowed */

  /* The guard is tp's version, which a class attribute of a mutable
     type does not move when that type gains __get__ or __set__. */
  if (descr && ! kos_type_is_immutable(Py_TYPE(descr)))
    return KOS_FIELD_GENERIC;

  if (descr && descr_is_data(descr)) {
    /* The member's offset describes the layout of the descriptor's own
       type; a member of some other class installed as an attribute of
       this one passes the lookup but not member_get's own check, so
       only a receiver type that is a subtype of d_type takes the
       offset. */
    if (Py_IS_TYPE(descr, &PyMemberDescr_Type)
        && PyType_IsSubtype(tp, PyDescr_TYPE(descr))) {
      PyMemberDef *m = ((PyMemberDescrObject *) descr)->d_member;
      if (m->type == KOS_T_OBJECT_EX && ! (m->flags & KOS_READONLY)
          && m->offset > 0 && (size_t) m->offset <= UINT32_MAX) {
        *index = (uint32_t) m->offset;
        return KOS_FIELD_SLOT;
      }
    }
    return KOS_FIELD_GENERIC;
  }

  /* No data descriptor: the value lives in the instance. A managed-dict type
     with shared keys stores it inline at a fixed index; find and cache that
     index (INSTANCE_VALUE). A non-data descriptor of the same name does not
     defeat this -- the instance value shadows it on read, and an unset slot
     falls back to the generic path, which then finds the class attribute. */
#if PY_MINOR_VERSION >= 11
  if ((tp->tp_flags & Py_TPFLAGS_HEAPTYPE) && kos_type_has_inline_values(tp)
      && kos_type_value_index(tp, name, index))
    return KOS_FIELD_INSTANCE_VALUE;
#endif
  return KOS_FIELD_GENERIC;
}


/**
   Read a __slots__ object member (KOS_FIELD_SLOT) at byte offset. Returns a
   borrowed reference, or NULL when the slot is unset (the caller falls back to
   the generic path, which raises AttributeError or runs __getattr__).
*/
static PyObject *field_slot_load(PyObject *obj, uint32_t offset) {
  return *(PyObject **) ((char *) obj + offset);
}


/**
   Store into a __slots__ object member at byte offset: the member takes
   the caller's reference to val and drops the previous occupant.
   Equivalent to the member descriptor's __set__ for a T_OBJECT_EX slot,
   minus the dispatch. Cannot fail.
*/
static void field_slot_store(PyObject *obj, uint32_t offset, PyObject *val) {
  PyObject **addr = (PyObject **) ((char *) obj + offset);
  Py_XSETREF(*addr, val);
}


#if PY_MINOR_VERSION >= 11
/**
   Read an inline instance value (KOS_FIELD_INSTANCE_VALUE) at its index.
   Returns a borrowed reference, or NULL to signal the caller must fall back:
   the values are no longer valid, or the slot is unset (AttributeError /
   __getattr__ / a shadowed class attribute), or the site's index exceeds
   this instance's own values capacity (3.13+ sizes instances at creation,
   so a later-grown keys index can outrun an early instance's buffer). On
   3.13+ a materialized dict shares valid values, so the read stays correct
   there. The per-version value access is behind kos_inline_value_slots
   (compat_3xx.h).
*/
static PyObject *field_instance_value_load(PyObject *obj, uint32_t index) {
  int active;
  uint32_t cap;
  PyObject **slots = kos_inline_value_slots(obj, &active, &cap);
  if (! active || index >= cap || ! slots[index]) {
    KOS_TRACE_OPCACHE_INC(KOS_OC_FIELD_FALLBACK);
    return NULL;
  }
  return slots[index];
}


/**
   Overwrite an inline instance value at its index. Returns 1 on success,
   the value slot having taken the caller's reference to val; 0 to signal
   fallback with val untouched: the values are no longer valid, or the
   instance has a materialized dict, or the slot is unset. A first
   assignment must bump the values' used count and insertion order, and
   on 3.13+ a materialized dict sharing the values must notify its
   watchers (and on 3.13 keep its GC tracking); we replicate none of that
   bookkeeping, so PyObject_SetAttr handles it. The common in-place update
   (slot already set, no dict) takes the fast path.
*/
static int field_instance_value_store(PyObject *obj, uint32_t index,
    PyObject *val) {
  int active;
  uint32_t cap;
  PyObject **slots = kos_inline_value_slots(obj, &active, &cap);
  if (! active || index >= cap || ! slots[index]) {
    KOS_TRACE_OPCACHE_INC(KOS_OC_FIELD_FALLBACK);
    return 0;
  }
#if PY_MINOR_VERSION >= 13
  if (kos_managed_dict_peek(obj)) {
    KOS_TRACE_OPCACHE_INC(KOS_OC_FIELD_FALLBACK);
    return 0;
  }
#endif
  Py_SETREF(slots[index], val);
  return 1;
}
#endif


/**
   Guard a live field slot against obj's type: identity plus version. The
   guard type is borrowed (see the field layout). For the direct modes the
   consumer additionally confirms this instance still has the expected layout
   (values vs materialized dict) before using the location.
*/
static int field_guard_ok(PyTypeObject *tp, KosCacheSlot *slot) {
  return tp == slot->u.field.type
      && kos_type_version(tp) == slot->u.field.type_version;
}


/**
   Fill a field slot. Ref-free -- a borrowed guard type and an integer index --
   so no release-before-overwrite and a no-op teardown. kind set last.
*/
static void field_slot_populate(
    KosCacheSlot *slot,
    PyTypeObject *tp,
    uint64_t ver,
    KosFieldMode mode,
    uint32_t index,
    uint8_t misses) {

  slot->u.field.type = tp;            /* borrowed */
  slot->u.field.type_version = ver;
  slot->u.field.mode = mode;
  slot->u.field.index = index;
  slot->u.field.misses = misses;
  slot->kind = KOS_CACHE_FIELD;
}


/**
   Cold path: classify the location and update the slot -- populate, or degrade
   to a megamorphic bypass after repeated guard misses. Outlined, in the vein
   of kos_resolve_named_attr_cold.
*/
KOS_NOINLINE static KosFieldMode kos_resolve_attr_location_cold(
    PyObject *obj,
    PyObject *name,
    KosCacheSlot *slot,
    uint32_t *index) {

  PyTypeObject *tp = Py_TYPE(obj);
  int is_field = (slot && slot->kind == KOS_CACHE_FIELD);
  int bypass = (is_field && slot->u.field.mode == KOS_FIELD_MEGAMORPHIC);
  int occupied_miss = (is_field
                       && slot->u.field.mode != KOS_FIELD_MEGAMORPHIC
                       && slot->u.field.mode != KOS_FIELD_GENERIC
                       && ! field_guard_ok(tp, slot));

  KOS_TRACE_OPCACHE_INC(KOS_OC_FIELD_MISS);

  KosFieldMode mode = resolve_attr_location_slow(obj, name, index);
  if (! slot || bypass)
    return mode;                    /* churned site: re-resolve, do not cache */

  if (mode == KOS_FIELD_GENERIC) {
    /* An uncacheable location, correct for any type. Cache it guard-free so
       the site stays cheap; GENERIC doubles as the field give-up state. */
    field_slot_populate(slot, NULL, 0, KOS_FIELD_GENERIC, 0, 0);
    return mode;
  }

  /* A location mode (SLOT): needs a guardable type version. */
  uint64_t ver = kos_type_version_assign(tp);
  if (ver == 0) {
    if (is_field)                          /* cannot guard: leave cold */
      slot->kind = KOS_CACHE_EMPTY;
  } else if (occupied_miss
             && (uint8_t) (slot->u.field.misses + 1) >= KOS_METHOD_MISS_LIMIT) {
    slot->u.field.type = NULL;             /* churn: stop caching this site */
    slot->u.field.type_version = 0;
    slot->u.field.mode = KOS_FIELD_MEGAMORPHIC;
    slot->u.field.misses = 0;
    slot->kind = KOS_CACHE_FIELD;
  } else {
    uint8_t n = occupied_miss ? (uint8_t) (slot->u.field.misses + 1) : 0;
    field_slot_populate(slot, tp, ver, mode, *index, n);
  }
  return mode;
}


/**
   Resolve the storage location of name on obj, using the per-site slot when
   provided. Returns the access mode and sets *index; GENERIC means no
   cacheable location (the caller falls back to PyObject_GetAttr/SetAttr). Fast
   path only -- a guarded hit returns inline, everything else tail-calls the
   outlined cold path. In the same vein as kos_resolve_named_attr.

   The FAST_ATTR and VAR_ATTR load and store handlers and the attribute
   inplace electors call it and branch on the returned mode.
*/
KOS_OP_INLINE KosFieldMode kos_resolve_attr_location(
    PyObject *obj,
    PyObject *name,
    KosCacheSlot *slot,
    uint32_t *index) {

  if (slot && slot->kind == KOS_CACHE_FIELD) {
    KosFieldMode m = slot->u.field.mode;
    /* GENERIC is correct for any receiver type: no guard, just report it so
       the caller takes the generic path. Keeps uncacheable sites cheap. */
    if (m == KOS_FIELD_GENERIC) {
      KOS_TRACE_OPCACHE_INC(KOS_OC_FIELD_GENERIC);
      return KOS_FIELD_GENERIC;
    }
    if (m != KOS_FIELD_MEGAMORPHIC) {
      PyTypeObject *tp = Py_TYPE(obj);
      if (field_guard_ok(tp, slot)) {
        slot->u.field.misses = 0;
        *index = slot->u.field.index;
        KOS_TRACE_OPCACHE_INC(KOS_OC_FIELD_HIT);
        return m;
      }
    }
  }
  return kos_resolve_attr_location_cold(obj, name, slot, index);
}


/**
   Cache-aware augmented attribute assignment (obj.attr OP= args...), used by
   FAST_ATTR_INPLACE. On a direct field-cache hit the accumulator is read from
   -- and the folded result written straight back to -- the resolved storage
   location (a __slots__ member or an inline instance value), skipping the
   PyObject_GetAttr / PyObject_SetAttr protocol on both ends of the
   read-modify-write.

   The location is resolved once, serving both the read and the write. The fold
   between them runs arbitrary user code (an __iadd__ may reassign obj's type or
   materialize its managed dict), so the direct store is gated by re-checking
   the slot's guard against obj's current type: a hit reuses the resolved offset
   (the guard's type+version invariant means it is still valid), a miss -- like
   an unset field or a generic/megamorphic location -- falls back to the generic
   getattr/setattr. This re-guard, not a second resolve, is what keeps the raw
   field_slot_store from writing to a stale offset. slot may be NULL when caches
   are disabled. Neither pops args nor touches obj's ref -- the caller owns
   both. Returns 0/-1.
*/
static int kos_attr_inplace_cached(
    KosCacheSlot *slot,
    PyObject *obj,
    PyObject *attr,
    KosOperand op,
    PyObject *const *args,
    int count) {

  if (! slot)
    return kos_attr_inplace_reduce(obj, attr, op, args, count);

  /* Load: resolve the field location once and read the accumulator directly. */
  uint32_t off;
  KosFieldMode m = kos_resolve_attr_location(obj, attr, slot, &off);

  PyObject *acc = NULL;
  if (m == KOS_FIELD_SLOT)
    acc = field_slot_load(obj, off);
#if PY_MINOR_VERSION >= 11
  else if (m == KOS_FIELD_INSTANCE_VALUE)
    acc = field_instance_value_load(obj, off);
#endif

  if (! acc)
    /* Unset field, generic/megamorphic location, or a materialized dict: the
       generic getattr raises AttributeError / runs __getattr__ as needed. */
    return kos_attr_inplace_reduce(obj, attr, op, args, count);

  Py_INCREF(acc);
  acc = attr_inplace_fold(acc, op, args, count);
  if (! acc)
    return -1;

  /* Store: reuse the resolved offset only if the guard still holds -- the fold
     may have retyped obj out from under it. On a miss, fall back to setattr. */
  if (slot->kind == KOS_CACHE_FIELD && field_guard_ok(Py_TYPE(obj), slot)) {
    if (m == KOS_FIELD_SLOT) {
      field_slot_store(obj, off, acc);        /* acc stolen */
      return 0;
    }
#if PY_MINOR_VERSION >= 11
    if (m == KOS_FIELD_INSTANCE_VALUE
        && field_instance_value_store(obj, off, acc)) {
      return 0;                               /* acc stolen */
    }
#endif
  }

  int res = PyObject_SetAttr(obj, attr, acc);
  Py_DECREF(acc);
  return res;
}


// The end.
