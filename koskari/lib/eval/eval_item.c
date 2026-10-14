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
   The item family's shared helpers. kos_item_load, kos_item_store
   and kos_item_inplace own the election on a site's cache slot, the
   container bodies (exact list or tuple under an exact int key, exact
   dict under any key), the miss and demotion accounting, and the
   object protocol fallback; kos_item_clear, kos_item_take and
   kos_item_ref hold the operands across the protocol for the verbs
   that elect nothing. A handler fetches its operands by origin, calls
   one helper, and settles.

   Every helper borrows obj and key. A body runs no user code between
   its guard and its access except on the dict and protocol paths,
   which hold obj and key across the call, since a hash or a dunder
   can rebind the fast slot a handler peeked; a dict body that cannot
   finish after its hash keeps its hold and hands it to the protocol
   path (KOS_ITEM_HELD), so the borrowed pointers are never read again
   once user code has run under them. The store helper steals val: a
   stack value moves into the cell, a constant or slot value arrives
   with the reference the cell needs.

   Only the elected hit path is inline; the election, the protocol
   path and the miss accounting are out of line, one copy each.

   See design/archive/item-opcode-matrix.md.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


/* === Codes and slot accounting === */


/* What a body answers: done; the guard failed, which counts toward
   demotion; the right kind but a cell out of range or absent, which
   takes the protocol without counting; an error with the exception
   set. */
typedef enum KosItemCode {
  KOS_ITEM_HIT = 0,
  KOS_ITEM_MISS,
  KOS_ITEM_DECLINE,
  KOS_ITEM_ERROR,
} KosItemCode;


/* Or'd into MISS or DECLINE by a body that ran user code under its
   hold on obj and key and keeps that hold: the borrowed pointers the
   handler passed may be dead once the hash or a dunder rebound the
   slot they came from, so the protocol path that follows takes the
   held references over and releases them after its call. A body
   that ran no user code answers a plain code and the protocol path
   holds for itself. */
#define KOS_ITEM_HELD 8


/* Settle a slot with the body the classification chose, or with no
   body when nothing matched, in which case the protocol runs and the
   ticks count toward a re-election. A site with no slot only counts
   as uncached. */
static inline void kos_item_elect(KosCacheSlot *slot, KosItemBody fn) {
  if (! slot) {
    KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_UNCACHED);
    return;
  }

  KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_MISS);
  slot->u.item.fn = fn;
  kos_election_reset(&slot->u.item.election,
                     fn.load ? KOS_ELECTOR_ELECTED : KOS_ELECTOR_MEGAMORPHIC);
  slot->kind = KOS_CACHE_ITEM;
}


/* Count a guard miss at an elected site; at the limit the body is
   dropped and the protocol runs until the tick wrap re-elects. */
static KOS_NOINLINE void kos_item_miss(KosCacheSlot *slot) {
  if (! slot || slot->kind != KOS_CACHE_ITEM)
    return;

  if (kos_election_miss(&slot->u.item.election))
    slot->u.item.fn.load = NULL;
}


/* One execution of the protocol at a site, reached by demotion, by a
   decline, or by a classification that found no body: the wrap
   empties the slot so the next execution classifies again. */
static inline void kos_item_tick(KosCacheSlot *slot) {
  if (slot && slot->kind == KOS_CACHE_ITEM
      && kos_election_tick(&slot->u.item.election))
    slot->kind = KOS_CACHE_EMPTY;
}


/* === Container access === */


/* The index a sequence body reads off an exact int key: the C value
   normalised against the length as list_subscript does. A key wider
   than long long or outside the sequence answers 0, and the body
   declines to the protocol, which raises through the object's own
   path so IndexError keeps CPython's message. */
static inline int kos_item_index(
    PyObject *key,
    Py_ssize_t size,
    Py_ssize_t *out) {

  long long i;

  if (unlikely(! kos_long_as_ll(key, &i)))
    return 0;
  if (i < 0)
    i += size;
  if (unlikely(i < 0 || i >= size))
    return 0;
  *out = (Py_ssize_t) i;
  return 1;
}


