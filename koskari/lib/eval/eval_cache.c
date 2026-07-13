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
   is not a standalone translation unit -- it shares eval.c's statics (e.g.
   str_getattr).

   It holds one mechanism today: monomorphic caching of attribute/method
   resolution for the CALL_ATTR* family (and, later, the attribute-read
   opcodes). Resolution is keyed on the receiver type and guarded by the
   type's version tag. A site that keeps missing its guard -- a polymorphic
   or megamorphic call site -- degrades to a bypass rather than thrash.

   The public entry point kos_resolve_named_attr keeps the -1/0/1 return
   contract (error / send-self / omit-self); the cache slot is an internal
   detail that never escapes it, so callers are insulated from whatever
   mechanism lives behind it.

   See design/interpreter/attr-lookup-cache.md.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
*/


/* __slots__ object-member type tag; the constant moved from structmember.h
   (<=3.11) to descrobject.h as Py_T_OBJECT_EX (3.12+). Same value (16). */
#if PY_MINOR_VERSION >= 12
# define KOS_T_OBJECT_EX Py_T_OBJECT_EX
#else
# include <structmember.h>
# define KOS_T_OBJECT_EX T_OBJECT_EX
#endif


/* === Attribute resolution helpers === */


/**
   True if descr is a plain instance-method descriptor (KosFunction or
   PyFunction) that we can return without calling __get__.
*/
static int is_plain_instance_method(PyObject *descr) {
  return KosFunction_CheckExact(descr) || PyFunction_Check(descr);
}


/**
   True if descr has __set__ or __delete__ (data descriptor).
*/
static int descr_is_data(PyObject *descr) {
  PyTypeObject *tp = Py_TYPE(descr);
  return tp->tp_descr_set != NULL;
}


/**
   Get instance dict from obj, or NULL if it has none. Uses
   _PyObject_GetDictPtr for correct layout handling across versions
   (managed dict, inline values).
*/
static PyObject *instance_dict_get(PyObject *obj) {
  PyObject **dictptr = _PyObject_GetDictPtr(obj);
  if (! dictptr)
    return NULL;
  return *dictptr;
}


/* === Type version guard === */


/**
   The receiver type's version, read for the guard (hot path). Zero means no
   valid version, so a cached entry always carries a non-zero version and a
   cleared/absent tag never matches it.

   The validity mechanism drifted: through 3.11 the tag is meaningful only
   while Py_TPFLAGS_VALID_VERSION_TAG is set; from 3.12 the flag is
   deprecated (and 3.14 stops setting it entirely) and `tp_version_tag != 0`
   is itself the validity signal -- PyType_Modified zeroes it on change.
*/
static uint64_t kos_type_version(PyTypeObject *tp) {
#if PY_MINOR_VERSION >= 12
  return (uint64_t) tp->tp_version_tag;
#else
  if (! (tp->tp_flags & Py_TPFLAGS_VALID_VERSION_TAG))
    return 0;
  return (uint64_t) tp->tp_version_tag;
#endif
}


/**
   Ensure the type has a version tag, for the populate (cold) path, and
   return it (0 if the type cannot be versioned). From 3.12, PyType_Lookup
   only assigns a tag conditionally, so we force it through the public
   PyUnstable_Type_AssignVersionTag -- otherwise the guard could never match
   and the cache would never engage (as it does not, on 3.14, without this).
   Through 3.11 the lookup during resolution already assigns and flags it.
*/
static uint64_t kos_type_version_assign(PyTypeObject *tp) {
#if PY_MINOR_VERSION >= 12
  if (! PyUnstable_Type_AssignVersionTag(tp))
    return 0;
  return (uint64_t) tp->tp_version_tag;
#else
  return kos_type_version(tp);
#endif
}


/* === Full (slow) resolution === */


