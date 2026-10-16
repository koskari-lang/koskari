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
   koskari/lib/stack.c

   Operand stack segments for the evaluator.

   Storage-only; control flow metadata (handler and jump targets,
   enabled flag) lives on the TRY, FINALLY, LOOP and FOREACH nodes of
   the dynamic chain, the region kinds. See dynchain.h.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
*/


#include "lib.h"
#include "hints.h"
#include "debug.h"
#include "tracing.h"



/* === KosSegment === */


#define KosSegment_NEEDED_SIZE(count)                        \
  (sizeof(KosSegment) + ((sizeof(PyObject *)) * count))


#define BLOCK_NEW_SIZE 16
#define BLOCK_MAX_SIZE KOS_SEGMENT_MAX_SLOTS

#define BLOCK_MAX_FREE KOS_FREELIST_CAP(256)

/* The inline discriminator (max <= KOS_SEGMENT_INLINE_SLOTS) and the
   single-step spill in KosStack_Grow both require inline capacity to
   sit strictly below the heap birth size. */
_Static_assert(KOS_SEGMENT_INLINE_SLOTS < BLOCK_NEW_SIZE,
               "inline capacity must be below heap birth size");

static size_t block_freecount = 0;

/* Linked-list cache for heap segments; same pattern used for dyn nodes and context. */
static KosSegment *block_freelist = NULL;


KosSegment *KosStack_NewEmpty(void) {
  SAY("KosStack_NewEmpty()");
  KosSegment *self = block_freelist;
  if (self) {
    block_freelist = (KosSegment *) KosSegment_BACK(self);
    block_freecount -= 1;
    KOS_TRACE_FL_ALLOC("stack_block", 1);
    SAYF(" found free, new block_freecount: %zu", block_freecount);

  } else {
    KOS_TRACE_FL_ALLOC("stack_block", 0);
    self = PyMem_Malloc(KosSegment_NEEDED_SIZE(BLOCK_NEW_SIZE));
    if (! self) {
      PyErr_SetString(PyExc_RuntimeError, "segment malloc failed");
      return NULL;
    }
    KosSegment_MAX(self) = BLOCK_NEW_SIZE;
    SAY(" no free, allocated new");
  }

  KosSegment_BACK(self) = NULL;
  KosSegment_LEN(self) = 0;

  SAYF("KosStack_NewEmpty() -> %p", (void *) self);
  return self;
}


/**
   Move an inline segment's contents (refcount-free) to a fresh heap
   segment, leaving the inline storage empty. Returns NULL on
   allocation failure with the source untouched.
*/
static KosSegment *kos_segment_spill(KosSegment *self) {
  KosSegment *heap = KosStack_NewEmpty();
  if (! heap)
    return NULL;

  memcpy(heap->objs, self->objs, self->len * sizeof(PyObject *));
  KosSegment_LEN(heap) = self->len;
  KosSegment_BACK(heap) = KosSegment_BACK(self);

  KosSegment_LEN(self) = 0;
  KosSegment_BACK(self) = NULL;
  return heap;
}


static KosSegment *KosStack_Grow(KosSegment *self) {

  /* Inline segments live inside their owning node and cannot realloc:
     spill to a fresh heap segment. The caller updates the owning
     slot; the inline storage goes dormant until its node is reused. */
  if (KosSegment_IS_INLINE(self)) {
    KosSegment *heap = kos_segment_spill(self);
    if (heap)
      KOS_TRACE_EVENTS_INC(KOS_TE_SEG_INLINE_SPILL);
    return heap;
  }

  size_t max = self->max * 2;

  /* stack starts 16, then doubles each grow up to the BLOCK_MAX_SIZE.
     The finalizer keeps any one instruction's operand count within it,
     so growth past it means bytecode that pushes without popping. */
  if (max > BLOCK_MAX_SIZE) {
    PyErr_SetString(PyExc_RuntimeError, "segment cannot grow over limit");
    return NULL;
  }

  self = PyMem_Realloc(self, KosSegment_NEEDED_SIZE(max));
  if (! self) {
    PyErr_SetString(PyExc_RuntimeError, "segment realloc failed");
    return NULL;
  }

  KosSegment_MAX(self) = max;
  return self;
}