static int kos_item_list_load(PyObject *obj, PyObject *key, PyObject **out) {
  Py_ssize_t i;

  if (unlikely(! (PyList_CheckExact(obj) && PyLong_CheckExact(key))))
    return KOS_ITEM_MISS;
  if (unlikely(! kos_item_index(key, PyList_GET_SIZE(obj), &i)))
    return KOS_ITEM_DECLINE;
  *out = PyList_GET_ITEM(obj, i);
  Py_INCREF(*out);
  return KOS_ITEM_HIT;
}


static int kos_item_tuple_load(PyObject *obj, PyObject *key, PyObject **out) {
  Py_ssize_t i;

  if (unlikely(! (PyTuple_CheckExact(obj) && PyLong_CheckExact(key))))
    return KOS_ITEM_MISS;
  if (unlikely(! kos_item_index(key, PyTuple_GET_SIZE(obj), &i)))
    return KOS_ITEM_DECLINE;
  *out = PyTuple_GET_ITEM(obj, i);
  Py_INCREF(*out);
  return KOS_ITEM_HIT;
}


static int kos_item_dict_load(PyObject *obj, PyObject *key, PyObject **out) {
  PyObject *v;

  if (unlikely(! PyDict_CheckExact(obj)))
    return KOS_ITEM_MISS;
  Py_INCREF(obj);
  Py_INCREF(key);
  v = PyDict_GetItemWithError(obj, key);
  if (unlikely(! v)) {
    /* a missing key is the exact dict's KeyError, raised here rather
       than by a second lookup through the protocol */
    if (! PyErr_Occurred())
      kos_elector_set_key_error(key);
    Py_DECREF(key);
    Py_DECREF(obj);
    return KOS_ITEM_ERROR;
  }
  Py_INCREF(v);
  Py_DECREF(key);
  Py_DECREF(obj);
  *out = v;
  return KOS_ITEM_HIT;
}


/* The stolen val moves into the cell; the displaced value is released
   last. */
static int kos_item_list_store(PyObject *obj, PyObject *key, PyObject *val) {
  Py_ssize_t i;
  PyObject *old;

  if (unlikely(! (PyList_CheckExact(obj) && PyLong_CheckExact(key))))
    return KOS_ITEM_MISS;
  if (unlikely(! kos_item_index(key, PyList_GET_SIZE(obj), &i)))
    return KOS_ITEM_DECLINE;
  old = PyList_GET_ITEM(obj, i);
  PyList_SET_ITEM(obj, i, val);
  Py_DECREF(old);
  return KOS_ITEM_HIT;
}


static int kos_item_dict_store(PyObject *obj, PyObject *key, PyObject *val) {
  int rc;

  if (unlikely(! PyDict_CheckExact(obj)))
    return KOS_ITEM_MISS;
  Py_INCREF(obj);
  Py_INCREF(key);
  rc = PyDict_SetItem(obj, key, val);
  Py_DECREF(key);
  Py_DECREF(obj);
  Py_DECREF(val);
  return unlikely(rc < 0) ? KOS_ITEM_ERROR : KOS_ITEM_HIT;
}


/* The body a site elects for a borrowed container and key. A store
   site has no tuple body. */
static inline KosItemLoadBody kos_item_load_classify(
    PyObject *obj,
    PyObject *key) {

  long long i;

  if (PyDict_CheckExact(obj))
    return kos_item_dict_load;
  if (! PyLong_CheckExact(key) || ! kos_long_as_ll(key, &i))
    return NULL;
  if (PyList_CheckExact(obj))
    return kos_item_list_load;
  if (PyTuple_CheckExact(obj))
    return kos_item_tuple_load;
  return NULL;
}