/**
   Resolve name on obj by full Python semantics, and report how the result
   could be cached. Returns -1 on error (exception set), 0 = send self
   (unbound class method), 1 = omit self.

   On success sets *out (new ref). *mode is the cacheable classification and
   *cache_descr the borrowed class-side descriptor to store, or NULL. Only
   the modes that run no __get__ during resolution are reported as cacheable
   (DIRECT_OBJECT_GUARDED plain method / plain attribute, INSTANCE_DICT);
   anything that invokes a descriptor or custom hook is reported as
   GENERIC_LOOKUP so the caller does not cache a value it cannot safely
   reproduce from a guarded descriptor.

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

  PyObject *descr = PyType_Lookup(tp, name);   /* borrowed */

  /* Data descriptor: overrides instance dict; __get__ runs per access.
     Not cached in this version -- its __get__ may run arbitrary Python. */
  if (descr && descr_is_data(descr)) {
    PyObject *res = Py_TYPE(descr)->tp_descr_get(descr, obj, (PyObject *) tp);
    if (! res)
      return -1;
    *out = res;
    return 1;
  }

  /* Instance dict: the value is stored directly, no descriptor. */
  {
    PyObject *dict = instance_dict_get(obj);
    if (dict) {
      PyObject *val = PyDict_GetItem(dict, name);
      if (val) {
        Py_INCREF(val);
        *out = val;
        *mode = KOS_METHOD_INSTANCE_DICT;
        return 1;
      }
    }
  }

  /* Non-data descriptor / class attribute. */
  if (descr) {
    if (is_plain_instance_method(descr)) {
      /* Return the function unbound; caller invokes fn(obj, *args). No
         __get__, no bound-method allocation -- safe and worth caching. */
      Py_INCREF(descr);
      *out = descr;
      *mode = KOS_METHOD_DIRECT_OBJECT_GUARDED;
      *cache_descr = descr;
      return 0;
    }
    if (Py_TYPE(descr)->tp_descr_get != NULL) {
      /* Method descriptor / classmethod / staticmethod: __get__ runs.
         Return the result but do not cache (mode stays GENERIC_LOOKUP). */
      PyObject *res = Py_TYPE(descr)->tp_descr_get(descr, obj, (PyObject *) tp);
      if (! res)
        return -1;
      *out = res;
      return 1;
    }
    /* Plain class attribute, no __get__: return as-is; safe to cache. */
    Py_INCREF(descr);
    *out = descr;
    *mode = KOS_METHOD_DIRECT_OBJECT_GUARDED;
    *cache_descr = descr;
    return 1;
  }

  /* __getattr__ fallback (invoked only when standard lookup fails). */
  {
    PyObject *getattr_descr = PyType_Lookup(tp, str_getattr);
    if (getattr_descr) {
      PyObject *bound;
      if (Py_TYPE(getattr_descr)->tp_descr_get != NULL) {
        bound = Py_TYPE(getattr_descr)->tp_descr_get(
            getattr_descr, obj, (PyObject *) tp);
      } else {
        bound = getattr_descr;
        Py_INCREF(bound);
      }
      if (! bound)
        return -1;
      PyObject *res = PyObject_CallFunctionObjArgs(bound, name, NULL);
      Py_DECREF(bound);
      if (! res)
        return -1;
      *out = res;
      return 1;
    }
  }

  PyErr_Format(PyExc_AttributeError,
      "'%.100s' object has no attribute '%R'", tp->tp_name, name);
  return -1;
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
  slot->kind = KOS_CACHE_METHOD;
}