int KosStack_Clear(KosSegment **block) {

  KosSegment *s = *block;

  if (! s->len)
    return 0;

  /* len is lowered before the release: a finalizer may collect, and
     the traversal must not visit the slots being released */
  size_t len = KosSegment_LEN(s);
  KosSegment_LEN(s) = 0;

  for (size_t i = len; i--; ) {
    Py_DECREF(s->objs[i]);
  }

  return 0;
}


void KosStack_Release(KosSegment *self) {
  SAYF("KosStack_Release(%p)", (void *) self);

  size_t len = KosSegment_LEN(self);
  KosSegment_LEN(self) = 0;

  for (size_t i = len; i--; ) {
    Py_DECREF(self->objs[i]);
  }

  /* Inline segments are embedded in their owning node: release the
     contents only; the storage lives and dies with the node. */
  if (KosSegment_IS_INLINE(self)) {
    KosSegment_BACK(self) = NULL;
    SAY(" inline segment, contents released");
    return;
  }

  /* Grown segments (max > BLOCK_NEW_SIZE) are from KosStack_Grow via
     PyMem_Realloc. Do not cache them: Valgrind reports them as indirectly
     lost when the freelist chain crosses Python-allocated memory. */
  if (self->max > BLOCK_NEW_SIZE) {
    KOS_TRACE_FL_RETURN("stack_block", 1);
    PyMem_Free(self);
    SAY(" grown segment, dealloced");
  } else if (block_freecount < BLOCK_MAX_FREE) {
    KOS_TRACE_FL_RETURN("stack_block", 0);
    block_freecount += 1;
    KosSegment_BACK(self) = (KosSegment **) block_freelist;
    block_freelist = self;
    SAYF(" deposited into freelist, new block_freecount: %zu",
         block_freecount);

  } else {
    KOS_TRACE_FL_RETURN("stack_block", 1);
    PyMem_Free(self);
    SAY(" at max freelist size, dealloced");
  }

  SAY("KosStack_Release -> void");
}


int KosStack_Traverse(
    KosSegment *b,
    visitproc visit,
    void *arg) {

  size_t i;

  if (b) {
    for (i = KosSegment_LEN(b); i--; ) {
      Py_VISIT(b->objs[i]);
    }
  }

  return 0;
}


KosSegment *KosStack_Detach(KosSegment *seg) {
  if (! seg || ! KosSegment_IS_INLINE(seg))
    return seg;
  return kos_segment_spill(seg);
}


KosSegment *KosStack_DeepCopy(KosSegment *block) {
  if (! block)
    return NULL;

  size_t len = KosSegment_LEN(block);
  KosSegment *copy;

  if (len <= BLOCK_NEW_SIZE) {
    copy = KosStack_NewEmpty();
    if (! copy)
      return NULL;
  } else {
    /* size the segment in one allocation: the next power-of-two
       capacity Grow would have reached */
    size_t cap = BLOCK_NEW_SIZE;
    while (cap < len)
      cap *= 2;

    copy = PyMem_Malloc(KosSegment_NEEDED_SIZE(cap));
    if (! copy) {
      PyErr_SetString(PyExc_RuntimeError, "segment malloc failed");
      return NULL;
    }
    KosSegment_MAX(copy) = cap;
    KosSegment_LEN(copy) = 0;
    KosSegment_BACK(copy) = NULL;
  }

  for (size_t i = 0; i < len; i++) {
    PyObject *val = block->objs[i];
    Py_INCREF(val);
    copy->objs[i] = val;
  }
  KosSegment_LEN(copy) = len;

  return copy;
}


int KosStack_PushRef(
    KosSegment **self,
    PyObject *val) {

  if (KOS_STACK_TRACE) {
    SAYF("[stack] PushRef seg=%p len=%zu accepting %R",
         (void *) *self, *self ? KosSegment_LEN(*self) : 0, val);
  }

  KosSegment *s = *self;
  if (! s) {
    s = KosStack_NewEmpty();
    if (! s)
      return -1;
    *self = s;
  }

  /* grow if needed */
  while (s->len >= s->max) {
    s = KosStack_Grow(s);
    if (! s)
      return -1;
  }

  Py_INCREF(val);
  s->objs[s->len++] = val;

  *self = s;
  return 0;
}