static inline KosItemStoreBody kos_item_store_classify(
    PyObject *obj,
    PyObject *key) {

  long long i;

  if (PyDict_CheckExact(obj))
    return kos_item_dict_store;
  if (PyList_CheckExact(obj) && PyLong_CheckExact(key)
      && kos_long_as_ll(key, &i))
    return kos_item_list_store;
  return NULL;
}


/* === Load === */


static KOS_NOINLINE PyObject *kos_item_load_protocol(
    KosCacheSlot *slot,
    PyObject *obj,
    PyObject *key) {

  PyObject *out;

  kos_item_tick(slot);
  Py_INCREF(obj);
  Py_INCREF(key);
  out = PyObject_GetItem(obj, key);
  Py_DECREF(key);
  Py_DECREF(obj);
  return out;
}


/* The protocol over references a body held and handed over. */
static KOS_NOINLINE PyObject *kos_item_load_protocol_held(
    KosCacheSlot *slot,
    PyObject *obj,
    PyObject *key) {

  PyObject *out;

  kos_item_tick(slot);
  out = PyObject_GetItem(obj, key);
  Py_DECREF(key);
  Py_DECREF(obj);
  return out;
}


static inline PyObject *kos_item_load_run(
    KosCacheSlot *slot,
    KosItemLoadBody body,
    PyObject *obj,
    PyObject *key) {

  PyObject *out;
  int code = body(obj, key, &out);

  switch (code & ~KOS_ITEM_HELD) {
  case KOS_ITEM_HIT:
    KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_HIT);
    return out;
  case KOS_ITEM_ERROR:
    return NULL;
  case KOS_ITEM_MISS:
    kos_item_miss(slot);
    break;
  case KOS_ITEM_DECLINE:
    break;
  }
  if (code & KOS_ITEM_HELD)
    return kos_item_load_protocol_held(slot, obj, key);
  return kos_item_load_protocol(slot, obj, key);
}


/* An empty slot or an ungated site classifies and elects; a demoted
   slot runs the protocol. */
static KOS_NOINLINE PyObject *kos_item_load_cold(
    KosCacheSlot *slot,
    PyObject *obj,
    PyObject *key) {

  KosItemLoadBody body;

  if (slot && slot->kind == KOS_CACHE_ITEM)
    return kos_item_load_protocol(slot, obj, key);
  body = kos_item_load_classify(obj, key);
  kos_item_elect(slot, (KosItemBody) { .load = body });
  if (! body)
    return kos_item_load_protocol(slot, obj, key);
  return kos_item_load_run(slot, body, obj, key);
}


/**
   obj[key] through the site's elected body. obj and key borrowed.
   Returns a new reference, or NULL with the error set.
*/
static inline PyObject *kos_item_load(
    KosCacheSlot *slot,
    PyObject *obj,
    PyObject *key) {

  if (likely(slot && slot->kind == KOS_CACHE_ITEM && slot->u.item.fn.load))
    return kos_item_load_run(slot, slot->u.item.fn.load, obj, key);
  return kos_item_load_cold(slot, obj, key);
}


/* === Store === */


static KOS_NOINLINE int kos_item_store_protocol(
    KosCacheSlot *slot,
    PyObject *obj,
    PyObject *key,
    PyObject *val) {

  int rc;

  kos_item_tick(slot);
  Py_INCREF(obj);
  Py_INCREF(key);
  rc = PyObject_SetItem(obj, key, val);
  Py_DECREF(key);
  Py_DECREF(obj);
  Py_DECREF(val);
  return rc;
}


static KOS_NOINLINE int kos_item_store_protocol_held(
    KosCacheSlot *slot,
    PyObject *obj,
    PyObject *key,
    PyObject *val) {

  int rc;

  kos_item_tick(slot);
  rc = PyObject_SetItem(obj, key, val);
  Py_DECREF(key);
  Py_DECREF(obj);
  Py_DECREF(val);
  return rc;
}