/**
   Guard a live method slot against obj's type: identity plus version.
*/
static int method_guard_ok(PyTypeObject *tp, KosCacheSlot *slot) {
  return tp == slot->u.method.type
      && kos_type_version(tp) == slot->u.method.type_version;
}


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

  switch (slot->u.method.mode) {
  case KOS_METHOD_DIRECT_OBJECT_GUARDED: {
    /* Instance dict may shadow the class attribute. */
    PyObject *dict = instance_dict_get(obj);
    if (dict && PyDict_GetItem(dict, name))
      return KOS_CACHE_HIT_FALL;
    if (is_plain_instance_method(descr)) {
      Py_INCREF(descr);
      *out = descr;
      return 0;
    }
    /* Plain class attribute (cached DIRECT never holds a __get__ descr). */
    Py_INCREF(descr);
    *out = descr;
    return 1;
  }
  case KOS_METHOD_INSTANCE_DICT: {
    PyObject *dict = instance_dict_get(obj);
    if (dict) {
      PyObject *val = PyDict_GetItem(dict, name);
      if (val) {
        Py_INCREF(val);
        *out = val;
        return 1;
      }
    }
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
   FIELD slot is ref-free (see the layout in bytecode.h). Baseline only: no
   opcode consumes it yet, and the direct modes are stubbed to GENERIC.
*/


/**
   Classify the storage location of name on obj: the access mode, and *index
   (a value slot / byte offset / dict-key hint, per the mode). The result is a
   location for the caller to load/store/delete through, not a value.

   Public today: a custom get/setattro or any data descriptor falls to
   GENERIC, correct via PyObject_GetAttr/SetAttr. The direct modes need
   per-version instance-layout access and land behind compat*.h; until then
   this reports GENERIC, so a consumer is correct but unaccelerated.
   TODO(field-offset): SLOT via member-descriptor offset (public);
   INSTANCE_VALUE / DICT_HINT via compat*.h.
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
     Every other data descriptor (property, ...) computes per access and stays
     GENERIC. */
  PyObject *descr = PyType_Lookup(tp, name);   /* borrowed */
  if (descr && descr_is_data(descr)) {
    if (Py_IS_TYPE(descr, &PyMemberDescr_Type)) {
      PyMemberDef *m = ((PyMemberDescrObject *) descr)->d_member;
      if (m->type == KOS_T_OBJECT_EX && m->offset > 0
          && m->offset <= (Py_ssize_t) UINT32_MAX) {
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
   Store into a __slots__ object member at byte offset: take a reference to val
   (the caller keeps its own) and drop the previous occupant. Equivalent to the
   member descriptor's __set__ for a T_OBJECT_EX slot, minus the dispatch.
*/
static void field_slot_store(PyObject *obj, uint32_t offset, PyObject *val) {
  PyObject **addr = (PyObject **) ((char *) obj + offset);
  Py_INCREF(val);
  Py_XSETREF(*addr, val);
}


#if PY_MINOR_VERSION >= 11
/**
   Read an inline instance value (KOS_FIELD_INSTANCE_VALUE) at its index.
   Returns a borrowed reference, or NULL to signal the caller must fall back:
   the instance materialized its dict, or the slot is unset (AttributeError /
   __getattr__ / a shadowed class attribute). The type-version guard has
   already matched, so the index is in bounds for a values-mode instance. The
   per-version value access is behind kos_inline_value_slots (compat_3xx.h).
*/
static PyObject *field_instance_value_load(PyObject *obj, uint32_t index) {
  int active;
  PyObject **slots = kos_inline_value_slots(obj, &active);
  if (! active)
    return NULL;
  return slots[index];
}


/**
   Overwrite an inline instance value at its index. Returns 1 on success, 0 to
   signal fallback: the instance materialized its dict, or the slot is unset --
   a first assignment must bump the values' used count and insertion order,
   bookkeeping we do not replicate, so PyObject_SetAttr handles it. The common
   in-place update (slot already set) takes the fast path.
*/
static int field_instance_value_store(PyObject *obj, uint32_t index,
    PyObject *val) {
  int active;
  PyObject **slots = kos_inline_value_slots(obj, &active);
  if (! active || ! slots[index])
    return 0;
  Py_INCREF(val);
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

   Baseline: no opcode consumes this yet. Load/store/delete handlers will call
   it and branch on the returned mode.
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
  int res;
  if (slot->kind == KOS_CACHE_FIELD && field_guard_ok(Py_TYPE(obj), slot)) {
    if (m == KOS_FIELD_SLOT) {
      field_slot_store(obj, off, acc);
      res = 0;
    }
#if PY_MINOR_VERSION >= 11
    else if (m == KOS_FIELD_INSTANCE_VALUE
             && field_instance_value_store(obj, off, acc)) {
      res = 0;
    }
#endif
    else {
      res = PyObject_SetAttr(obj, attr, acc);
    }
  } else {
    res = PyObject_SetAttr(obj, attr, acc);
  }

  Py_DECREF(acc);
  return res;
}


// The end.