int KosStack_PUSH(
    KosSegment **self,
    PyObject *val) {

  if (KOS_STACK_TRACE) {
    SAYF("[stack] PUSH seg=%p len=%zu accepting %R",
         (void *) *self, *self ? KosSegment_LEN(*self) : 0, val);
  }

  KosSegment *s = *self;
  if (! s) {
    s = KosStack_NewEmpty();
    if (! s) {
      Py_DECREF(val);
      return -1;
    }
    *self = s;
  }

  /* grow if needed */
  while (s->len >= s->max) {
    s = KosStack_Grow(s);
    if (! s) {
      Py_DECREF(val);
      return -1;
    }
    *self = s;
  }

  /* stolen reference */
  s->objs[s->len++] = val;

  return 0;
}


PyObject *KosStack_Peek(KosSegment *self) {

  size_t len = self->len;
  if (! len) {
    PyErr_SetString(PyExc_RuntimeError, "stack peek underrun");
    return NULL;
  }

  /* borrowed reference */
  return self->objs[len - 1];
}


PyObject *KosStack_PeekAt(
    KosSegment *self,
    Py_ssize_t index) {

  size_t len = self->len;

  /* we don't allow wrap-around, but we don't mind negative offsets */
  if (len <= (size_t)((index < 0)? -index: index)) {
    PyErr_Format(PyExc_RuntimeError, "stack peek underrun: %zd", index);
    return NULL;
  }

  size_t ain = ((index < 0)? len + index: (size_t) index);

  /* borrowed reference */
  return self->objs[ain];
}


PyObject *KosStack_PeekUnder(KosSegment *self, size_t under) {

  size_t len = self->len;
  if (under >= len) {
    PyErr_Format(PyExc_RuntimeError, "stack peek underrun: %zu under",
                 under);
    return NULL;
  }

  /* borrowed reference */
  return self->objs[len - 1 - under];
}


int KosStack_SetUnder(KosSegment *self, size_t under, PyObject *val) {

  size_t len = self->len;
  if (under >= len) {
    Py_DECREF(val);
    PyErr_Format(PyExc_RuntimeError, "stack store underrun: %zu under",
                 under);
    return -1;
  }

  PyObject **slot = &self->objs[len - 1 - under];
  PyObject *old = *slot;
  *slot = val;
  Py_DECREF(old);
  return 0;
}


PyObject *KosStack_PopRef(KosSegment **self) {

  KosSegment *s = *self;
  if (! s) {
    PyErr_SetString(PyExc_RuntimeError, "stack pop underrun");
    return NULL;
  }

  size_t len = s->len;
  if (KOS_STACK_TRACE)
    SAYF("[stack] Pop seg=%p len=%zu", (void *) s, len);
  if (! len) {
    if (KOS_STACK_TRACE)
      SAY("underrun");
    PyErr_SetString(PyExc_RuntimeError, "stack pop underrun");
    return NULL;
  }

  s->len = --len;
  *self = s;

  /* stolen reference */
  PyObject *ret = s->objs[len];
  if (KOS_STACK_TRACE)
    SAYF("[stack] Pop seg=%p len=%zu -> %R", (void *) s, len, ret);
  return ret;
}


int KosStack_Drop1(KosSegment **self) {
  KosSegment *s = *self;
  if (unlikely(! s || ! KosSegment_LEN(s))) {
    PyErr_SetString(PyExc_RuntimeError, "stack drop underrun");
    return -1;
  }

  Py_DECREF(s->objs[--(KosSegment_LEN(s))]);
  *self = s;

  return 0;
}


int KosStack_Replace2(KosSegment **self, PyObject *val) {
  KosSegment *s = *self;
  if (unlikely(! s || KosSegment_LEN(s) < 2)) {
    Py_DECREF(val);
    PyErr_SetString(PyExc_RuntimeError, "stack replace underrun");
    return -1;
  }

  size_t len = KosSegment_LEN(s);
  PyObject *left = s->objs[len - 2];
  PyObject *right = s->objs[len - 1];

  s->objs[len - 2] = val;
  KosSegment_LEN(s) = len - 1;
  Py_DECREF(right);
  Py_DECREF(left);

  return 0;
}