static inline int kos_item_store_run(
    KosCacheSlot *slot,
    KosItemStoreBody body,
    PyObject *obj,
    PyObject *key,
    PyObject *val) {

  int code = body(obj, key, val);

  switch (code & ~KOS_ITEM_HELD) {
  case KOS_ITEM_HIT:
    KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_HIT);
    return 0;
  case KOS_ITEM_ERROR:
    return -1;
  case KOS_ITEM_MISS:
    kos_item_miss(slot);
    break;
  case KOS_ITEM_DECLINE:
    break;
  }
  if (code & KOS_ITEM_HELD)
    return kos_item_store_protocol_held(slot, obj, key, val);
  return kos_item_store_protocol(slot, obj, key, val);
}


static KOS_NOINLINE int kos_item_store_cold(
    KosCacheSlot *slot,
    PyObject *obj,
    PyObject *key,
    PyObject *val) {

  KosItemStoreBody body;

  if (slot && slot->kind == KOS_CACHE_ITEM)
    return kos_item_store_protocol(slot, obj, key, val);
  body = kos_item_store_classify(obj, key);
  kos_item_elect(slot, (KosItemBody) { .store = body });
  if (! body)
    return kos_item_store_protocol(slot, obj, key, val);
  return kos_item_store_run(slot, body, obj, key, val);
}


/**
   obj[key] = val through the site's elected body. obj and key
   borrowed; val stolen, consumed on every path. Returns 0, or -1 with
   the error set.
*/
static inline int kos_item_store(
    KosCacheSlot *slot,
    PyObject *obj,
    PyObject *key,
    PyObject *val) {

  if (likely(slot && slot->kind == KOS_CACHE_ITEM && slot->u.item.fn.store))
    return kos_item_store_run(slot, slot->u.item.fn.store, obj, key, val);
  return kos_item_store_cold(slot, obj, key, val);
}


/* === In-place === */


/* The cell an in-place body reduces: a list cell under an exact int
   index, or a dict cell under any key. FOUND sets acc (borrowed from
   the container) and idx, -1 on the dict arm, where obj and key are
   then held until kos_item_inplace_store or kos_item_inplace_release
   lets them go, since the hash ran user code. DECLINE is an index
   outside the list or a key the dict lacks (the protocol raises the
   IndexError or KeyError); on the dict arm the hold stays, with idx
   -1, for the body to hand over. MISS any other container, ERROR a
   failed hash with the error set and the hold released. */
typedef enum KosItemCell {
  KOS_ITEM_CELL_FOUND,
  KOS_ITEM_CELL_DECLINE,
  KOS_ITEM_CELL_MISS,
  KOS_ITEM_CELL_ERROR,
} KosItemCell;


static inline KosItemCell kos_item_inplace_cell(
    PyObject *obj,
    PyObject *key,
    PyObject **out_acc,
    Py_ssize_t *out_idx) {

  if (PyList_CheckExact(obj) && PyLong_CheckExact(key)) {
    if (! kos_item_index(key, PyList_GET_SIZE(obj), out_idx)) {
      *out_idx = 0;
      return KOS_ITEM_CELL_DECLINE;
    }
    *out_acc = PyList_GET_ITEM(obj, *out_idx);
    return KOS_ITEM_CELL_FOUND;
  }
  if (PyDict_CheckExact(obj)) {
    Py_INCREF(obj);
    Py_INCREF(key);
    *out_acc = PyDict_GetItemWithError(obj, key);
    *out_idx = -1;
    if (! *out_acc) {
      if (PyErr_Occurred()) {
        Py_DECREF(key);
        Py_DECREF(obj);
        return KOS_ITEM_CELL_ERROR;
      }
      /* the hash ran; the hold stays with the caller's answer */
      return KOS_ITEM_CELL_DECLINE;
    }
    return KOS_ITEM_CELL_FOUND;
  }
  *out_idx = 0;
  return KOS_ITEM_CELL_MISS;
}


