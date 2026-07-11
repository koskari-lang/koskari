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
   The receiver type's version, used as the cache guard -- the same tag
   CPython's own attribute specializer guards on. It is valid only while
   Py_TPFLAGS_VALID_VERSION_TAG is set: PyType_Lookup assigns it,
   PyType_Modified clears it. Return 0 when unavailable so the caller
   declines to cache; a cached entry therefore always carries a non-zero
   version, and a cleared tag (current version 0) never matches it.
*/
static uint64_t kos_type_version(PyTypeObject *tp) {
  if (! (tp->tp_flags & Py_TPFLAGS_VALID_VERSION_TAG))
    return 0;
  return (uint64_t) tp->tp_version_tag;
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
    uint64_t ver = kos_type_version(tp);
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


// The end.