int KosStack_Settle2(KosSegment **self, PyObject *val) {
  KosSegment *s = *self;
  if (unlikely(! s || KosSegment_LEN(s) < 2)) {
    Py_XDECREF(val);
    PyErr_SetString(PyExc_RuntimeError, "stack settle underrun");
    return -1;
  }

  size_t len = KosSegment_LEN(s);
  PyObject *right = s->objs[len - 1];

  KosSegment_LEN(s) = len - 1;
  if (val) {
    s->objs[len - 2] = val;
    Py_DECREF(right);
  } else {
    s->objs[len - 2] = right;
  }

  return 0;
}


int KosStack_Drop(KosSegment **self, size_t count) {
  if (unlikely(! count))
    return 0;

  KosSegment *s = *self;
  if (unlikely(! s)) {
    PyErr_SetString(PyExc_RuntimeError, "stack drop underrun");
    return -1;
  }

  size_t len = s->len;
  if (unlikely(count > len)) {
    PyErr_SetString(PyExc_RuntimeError, "stack drop underrun");
    return -1;
  }

  s->len = len - count;
  while (count--) {
    Py_DECREF(s->objs[--len]);
  }

  *self = s;
  return 0;
}


int KosStack_DropNoErr(KosSegment **self, size_t count) {
  if (! count)
    return 0;

  KosSegment *s = *self;
  if (! s)
    return -1;

  int res = 0;

  size_t len = s->len;
  if (count > len) {
    count = len;
    res = -1;
  }

  s->len = len - count;
  while (count--) {
    Py_DECREF(s->objs[--len]);
  }

  *self = s;
  return res;
}


int KosStack_Duplicate(KosSegment **self, size_t count) {

  if (! count)
    return 0;

  KosSegment *s = *self;
  if (! s) {
    PyErr_SetString(PyExc_RuntimeError, "stack dup underrun");
    return -1;
  }

  if (count > s->len) {
    PyErr_SetString(PyExc_RuntimeError, "stack dup underrun");
    return -1;
  }

  /* grow if needed; write the slot back each step so a later failure
     cannot strand values in a spilled segment */
  while (s->len + count >= s->max) {
    s = KosStack_Grow(s);
    if (! s)
      return -1;
    *self = s;
  }

  for (size_t i = count; i--; ) {
    PyObject *val = s->objs[s->len - count];
    Py_INCREF(val);
    s->objs[s->len++] = val;
  }

  *self = s;
  return 0;
}


PyObject *KosStack_PopTail(
    KosSegment **self,
    size_t count) {

  if (! count)
    return PyTuple_New(0);

  KosSegment *s = *self;
  if (! s) {
    PyErr_Format(PyExc_RuntimeError, "stack pop tail underrun."
                 " requested: %zu, available: 0", count);
    return NULL;
  }

  size_t len = s->len;

  if (count > len) {
    PyErr_Format(PyExc_RuntimeError, "stack pop tail underrun."
                 " requested: %zu, available: %zu", count, len);
    return NULL;
  }

  PyObject *ret = PyTuple_New(count);
  if (! ret)
    return NULL;

  while (count--) {
    PyTuple_SET_ITEM(ret, count, s->objs[--len]);
  }

  s->len = len;
  *self = s;

  return ret;
}


PyObject *const *KosStack_TailPtr(
    KosSegment *self,
    size_t count) {

  if (! count)
    return NULL;
  if (! self) {
    PyErr_SetString(PyExc_RuntimeError, "stack tail ptr underrun");
    return NULL;
  }

  size_t len = self->len;
  if (count > len) {
    PyErr_Format(PyExc_RuntimeError, "stack tail ptr underrun."
                 " requested: %zu, available: %zu", count, len);
    return NULL;
  }

  return &self->objs[len - count];
}


int KosStack_PopTailPtr(
    KosSegment **self,
    size_t count,
    PyObject **dest) {

  if (! count)
    return 0;

  KosSegment *s = *self;
  if (! s) {
    PyErr_Format(PyExc_RuntimeError, "stack pop tail ptr underrun."
                 " requested: %zu, available: 0", count);
    return -1;
  }

  size_t len = s->len;
  if (count > len) {
    PyErr_Format(PyExc_RuntimeError, "stack pop tail ptr underrun."
                 " requested: %zu, available: %zu", count, len);
    return -1;
  }

  while (count--) {
    dest[count] = s->objs[--len];
  }

  s->len = len;
  *self = s;

  return 0;
}