/* The dict arm holds obj and key from the cell lookup to the store.
   An error inside the arm releases them on the way out; a miss or a
   decline hands them to the caller flagged KOS_ITEM_HELD, since the
   hash ran user code and the caller's borrowed pointers may be dead. */
static inline void kos_item_inplace_release(
    PyObject *obj,
    PyObject *key,
    Py_ssize_t idx) {

  if (idx < 0) {
    Py_DECREF(key);
    Py_DECREF(obj);
  }
}


static inline int kos_item_inplace_answer(Py_ssize_t idx, int code) {
  return idx < 0 ? code | KOS_ITEM_HELD : code;
}


/* The store of a body's result into the cell kos_item_inplace_cell
   found: nothing when the result is the accumulator written in place,
   else the list cell is swapped and the old value released, or the
   dict cell stored and the owned result released. The dict arm's
   holds are released either way. Returns 0, or -1 with an error set. */
static inline int kos_item_inplace_store(
    PyObject *obj,
    PyObject *key,
    Py_ssize_t idx,
    PyObject *acc,
    PyObject *out) {

  int rc = 0;

  if (idx >= 0) {
    if (out != acc) {
      PyList_SET_ITEM(obj, idx, out);
      Py_DECREF(acc);
    }
    return 0;
  }
  if (out != acc) {
    rc = PyDict_SetItem(obj, key, out);
    Py_DECREF(out);
  }
  Py_DECREF(key);
  Py_DECREF(obj);
  return rc;
}


/* The kinds an in-place body reduces: the exact-type check, the read
   of the C value (an int wider than long long declines), and the
   operator's fast path by name, which writes the result into the
   accumulator when the container is its only owner. */
#define KOS_ITEM_INT_VARS        long long a, b
#define KOS_ITEM_INT_CHECK(v)    PyLong_CheckExact(v)
#define KOS_ITEM_INT_READ(v, into)  kos_long_as_ll((v), &(into))
#define KOS_ITEM_INT_FAST(NAME)  long_fast_##NAME##_ll

#define KOS_ITEM_FLOAT_VARS      double a, b
#define KOS_ITEM_FLOAT_CHECK(v)  PyFloat_CheckExact(v)
#define KOS_ITEM_FLOAT_READ(v, into)  (((into) = PyFloat_AS_DOUBLE(v)), 1)
#define KOS_ITEM_FLOAT_FAST(NAME)  float_fast_##NAME##_d

#define KOS_ITEM_INT_OPS(X)                                             \
  X(INT, int, add, ADD)                                                 \
  X(INT, int, subtract, SUBTRACT)                                       \
  X(INT, int, multiply, MULTIPLY)                                       \
  X(INT, int, modulo, MODULO)                                           \
  X(INT, int, bit_and, BIT_AND)                                         \
  X(INT, int, bit_or, BIT_OR)                                           \
  X(INT, int, bit_xor, BIT_XOR)                                         \
  X(INT, int, bit_left, BIT_LEFT)                                       \
  X(INT, int, bit_right, BIT_RIGHT)                                     \
  X(INT, int, floor_divide, FLOOR_DIVIDE)                               \
  X(INT, int, true_divide, TRUE_DIVIDE)                                 \
  X(INT, int, power, POWER)

#define KOS_ITEM_FLOAT_OPS(X)                                           \
  X(FLOAT, float, add, ADD)                                             \
  X(FLOAT, float, subtract, SUBTRACT)                                   \
  X(FLOAT, float, multiply, MULTIPLY)                                   \
  X(FLOAT, float, modulo, MODULO)                                       \
  X(FLOAT, float, true_divide, TRUE_DIVIDE)                             \
  X(FLOAT, float, floor_divide, FLOOR_DIVIDE)


/* One body per operator and kind: the cell found, the kind's guard on
   the cell's value and on rhs (a miss counts), the C values read (a
   decline takes the protocol without counting), the arithmetic with
   the accumulator offered as the result's home, and the store back. */