/* === KosStackSteal === */


int KosStackSteal_Tail(
    KosStackSteal *s,
    size_t count,
    KosSegment **block) {

  if (! count) {
    s->len = 0;
    s->owned_start = 0;
    s->vals = s->fast;
    return 0;
  }

  KosSegment *b = *block;
  if (! b) {
    PyErr_Format(PyExc_RuntimeError, "stack steal tail underrun."
                 " requested: %zu, available: 0", count);
    s->len = 0;
    s->owned_start = 0;
    s->vals = s->fast;
    return -1;
  }

  size_t len = b->len;
  if (count > len) {
    PyErr_Format(PyExc_RuntimeError, "stack steal tail underrun."
                 " requested: %zu, available: %zu", count, len);
    s->len = 0;
    s->owned_start = 0;
    s->vals = s->fast;
    return -1;
  }

  s->len = count;
  s->owned_start = 0;

  if (count > KOS_STACK_STEAL_FAST_SIZE) {
    s->vals = PyMem_Malloc(count * sizeof(PyObject *));
    if (! s->vals) {
      PyErr_NoMemory();
      s->len = 0;
      s->owned_start = 0;
      s->vals = s->fast;
      return -1;
    }
  } else {
    s->vals = s->fast;
  }

  while (count--) {
    s->vals[count] = b->objs[--len];
  }

  b->len = len;
  *block = b;

  return 0;
}


void KosStackSteal_Free(KosStackSteal *s) {

  for (size_t i = s->len; i > s->owned_start; i--)
    Py_DECREF(s->vals[i - 1]);

  if (s->vals != s->fast)
    PyMem_Free(s->vals);

  s->len = 0;
  s->owned_start = 0;
  s->vals = s->fast;
}


int KosStackSteal_TailPrepend(
    KosStackSteal *s,
    PyObject *prefix,
    size_t count,
    KosSegment **block) {

  if (! count) {
    s->len = 1;
    s->owned_start = 1;
    s->vals = s->fast;
    s->fast[0] = prefix;
    return 0;
  }

  KosSegment *b = *block;
  if (! b) {
    PyErr_Format(PyExc_RuntimeError, "stack steal tail underrun."
                 " requested: %zu, available: 0", count);
    s->len = 0;
    s->owned_start = 0;
    s->vals = s->fast;
    return -1;
  }

  size_t len = b->len;
  if (count > len) {
    PyErr_Format(PyExc_RuntimeError, "stack steal tail underrun."
                 " requested: %zu, available: %zu", count, len);
    s->len = 0;
    s->owned_start = 0;
    s->vals = s->fast;
    return -1;
  }

  size_t total = count + 1;
  s->len = total;
  s->owned_start = 1;

  if (total > KOS_STACK_STEAL_FAST_SIZE) {
    s->vals = PyMem_Malloc(total * sizeof(PyObject *));
    if (! s->vals) {
      PyErr_NoMemory();
      s->len = 0;
      s->owned_start = 0;
      s->vals = s->fast;
      return -1;
    }
  } else {
    s->vals = s->fast;
  }

  s->vals[0] = prefix;
  while (count--) {
    s->vals[count + 1] = b->objs[--len];
  }

  b->len = len;
  *block = b;

  return 0;
}


PyObject *KosStack_PopTailDict(
    KosSegment **self,
    size_t count) {

  if (! count)
    return PyDict_New();

  KosSegment *s = *self;
  if (! s) {
    PyErr_SetString(PyExc_RuntimeError, "stack pop tail dict underrun");
    return NULL;
  }

  size_t tcount = count * 2;

  if (tcount > s->len) {
    PyErr_SetString(PyExc_RuntimeError, "stack pop tail dict underrun");
    return NULL;
  }

  PyObject *ret = PyDict_NewPresized(count);
  if (! ret)
    return NULL;

  /* every insert first, the dict taking its own references; the
     segment gives its entries up only once all of them are in, so a
     failed insert leaves it owning every entry for the unwind to
     release once */
  size_t base = s->len - tcount;
  for (size_t i = base; i < s->len; i += 2) {
    if (PyDict_SetItem(ret, s->objs[i], s->objs[i + 1]) < 0) {
      Py_DECREF(ret);
      return NULL;
    }
  }

  s->len = base;
  for (size_t i = base; i < base + tcount; i++)
    Py_DECREF(s->objs[i]);

  *self = s;
  return ret;
}