#define KOS_ITEM_INPLACE_BODY(KIND, kind, NAME, OPERAND)                \
  static int kos_item_inplace_##kind##_##NAME(                          \
      PyObject *obj, PyObject *key, PyObject *rhs) {                    \
    PyObject *acc, *out;                                                \
    Py_ssize_t idx;                                                     \
    KOS_ITEM_##KIND##_VARS;                                             \
    int rc;                                                             \
                                                                        \
    switch (kos_item_inplace_cell(obj, key, &acc, &idx)) {              \
    case KOS_ITEM_CELL_FOUND:                                           \
      break;                                                            \
    case KOS_ITEM_CELL_MISS:                                            \
      return KOS_ITEM_MISS;                                             \
    case KOS_ITEM_CELL_ERROR:                                           \
      return KOS_ITEM_ERROR;                                            \
    default:                                                            \
      return kos_item_inplace_answer(idx, KOS_ITEM_DECLINE);            \
    }                                                                   \
    if (unlikely(! KOS_ITEM_##KIND##_CHECK(acc)                         \
                 || ! KOS_ITEM_##KIND##_CHECK(rhs)))                    \
      return kos_item_inplace_answer(idx, KOS_ITEM_MISS);               \
    if (unlikely(! KOS_ITEM_##KIND##_READ(acc, a)                       \
                 || ! KOS_ITEM_##KIND##_READ(rhs, b)))                  \
      return kos_item_inplace_answer(idx, KOS_ITEM_DECLINE);            \
    rc = KOS_ITEM_##KIND##_FAST(NAME)(a, b, acc, NULL, &out);           \
    if (unlikely(rc <= 0)) {                                            \
      if (rc < 0) {                                                     \
        kos_item_inplace_release(obj, key, idx);                        \
        return KOS_ITEM_ERROR;                                          \
      }                                                                 \
      return kos_item_inplace_answer(idx, KOS_ITEM_DECLINE);            \
    }                                                                   \
    if (unlikely(kos_item_inplace_store(obj, key, idx, acc, out) < 0))  \
      return KOS_ITEM_ERROR;                                            \
    return KOS_ITEM_HIT;                                                \
  }

KOS_ITEM_INT_OPS(KOS_ITEM_INPLACE_BODY)
KOS_ITEM_FLOAT_OPS(KOS_ITEM_INPLACE_BODY)

#define KOS_ITEM_INPLACE_ENTRY(KIND, kind, NAME, OPERAND)               \
  [KOS_OPERAND_##OPERAND] = kos_item_inplace_##kind##_##NAME,

static const KosItemInplaceBody
kos_item_inplace_int_bodies[KOS_OPERAND_COUNT] = {
  KOS_ITEM_INT_OPS(KOS_ITEM_INPLACE_ENTRY)
};

static const KosItemInplaceBody
kos_item_inplace_float_bodies[KOS_OPERAND_COUNT] = {
  KOS_ITEM_FLOAT_OPS(KOS_ITEM_INPLACE_ENTRY)
};

#undef KOS_ITEM_INPLACE_ENTRY
#undef KOS_ITEM_INPLACE_BODY
#undef KOS_ITEM_FLOAT_OPS
#undef KOS_ITEM_INT_OPS
#undef KOS_ITEM_FLOAT_FAST
#undef KOS_ITEM_FLOAT_READ
#undef KOS_ITEM_FLOAT_CHECK
#undef KOS_ITEM_FLOAT_VARS
#undef KOS_ITEM_INT_FAST
#undef KOS_ITEM_INT_READ
#undef KOS_ITEM_INT_CHECK
#undef KOS_ITEM_INT_VARS


/* The str body: + over an exact-str cell and operand. A container
   cell cannot be vacated for an in-place append the way a fast slot
   can, since a list with a NULL entry is not a list, so the
   concatenation is swapped in. */
static int kos_item_inplace_str_add(
    PyObject *obj,
    PyObject *key,
    PyObject *rhs) {

  PyObject *acc, *out;
  Py_ssize_t idx;

  switch (kos_item_inplace_cell(obj, key, &acc, &idx)) {
  case KOS_ITEM_CELL_FOUND:
    break;
  case KOS_ITEM_CELL_MISS:
    return KOS_ITEM_MISS;
  case KOS_ITEM_CELL_ERROR:
    return KOS_ITEM_ERROR;
  default:
    return kos_item_inplace_answer(idx, KOS_ITEM_DECLINE);
  }
  if (unlikely(! PyUnicode_CheckExact(acc) || ! PyUnicode_CheckExact(rhs)))
    return kos_item_inplace_answer(idx, KOS_ITEM_MISS);

  out = PyUnicode_Concat(acc, rhs);
  if (unlikely(! out)) {
    kos_item_inplace_release(obj, key, idx);
    return KOS_ITEM_ERROR;
  }
  /* an empty operand hands the cell's own value back as a new
     reference; the store keeps the cell, so that reference goes */
  if (out == acc)
    Py_DECREF(out);
  if (unlikely(kos_item_inplace_store(obj, key, idx, acc, out) < 0))
    return KOS_ITEM_ERROR;
  return KOS_ITEM_HIT;
}


/* The cell of an exact list under an exact int key the index read can
   take, borrowed; NULL for any other shape or an index outside the
   list. */
static inline PyObject *kos_item_inplace_peek(PyObject *obj, PyObject *key) {
  Py_ssize_t idx;

  if (PyList_CheckExact(obj) && PyLong_CheckExact(key))
    return kos_item_index(key, PyList_GET_SIZE(obj), &idx)
        ? PyList_GET_ITEM(obj, idx) : NULL;
  return NULL;
}


/* The body a site elects: the operator's entry in the int table when
   the cell and rhs are exact ints the read can take, in the float
   table when both are exact floats, the str body for + on two exact
   strs, else NULL. An exact dict's cell is not read, since the lookup
   runs the key's __eq__; rhs stands in for it, and the body checks
   the cell and answers a miss. Runs no user code. */
static inline KosItemInplaceBody kos_item_inplace_classify(
    KosOperand op,
    PyObject *obj,
    PyObject *key,
    PyObject *rhs) {

  PyObject *acc = PyDict_CheckExact(obj)
      ? rhs : kos_item_inplace_peek(obj, key);
  long long b;

  if (op >= KOS_OPERAND_COUNT || ! acc)
    return NULL;
  if (PyLong_CheckExact(acc) && PyLong_CheckExact(rhs))
    return kos_long_as_ll(rhs, &b) ? kos_item_inplace_int_bodies[op] : NULL;
  if (PyFloat_CheckExact(acc) && PyFloat_CheckExact(rhs))
    return kos_item_inplace_float_bodies[op];
  if (op == KOS_OPERAND_ADD
      && PyUnicode_CheckExact(acc) && PyUnicode_CheckExact(rhs))
    return kos_item_inplace_str_add;
  return NULL;
}


static KOS_NOINLINE int kos_item_inplace_protocol(
    KosCacheSlot *slot,
    PyObject *obj,
    PyObject *key,
    KosOperand op,
    PyObject *const *args,
    int count) {

  int rc;

  kos_item_tick(slot);
  Py_INCREF(obj);
  Py_INCREF(key);
  rc = kos_item_inplace_reduce(obj, key, op, args, count);
  Py_DECREF(key);
  Py_DECREF(obj);
  return rc;
}


static KOS_NOINLINE int kos_item_inplace_protocol_held(
    KosCacheSlot *slot,
    PyObject *obj,
    PyObject *key,
    KosOperand op,
    PyObject *const *args,
    int count) {

  int rc;

  kos_item_tick(slot);
  rc = kos_item_inplace_reduce(obj, key, op, args, count);
  Py_DECREF(key);
  Py_DECREF(obj);
  return rc;
}


static inline int kos_item_inplace_run(
    KosCacheSlot *slot,
    KosItemInplaceBody body,
    PyObject *obj,
    PyObject *key,
    KosOperand op,
    PyObject *const *args) {

  int code = body(obj, key, args[0]);

  switch (code & ~KOS_ITEM_HELD) {
  case KOS_ITEM_HIT:
    KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_HIT);
    return 0;
  case KOS_ITEM_ERROR:
    return -1;
  case KOS_ITEM_MISS:
    kos_item_miss(slot);
    break;
  case KOS_ITEM_DECLINE:
    break;
  }
  if (code & KOS_ITEM_HELD)
    return kos_item_inplace_protocol_held(slot, obj, key, op, args, 1);
  return kos_item_inplace_protocol(slot, obj, key, op, args, 1);
}


/* A site of more than one operand never elects. */
static KOS_NOINLINE int kos_item_inplace_cold(
    KosCacheSlot *slot,
    PyObject *obj,
    PyObject *key,
    KosOperand op,
    PyObject *const *args,
    int count) {

  KosItemInplaceBody body;

  if (slot && slot->kind == KOS_CACHE_ITEM)
    return kos_item_inplace_protocol(slot, obj, key, op, args, count);
  body = count == 1 ? kos_item_inplace_classify(op, obj, key, args[0]) : NULL;
  kos_item_elect(slot, (KosItemBody) { .inplace = body });
  if (! body)
    return kos_item_inplace_protocol(slot, obj, key, op, args, count);
  return kos_item_inplace_run(slot, body, obj, key, op, args);
}


/**
   obj[key] = obj[key] op args..., reduced left to right, through the
   site's elected body. obj and key borrowed; args borrowed, count of
   them, on the stack or one pool constant. Returns 0, or -1 with the
   error set.
*/
static inline int kos_item_inplace(
    KosCacheSlot *slot,
    PyObject *obj,
    PyObject *key,
    KosOperand op,
    PyObject *const *args,
    int count) {

  if (likely(count == 1 && slot && slot->kind == KOS_CACHE_ITEM
             && slot->u.item.fn.inplace))
    return kos_item_inplace_run(slot, slot->u.item.fn.inplace, obj, key,
                                op, args);
  return kos_item_inplace_cold(slot, obj, key, op, args, count);
}


/* === Clear, take, ref === */


/**
   del obj[key]. obj and key borrowed, held across the call. Returns
   0, or -1 with the error set.
*/
static inline int kos_item_clear(PyObject *obj, PyObject *key) {
  int rc;

  Py_INCREF(obj);
  Py_INCREF(key);
  rc = PyObject_DelItem(obj, key);
  Py_DECREF(key);
  Py_DECREF(obj);
  return rc;
}


/**
   obj[key], then del obj[key]. obj and key borrowed, held across the
   calls. Returns the item as a new reference, or NULL with the error
   set.
*/
static inline PyObject *kos_item_take(PyObject *obj, PyObject *key) {
  PyObject *item;

  Py_INCREF(obj);
  Py_INCREF(key);
  item = PyObject_GetItem(obj, key);
  if (item && unlikely(PyObject_DelItem(obj, key) < 0))
    Py_CLEAR(item);
  Py_DECREF(key);
  Py_DECREF(obj);
  return item;
}


/**
   A reference cell over obj[key]. obj and key borrowed. Returns the
   new KosItemRef, or NULL with the error set.
*/
static inline PyObject *kos_item_ref(PyObject *obj, PyObject *key) {
  return KosItemRef_New(NULL, obj, key);
}


/* === Handler support === */


/* Replace the top of the stack with an owned value. No underrun
   guard: the handler holding a tail pointer of one has proved the
   length. */
static inline void kos_item_replace_top(KosSegment **bl, PyObject *val) {
  KosSegment *s = *bl;
  PyObject *old = s->objs[s->len - 1];

  s->objs[s->len - 1] = val;
  Py_DECREF(old);
}


// The end.