PyObject *KosStack_PopTailDictNames(
    KosSegment **self,
    size_t count) {

  PyObject *ret = NULL;

  KosSegment *s = *self;

  PyObject *names = KosStack_PopRef(&s);
  if (! names)
    return NULL;

  /* might be a copy */
  *self = s;

  if (! PyTuple_CheckExact(names)) {
    PyErr_Format(PyExc_RuntimeError, "stack pop tail dict expected names"
                 " tuple, got %R", names);
    goto failure;
  }

  size_t l = (size_t) PyTuple_GET_SIZE(names);
  if (l != count) {
    PyErr_Format(PyExc_RuntimeError, "mismatch in names size (%zu) and count"
                 " of values (%zu)", l, count);
    goto failure;
  }

  if (count > s->len) {
    PyErr_SetString(PyExc_RuntimeError, "stack pop tail dict underrun");
    goto failure;
  }

  ret = PyDict_NewPresized(count);
  if (! ret)
    goto failure;

  /* every insert first, in the order the values arrived; the segment
     gives its entries up only once all of them are in */
  {
    size_t base = s->len - count;
    for (size_t j = 0; j < count; j++) {
      if (PyDict_SetItem(ret, PyTuple_GET_ITEM(names, j),
                         s->objs[base + j]) < 0)
        goto failure;
    }

    s->len = base;
    for (size_t j = 0; j < count; j++)
      Py_DECREF(s->objs[base + j]);
  }

  Py_DECREF(names);
  return ret;

 failure:
  Py_XDECREF(names);
  Py_XDECREF(ret);
  return NULL;
}


PyObject *KosStack_PopTailDictFromNames(
    KosSegment **self,
    PyObject *names) {

  PyObject *ret = NULL;

  if (! PyTuple_CheckExact(names)) {
    PyErr_Format(PyExc_RuntimeError, "pop tail dict from names expected"
                 " tuple, got %R", names);
    return NULL;
  }

  size_t count = (size_t) PyTuple_GET_SIZE(names);

  if (! count)
    return PyDict_New();

  KosSegment *s = *self;
  if (! s) {
    PyErr_SetString(PyExc_RuntimeError, "stack pop tail dict from names"
                    " underrun");
    return NULL;
  }

  if (count > s->len) {
    PyErr_SetString(PyExc_RuntimeError, "stack pop tail dict from names"
                    " underrun");
    return NULL;
  }

  ret = PyDict_NewPresized(count);
  if (! ret)
    return NULL;

  /* every insert first; the segment gives its entries up only once
     all of them are in, so a failed insert leaves it owning every
     entry for the unwind to release once */
  size_t base = s->len - count;
  for (size_t j = 0; j < count; j++) {
    if (PyDict_SetItem(ret, PyTuple_GET_ITEM(names, j),
                       s->objs[base + j]) < 0) {
      Py_DECREF(ret);
      return NULL;
    }
  }

  s->len = base;
  for (size_t j = 0; j < count; j++)
    Py_DECREF(s->objs[base + j]);

  *self = s;
  return ret;
}


/* === unit lifecycle === */


int koskari_stack_init(PyObject *mod) {
  /* Nothing to register: the unit has no Python-visible surface since
     the Stack wrapper type was removed. Kept as the standard per-unit
     init hook (lib.c calls every unit's). */
  (void) mod;
  return 0;
}


int koskari_stack_clear(PyObject *mod) {

  /* clear the freelist */
  while (block_freelist) {
    KosSegment *tmp = block_freelist;
    block_freelist = (KosSegment *) KosSegment_BACK(tmp);
    PyMem_Free(tmp);
  }
  block_freecount = 0;

  return 0;
}


// The end.
