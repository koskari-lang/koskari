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

   Stack and StackBlock types for operand storage.

   The Stack type provides a block-structured value stack for the
   evaluator. This is storage-only; control flow metadata (block style,
   labels, enabled flag) has been moved to KosDynRegion nodes in the
   dynamic chain. See dynchain.h.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
   :ai-assistant: Claude Opus 4.5 via Cursor
*/


#include "lib.h"
#include "debug.h"


/* === StackBlock === */


#define KosStackBlock_NEEDED_SIZE(count)                        \
  (sizeof(KosStackBlock) + ((sizeof(PyObject *)) * count))


#define BLOCK_NEW_SIZE 16
#define BLOCK_MAX_SIZE 256

#define BLOCK_MAX_FREE 256

static size_t block_freecount = 0;

/* Linked-list cache for stack blocks; same pattern used for dyn nodes and context. */
static KosStackBlock *block_freelist = NULL;


static KosStackBlock *KosStackBlock_New(KosStackBlock *parent) {
  SAY("KosStackBlock_New()");
  KosStackBlock *self = block_freelist;
  if (self) {
    block_freelist = KosStackBlock_PARENT(self);
    block_freecount -= 1;
    SAYF(" found free, new block_freecount: %zu", block_freecount);

  } else {
    self = PyMem_Malloc(KosStackBlock_NEEDED_SIZE(BLOCK_NEW_SIZE));
    if (! self) {
      PyErr_SetString(PyExc_RuntimeError, "block malloc failed");
      return NULL;
    }
    KosStackBlock_MAX(self) = BLOCK_NEW_SIZE;
    SAY(" no free, allocated new");
  }

  KosStackBlock_PARENT(self) = parent;
  KosStackBlock_REFCOUNT(self) = 1;
  KosStackBlock_LEN(self) = 0;

  SAYF("KosStackBlock_New() -> %p", (void *) self);
  return self;
}


static KosStackBlock *KosStackBlock_NewSized(
    KosStackBlock *parent,
    size_t max) {

  SAY("KosStackBlock_NewSized()");

  KosStackBlock *self = block_freelist;
  if (self) {
    block_freelist = KosStackBlock_PARENT(self);
    block_freecount -= 1;
    SAYF(" found free, new block_freecount: %zu", block_freecount);

    if (self->max < max) {
      self = PyMem_Realloc(self, KosStackBlock_NEEDED_SIZE(max));
      if (! self) {
        PyErr_SetString(PyExc_RuntimeError, "block realloc failed");
        return NULL;
      }
      KosStackBlock_MAX(self) = max;
    }

  } else {
    self = PyMem_Malloc(KosStackBlock_NEEDED_SIZE(max));
    if (! self) {
      PyErr_SetString(PyExc_RuntimeError, "block malloc failed");
      return NULL;
    }
    KosStackBlock_MAX(self) = max;
  }

  KosStackBlock_PARENT(self) = parent;
  KosStackBlock_REFCOUNT(self) = 1;
  KosStackBlock_LEN(self) = 0;

  SAYF("KosStackBlock_NewSized() -> %p", (void *) self);
  return self;
}


static KosStackBlock *KosStackBlock_Grow(KosStackBlock *self) {
  size_t max = self->max * 2;

  /* stack starts 16, then doubles each grow up to the BLOCK_MAX_SIZE.
     Anything past that is considered an error, indicating there's
     some problem in the bytecode that is endlessly growing the stack */
  if (max > BLOCK_MAX_SIZE) {
    PyErr_SetString(PyExc_RuntimeError, "block cannot grow over limit");
    return NULL;
  }

  self = PyMem_Realloc(self, KosStackBlock_NEEDED_SIZE(max));
  if (! self) {
    PyErr_SetString(PyExc_RuntimeError, "block realloc failed");
    return NULL;
  }

  KosStackBlock_MAX(self) = max;
  return self;
}


int KosStackBlock_CopyForWrite(KosStackBlock **block) {
  KosStackBlock *b = *block;

  SAYF("KosStackBlock_CopyForWrite(%p)", (void *) *block);

  if (! b) {
    b = KosStackBlock_New(NULL);
    if (! b)
      return -1;
    *block = b;
    SAY("  allocated new block for NULL stack");
    return 0;
  }

  if (KosStackBlock_REFCOUNT(b) == 1) {
    SAY("  only 1 ref, no copy needed");
    return 0;

  } else {
    SAYF("  refcount %zu, making a copy", KosStackBlock_REFCOUNT(b));

    KosStackBlock *new = KosStackBlock_NewSized(KosStackBlock_PARENT(b),
                                                KosStackBlock_MAX(b));
    if (! new)
      return -1;

    for (size_t i = KosStackBlock_LEN(b); i--; ) {
      PyObject *v = b->objs[i];
      Py_INCREF(v);
      new->objs[i] = v;
    }
    KosStackBlock_LEN(new) = KosStackBlock_LEN(b);

    *block = new;
    KosStackBlock_REFCOUNT(b)--;
    SAYF("  new refcount of %p is %zu", (void *) b, KosStackBlock_REFCOUNT(b));
    SAYF("  copy is %p with refcount %zu",
         (void *) new, KosStackBlock_REFCOUNT(new));

    SAY("KosStackBlock_CopyForWrite -> 0");
    return 0;
  }
}


int KosStackBlock_ClearBlock(KosStackBlock **block) {

  if (KosStackBlock_EnsureExclusive(block))
    return -1;

  KosStackBlock *s = *block;

  for (size_t i = s->len; i--; ) {
    Py_DECREF(s->objs[i]);
  }

  KosStackBlock_LEN(s) = 0;

  return 0;
}


void KosStackBlock_Release(KosStackBlock *self) {
  KosStackBlock_ReleaseExclude(self, NULL);
}


void KosStackBlock_ReleaseExclude(KosStackBlock *self, PyObject *exclude) {
  SAYF("KosStackBlock_Release(%p)", (void *) self);

  if (self->refcount > 1) {
    KosStackBlock_REFCOUNT(self)--;
    SAYF(" refcount remaining: %zu", KosStackBlock_REFCOUNT(self));

  } else {
    for (size_t i = self->len; i--; ) {
      PyObject *o = self->objs[i];
      if (exclude && o == exclude) {
        self->objs[i] = NULL;
        continue;
      }
      Py_DECREF(o);
    }

    if (block_freecount < BLOCK_MAX_FREE) {
      KosStackBlock_REFCOUNT(self) = 0;
      KosStackBlock_LEN(self) = 0;

      block_freecount += 1;
      KosStackBlock_PARENT(self) = block_freelist;
      block_freelist = self;
      SAYF(" deposited into freelist, new block_freecount: %zu",
           block_freecount);

    } else {
      PyMem_Free(self);
      SAY(" at max freelist size, dealloced");
    }
  }

  SAY("KosStackBlock_Release -> void");
}


void KosStackBlock_ReleaseChainExclude(KosStackBlock *block, PyObject *exclude) {
  KosStackBlock *b = block;
  while (b) {
    KosStackBlock *p = KosStackBlock_PARENT(b);
    KosStackBlock_ReleaseExclude(b, exclude);
    b = p;
  }
}


KosStackBlock *KosStackBlock_COW(KosStackBlock *block) {
  SAYF("KosStackBlock_COW(%p)", (void *) block);

  if (! block)
    return NULL;

  KosStackBlock *b = block;
  while (b) {
    KosStackBlock_REFCOUNT(b)++;
    SAYF("  block %p refcount now %zu", (void *) b, KosStackBlock_REFCOUNT(b));
    b = KosStackBlock_PARENT(b);
  }

  SAYF("KosStackBlock_COW() -> %p", (void *) block);
  return block;
}


KosStackBlock *KosStackBlock_DeepCopy(KosStackBlock *block) {
  if (! block)
    return NULL;

  /* Count total slots */
  size_t total = 0;
  KosStackBlock *b = block;
  while (b) {
    total += KosStackBlock_LEN(b);
    b = KosStackBlock_PARENT(b);
  }
  if (total == 0) {
    KosStackBlock *empty = KosStackBlock_New(NULL);
    return empty;
  }

  /* Collect values top to bottom (caller will push in reverse to rebuild) */
  PyObject **vals = PyMem_Malloc(total * sizeof(PyObject *));
  if (! vals) {
    PyErr_NoMemory();
    return NULL;
  }
  size_t idx = 0;
  b = block;
  while (b) {
    size_t len = KosStackBlock_LEN(b);
    for (size_t i = len; i--; ) {
      vals[idx++] = b->objs[i];
    }
    b = KosStackBlock_PARENT(b);
  }

  KosStackBlock *new_chain = NULL;
  for (size_t i = total; i--; ) {
    if (KosStackBlock_PushRef(&new_chain, vals[i]) < 0) {
      if (new_chain) {
        KosStackBlock *r = new_chain;
        while (r) {
          KosStackBlock *p = KosStackBlock_PARENT(r);
          KosStackBlock_Release(r);
          r = p;
        }
      }
      PyMem_Free(vals);
      return NULL;
    }
  }
  PyMem_Free(vals);
  return new_chain;
}


static int KosStackBlock_Push(
    KosStackBlock **self,
    PyObject *val) {

  if (KOS_STACK_TRACE) {
    SAYF("[stack] Push block=%p len=%zu accepting %R",
         (void *) *self, KosStackBlock_LEN(*self), val);
  }

  if (KosStackBlock_EnsureExclusive(self))
    return -1;

  KosStackBlock *s = *self;

  /* grow if needed */
  while (s->len >= s->max) {
    s = KosStackBlock_Grow(s);
    if (! s)
      return -1;
  }

  Py_INCREF(val);
  s->objs[s->len++] = val;

  *self = s;
  return 0;
}


int KosStackBlock_PushRef(KosStackBlock **block, PyObject *val) {
  return KosStackBlock_Push(block, val);
}


static int KosStackBlock_PUSH(
    KosStackBlock **self,
    PyObject *val) {

  if (KosStackBlock_EnsureExclusive(self))
    return -1;

  KosStackBlock *s = *self;

  /* grow if needed */
  while (s->len >= s->max) {
    s = KosStackBlock_Grow(s);
    if (! s)
      return -1;
  }

  /* stolen reference */
  s->objs[s->len++] = val;

  *self = s;
  return 0;
}


static int KosStackBlock_ExtendPtr(
    KosStackBlock **self,
    PyObject *const *elms,
    size_t count,
    int reverse) {

  if (! count)
    return 0;

  if (KosStackBlock_EnsureExclusive(self))
    return -1;

  KosStackBlock *s = *self;

  size_t needed = s->len + count;
  while (needed >= s->max) {
    s = KosStackBlock_Grow(s);
    if (! s)
      return -1;
  }

  size_t offset = s->len;
  PyObject *val = NULL;

  if (reverse) {
    while (count--) {
      val = elms[count];
      Py_XINCREF(val);
      s->objs[offset++] = val;
    }
  } else {
    for (size_t i = 0; i < count; i++) {
      val = elms[i];
      Py_XINCREF(val);
      s->objs[offset++] = val;
    }
  }
  s->len = offset;

  *self = s;
  return 0;
}


static int KosStackBlock_EXTENDPTR(
    KosStackBlock **self,
    PyObject *const *elms,
    size_t count,
    int reverse) {

  if (! count)
    return 0;

  if (KosStackBlock_EnsureExclusive(self))
    return -1;

  KosStackBlock *s = *self;

  size_t needed = s->len + count;
  while (needed >= s->max) {
    s = KosStackBlock_Grow(s);
    if (! s) {
      while (count--) {
        Py_XDECREF(elms[count]);
      }
      return -1;
    }
  }

  size_t offset = s->len;

  if (reverse) {
    while (count--) {
      s->objs[offset++] = elms[count];
    }
  } else {
    for (size_t i = 0; i < count; i++) {
      s->objs[offset++] = elms[i];
    }
  }
  s->len = offset;

  *self = s;
  return 0;
}


static PyObject *KosStackBlock_Peek(KosStackBlock *self) {

  size_t len = self->len;
  if (! len) {
    PyErr_SetString(PyExc_RuntimeError, "stack peek underrun");
    return NULL;
  }

  /* borrowed reference */
  return self->objs[len - 1];
}


static PyObject *KosStackBlock_PeekAt(
    KosStackBlock *self,
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


static int KosStackBlock_Pull(KosStackBlock **self, size_t count) {

  if (! count)
    return 0;

  KosStackBlock *s = *self;

  KosStackBlock *parent = KosStackBlock_PARENT(s);
  if (! parent) {
    PyErr_SetString(PyExc_RuntimeError, "stack pull failed; no parent block");
    return -1;
  }

  if (parent->len < count) {
    PyErr_SetString(PyExc_RuntimeError, "stack pull failed; parent underrun");
    return -1;
  }

  if (KosStackBlock_EnsureExclusive(&s))
    return -1;

  while (s->len + count >= s->max) {
    s = KosStackBlock_Grow(s);
    if (! s)
      return -1;
  }

  for (size_t i = parent->len - count; count--; i++) {
    PyObject *val = parent->objs[i];
    Py_INCREF(val);
    s->objs[s->len++] = val;
  }

  *self = s;
  return 0;
}


static PyObject *KosStackBlock_Pop(KosStackBlock **self) {

  KosStackBlock *s = *self;
  if (! s) {
    PyErr_SetString(PyExc_RuntimeError, "stack pop underrun");
    return NULL;
  }

  size_t len = s->len;
  if (KOS_STACK_TRACE)
    SAYF("[stack] Pop block=%p len=%zu", (void *) s, len);
  if (! len) {
    if (KOS_STACK_TRACE)
      SAY("underrun");
    PyErr_SetString(PyExc_RuntimeError, "stack pop underrun");
    return NULL;
  }

  if (KosStackBlock_EnsureExclusive(&s))
    return NULL;

  s->len = --len;
  *self = s;

  /* stolen reference */
  PyObject *ret = s->objs[len];
  if (KOS_STACK_TRACE)
    SAYF("[stack] Pop block=%p len=%zu -> %R", (void *) s, len, ret);
  return ret;
}


PyObject *KosStackBlock_PopRef(KosStackBlock **block) {
  return KosStackBlock_Pop(block);
}


int KosStackBlock_Drop(KosStackBlock **self, size_t count) {
  if (! count)
    return 0;

  KosStackBlock *s = *self;
  if (! s) {
    PyErr_SetString(PyExc_RuntimeError, "stack drop underrun");
    return -1;
  }

  size_t len = s->len;
  if (count > len) {
    PyErr_SetString(PyExc_RuntimeError, "stack drop underrun");
    return -1;
  }

  if (KosStackBlock_EnsureExclusive(&s))
    return -1;

  while (count--) {
    Py_DECREF(s->objs[--len]);
  }
  s->len = len;

  *self = s;
  return 0;
}


int KosStackBlock_DropNoErr(KosStackBlock **self, size_t count) {
  if (! count)
    return 0;

  KosStackBlock *s = *self;
  if (! s)
    return -1;

  int res = 0;

  size_t len = s->len;
  if (count > len) {
    count = len;
    res = -1;
  }

  if (KosStackBlock_EnsureExclusive(&s))
    return -1;

  while (count--) {
    Py_DECREF(s->objs[--len]);
  }
  s->len = len;

  *self = s;
  return res;
}


int KosStackBlock_Duplicate(KosStackBlock **self, size_t count) {

  if (! count)
    return 0;

  KosStackBlock *s = *self;
  if (! s) {
    PyErr_SetString(PyExc_RuntimeError, "stack dup underrun");
    return -1;
  }

  if (count > s->len) {
    PyErr_SetString(PyExc_RuntimeError, "stack dup underrun");
    return -1;
  }

  if (KosStackBlock_EnsureExclusive(&s))
    return -1;

  /* grow if needed */
  while (s->len + count >= s->max) {
    s = KosStackBlock_Grow(s);
    if (! s)
      return -1;
  }

  for (size_t i = count; i--; ) {
    PyObject *val = s->objs[s->len - count];
    Py_INCREF(val);
    s->objs[s->len++] = val;
  }

  *self = s;
  return 0;
}


static int KosStackBlock_Replace(
    KosStackBlock **self,
    size_t index,
    PyObject *value) {

  if (! value) {
    PyErr_SetString(PyExc_RuntimeError, "stack replace cannot assign NULL");
    return -1;
  }

  KosStackBlock *s = *self;

  if (index >= s->len) {
    PyErr_SetString(PyExc_RuntimeError, "stack replace underrun");
    return -1;
  }

  if (KosStackBlock_EnsureExclusive(&s))
    return -1;

  Py_INCREF(value);
  Py_DECREF(s->objs[index]);
  s->objs[index] = value;

  *self = s;
  return 0;
}


PyObject *KosStackBlock_PopTail(
    KosStackBlock **self,
    size_t count) {

  if (! count)
    return PyTuple_New(0);

  KosStackBlock *s = *self;
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

  if (KosStackBlock_EnsureExclusive(&s)) {
    Py_DECREF(ret);
    return NULL;
  }

  while (count--) {
    PyTuple_SET_ITEM(ret, count, s->objs[--len]);
  }

  s->len = len;
  *self = s;

  return ret;
}


PyObject *const *KosStackBlock_TailPtr(
    KosStackBlock *self,
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


int KosStackBlock_PushBlock(KosStackBlock **block) {
  KosStackBlock *top = *block;
  if (! top) {
    top = KosStackBlock_New(NULL);
    if (! top) {
      PyErr_SetString(PyExc_RuntimeError,
                      "could not push block; alloc failed");
      return -1;
    }
    *block = top;
  }
  KosStackBlock *new_block = KosStackBlock_New(top);
  if (! new_block) {
    PyErr_SetString(PyExc_RuntimeError, "could not push block; alloc failed");
    return -1;
  }
  *block = new_block;
  return 0;
}


int KosStackBlock_DropBlock(KosStackBlock **block) {
  KosStackBlock *top = *block;
  if (! top) {
    PyErr_SetString(PyExc_RuntimeError, "drop block failed; no top block");
    return -1;
  }
  KosStackBlock *back = top->back;
  if (! back) {
    PyErr_SetString(PyExc_RuntimeError, "cannot drop root block");
    return -1;
  }
  *block = back;
  KosStackBlock_Release(top);
  return 0;
}


static int KosStackBlock_PopTailPtr(
    KosStackBlock **self,
    size_t count,
    PyObject **dest) {

  if (! count)
    return 0;

  KosStackBlock *s = *self;
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

  if (KosStackBlock_EnsureExclusive(&s))
    return -1;

  s->len = len;
  *self = s;

  return 0;
}


static PyObject *KosStackBlock_PopTailDict(
    KosStackBlock **self,
    size_t count) {

  if (! count)
    return PyDict_New();

  KosStackBlock *s = *self;
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

  if (KosStackBlock_EnsureExclusive(&s)) {
    Py_DECREF(ret);
    return NULL;
  }

  s->len -= tcount;

  size_t i = s->len;
  while (count--) {
    PyObject *key = s->objs[i++];
    PyObject *val = s->objs[i++];

    PyDict_SetItem(ret, key, val);

    Py_DECREF(key);
    Py_DECREF(val);
  }

  *self = s;
  return ret;
}


PyObject *KosStackBlock_PopTailDictNames(
    KosStackBlock **self,
    size_t count) {

  PyObject *ret = NULL;

  KosStackBlock *s = *self;

  PyObject *names = KosStackBlock_Pop(&s);
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

  /* set the stack height down ahead of time. We'll work our way back
     up in order from there, because we want to insert in the same
     order things arrived on the stack */
  s->len -= count;

  size_t i = s->len, j = 0;
  while (count--) {
    PyObject *key = PyTuple_GET_ITEM(names, j++);
    PyObject *val = s->objs[i++];

    PyDict_SetItem(ret, key, val);

    Py_DECREF(val);
  }

  Py_DECREF(names);
  return ret;

 failure:
  Py_XDECREF(names);
  Py_XDECREF(ret);
  return NULL;
}


/* === Stack methods === */


static PyObject *stack_m_push(PyObject *self, PyObject *value) {
  if (KosStack_Push(self, value)) {
    return NULL;
  } else {
    Py_RETURN_NONE;
  }
}


static PyObject *stack_m_extend(PyObject *self, PyObject *seq) {
  if (KosStack_Extend(self, seq, 0)) {
    return NULL;
  } else {
    Py_RETURN_NONE;
  }
}


static PyObject *stack_m_pull(PyObject *self, PyObject *count) {
  if (! PyLong_CheckExact(count)) {
    PyErr_SetString(PyExc_TypeError, "pull count must be an int");
    return NULL;
  }

  if (KosStack_Pull(self, PyLong_AsSize_t(count))) {
    return NULL;
  } else {
    Py_RETURN_NONE;
  }
}


static PyObject *stack_m_pop(PyObject *self) {
  return KosStack_Pop(self);
}


static PyObject *stack_m_peek(PyObject *self) {
  PyObject *ret = KosStack_Peek(self);
  Py_XINCREF(ret);
  return ret;
}


static PyObject *stack_m_peek_at(PyObject *self, PyObject *index) {

  if (! PyLong_CheckExact(index)) {
    PyErr_SetString(PyExc_TypeError, "peek_at index must be an int");
    return NULL;
  }

  PyObject *ret = KosStack_PeekAt(self, PyLong_AsSsize_t(index));
  Py_XINCREF(ret);
  return ret;
}


static PyObject *stack_m_drop(PyObject *self, PyObject *count) {

  if (! PyLong_CheckExact(count)) {
    PyErr_SetString(PyExc_TypeError, "drop tail count must be an int");
    return NULL;
  }

  if (KosStack_Drop(self, PyLong_AsSize_t(count))) {
    return NULL;
  } else {
    Py_RETURN_NONE;
  }
}


static PyObject *stack_m_replace(PyObject *self, PyObject *args) {
  unsigned int i = 0;
  PyObject *value = NULL;

  if (! PyArg_ParseTuple(args, "IO:Stack.replace", &i, &value))
    return NULL;

  if (KosStack_Replace(self, (size_t) i, value)) {
    return NULL;
  } else {
    Py_RETURN_NONE;
  }
}


static PyObject *stack_m_duplicate(PyObject *self, PyObject *count) {

  if (! PyLong_CheckExact(count)) {
    PyErr_SetString(PyExc_TypeError, "duplicate count must be an int");
    return NULL;
  }

  if (KosStack_Duplicate(self, PyLong_AsSize_t(count))) {
    return NULL;
  } else {
    Py_RETURN_NONE;
  }
}


static PyObject *stack_m_pop_tail(PyObject *self, PyObject *count) {

  if (! PyLong_CheckExact(count)) {
    PyErr_SetString(PyExc_TypeError, "pop tail count must be an int");
    return NULL;
  }

  return KosStack_PopTail(self, PyLong_AsSize_t(count));
}


static PyObject *stack_m_pop_tail_dict(PyObject *self, PyObject *count) {

  if (! PyLong_CheckExact(count)) {
    PyErr_SetString(PyExc_TypeError, "pop tail dict count must be an int");
    return NULL;
  }

  return KosStack_PopTailDict(self, PyLong_AsSize_t(count));
}


static PyObject *stack_m_push_block(PyObject *self) {
  if (KosStack_PushBlock(self)) {
    return NULL;
  } else {
    Py_RETURN_NONE;
  }
}


static PyObject *stack_m_pop_block(PyObject *self, PyObject *count) {

  if (! PyLong_CheckExact(count)) {
    PyErr_SetString(PyExc_TypeError, "pop block count must be an int");
    return NULL;
  }

  if (KosStack_PopBlock(self, PyLong_AsSize_t(count))) {
    return NULL;
  } else {
    Py_RETURN_NONE;
  }
}


static PyObject *stack_m_drop_block(PyObject *self) {
  if (KosStack_DropBlock(self)) {
    return NULL;
  } else {
    Py_RETURN_NONE;
  }
}


static PyObject *stack_m_block_len(PyObject *self) {
  KosStackBlock *block = KosStack_BLOCK(self);
  return PyLong_FromSize_t(block->len);
}


static PyObject *stack_m_block_view(PyObject *self) {
  KosStackBlock *bl = KosStack_BLOCK(self);
  size_t len = KosStackBlock_LEN(bl);
  PyObject *res = PyTuple_New(len);

  while (len--) {
    PyObject *val = bl->objs[len];
    Py_INCREF(val);
    PyTuple_SET_ITEM(res, len, val);
  }

  return res;
}


static PyObject *stack_m_depth(PyObject *self) {
  unsigned long len = 0;
  KosStackBlock *block = KosStack_BLOCK(self);

  while (block) {
    len++;
    block = block->back;
  }

  return PyLong_FromUnsignedLong(len);
}


static PyObject *stack_m_total(PyObject *self) {
  unsigned long len = 0;
  KosStackBlock *block = KosStack_BLOCK(self);

  while (block) {
    len += block->len;
    block = block->back;
  }

  return PyLong_FromUnsignedLong(len);
}


static PyObject *stack_m_clear(PyObject *self) {
  if (KosStack_Clear(self)) {
    return NULL;
  } else {
    Py_RETURN_NONE;
  }
}


static PyObject *stack_m_copy(PyObject *self) {
  return KosStack_Copy(self);
}


static PyMethodDef stack_methods[] = {
  { "push", (PyCFunction) stack_m_push, METH_O,
    "Inserts value at the top of the current block's stack\n"
    "S.push(V) => None" },

  { "extend", (PyCFunction) stack_m_extend, METH_O,
    "Inserts values in-order onto the top of the current block's stack\n"
    "S.extend(I) => None" },

  { "pop", (PyCFunction) stack_m_pop, METH_NOARGS,
    "Removes and returns the value at the top of the current block's stack\n"
    "S.pop() => V" },

  { "peek", (PyCFunction) stack_m_peek, METH_NOARGS,
    "The value at the top of the current block's stack\n"
    "S.peek() => V" },

  { "peek_at", (PyCFunction) stack_m_peek_at, METH_O,
    "The value at the index of the current block's stack\n"
    "S.peek_at(index) => V" },

  { "pull", (PyCFunction) stack_m_pull, METH_O,
    "Copies values from parent block to current block\n"
    "S.pull(count) => None" },

  { "drop", (PyCFunction) stack_m_drop, METH_O,
    "Removes values from the top of the current block's stack\n"
    "S.drop(count) => None" },

  { "replace", (PyCFunction) stack_m_replace, METH_VARARGS,
    "Replaces entry at index in the current block's stack with value\n"
    "S.replace(index, value) => None" },

  { "duplicate", (PyCFunction) stack_m_duplicate, METH_O,
    "Duplicates values from the top of the current block's stack\n"
    "S.duplicate(count) => None" },

  { "pop_tail", (PyCFunction) stack_m_pop_tail, METH_O,
    "Removes and returns values from the top of the current block's stack\n"
    "S.pop_tail(count) => (V, ...)" },

  { "pop_tail_dict",  (PyCFunction) stack_m_pop_tail_dict, METH_O,
    "S.pop_tail_dict(count) => {K: V, ...}" },

  { "push_block", (PyCFunction) stack_m_push_block, METH_NOARGS,
    "Allocates a new block and makes it the current block\n"
    "S.push_block() => None" },

  { "pop_block", (PyCFunction) stack_m_pop_block, METH_O,
    "Removes the top block, pushing values onto the parent block\n"
    "S.pop_block(count) => None" },

  { "drop_block", (PyCFunction) stack_m_drop_block, METH_NOARGS,
    "Removes the top block\n"
    "S.drop_block() => None" },

  { "length", (PyCFunction) stack_m_block_len, METH_NOARGS,
    "The used size of the current block's stack\n"
    "S.length() => i" },

  { "view", (PyCFunction) stack_m_block_view, METH_NOARGS,
    "Tuple showing the current block's stack\n"
    "S.view() => (...)" },

  { "depth", (PyCFunction) stack_m_depth, METH_NOARGS,
    "The count of blocks\n"
    "S.depth() => i" },

  { "total", (PyCFunction) stack_m_total, METH_NOARGS,
    "The used size of every block's stack\n"
    "S.total() => i" },

  { "clear", (PyCFunction) stack_m_clear, METH_NOARGS,
    "Pops all blocks in the stack except the first, which is emptied\n"
    "S.clear() => None" },

  { "copy", (PyCFunction) stack_m_copy, METH_NOARGS,
    "Creates a copy-on-write clone of this stack\n"
    "S.copy() => Stack" },

  { NULL, NULL, 0, NULL },
};


/* === Stack type === */


static PyObject *stack_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  if (PyTuple_GET_SIZE(args) || (kwds && PyDict_Size(kwds))) {
    PyErr_SetString(PyExc_TypeError, "Stack takes no arguments");
    return NULL;
  }

  return KosStack_New();
}


static void stack_dealloc(PyObject *self) {
  KosStackBlock *block = KosStack_BLOCK(self);
  while (block) {
    KosStackBlock *p = KosStackBlock_PARENT(block);
    KosStackBlock_Release(block);
    block = p;
  }

  Py_TYPE(self)->tp_free(self);
}


static int stack_traverse(
    PyObject *self,
    visitproc visit,
    void *arg) {

  KosStackBlock *b = KosStack_BLOCK(self);
  size_t i;
  while (b) {
    for (i = b->len; i--; ) {
      Py_VISIT(b->objs[i]);
    }
    b = b->back;
  }
  return 0;
}


static int stack_clear(PyObject *self) {
  KosStackBlock *block = KosStack_BLOCK(self);
  while (block) {
    KosStackBlock *p = KosStackBlock_PARENT(block);
    KosStackBlock_Release(block);
    block = p;
  }

  return 0;
}


PyTypeObject KosStackType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "Stack",
  sizeof(KosStack),
  0,

  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_methods = stack_methods,
  .tp_new = stack_new,
  .tp_dealloc = stack_dealloc,
  .tp_traverse = stack_traverse,
  .tp_clear = stack_clear,
};


/* === Stack public API === */


PyObject *KosStack_New(void) {
  SAY("KosStack_New()");

  PyObject *self = PyObject_New(PyObject, &KosStackType);
  if (! self)
    return NULL;

  KosStack_BLOCK(self) = KosStackBlock_New(NULL);

  SAYF("KosStack_New() -> %p", (void *) self);
  return self;
}


PyObject *KosStack_Copy(PyObject *original) {
  SAY("KosStack_Copy()");

  PyObject *self = PyObject_New(PyObject, &KosStackType);
  if (! self)
    return NULL;

  KosStackBlock *b = KosStack_BLOCK(original);
  KosStack_BLOCK(self) = b;

  while (b) {
    KosStackBlock_REFCOUNT(b)++;
    b = KosStackBlock_PARENT(b);
  }

  SAYF("KosStack_Copy() -> %p", (void *) self);
  return self;
}


int KosStack_Push(PyObject *self, PyObject *val) {
  if (! val) {
    PyErr_SetString(PyExc_RuntimeError, "stack push failed; NULL value");
    return -1;
  }

  if (! KosStack_BLOCK(self)) {
    PyErr_SetString(PyExc_RuntimeError, "stack push failed; no top block");
    return -1;
  }

  return KosStackBlock_Push(&KosStack_BLOCK(self), val);
}


int KosStack_PUSH(PyObject *self, PyObject *val) {
  if (! val) {
    PyErr_SetString(PyExc_RuntimeError, "stack push failed; NULL value");
    return -1;
  }

  if (! KosStack_BLOCK(self)) {
    PyErr_SetString(PyExc_RuntimeError, "stack push failed; no top block");
    Py_DECREF(val);
    return -1;
  }

  return KosStackBlock_PUSH(&KosStack_BLOCK(self), val);
}


int KosStack_Extend(PyObject *self, PyObject *seq, int reverse) {
  if (! seq) {
    PyErr_SetString(PyExc_RuntimeError, "stack extend failed; NULL sequence");
    return -1;
  }

  if (! KosStack_BLOCK(self)) {
    PyErr_SetString(PyExc_RuntimeError, "stack extend failed; no top block");
    return -1;
  }

  seq = PySequence_Fast(seq, "stack extend requires a sequence");
  if (! seq)
    return -1;

  Py_ssize_t count = PySequence_Fast_GET_SIZE(seq);
  if (count < 0) {
    Py_DECREF(seq);
    return -1;
  }

  PyObject **elms = PySequence_Fast_ITEMS(seq);

  int ret = KosStackBlock_ExtendPtr(&KosStack_BLOCK(self), elms,
                                    (size_t) count, reverse);
  Py_DECREF(seq);
  return ret;
}


int KosStack_ExtendPtr(
    PyObject *self,
    PyObject *const *elms,
    size_t count,
    int reverse) {

  if (! count)
    return 0;

  if (! elms) {
    PyErr_SetString(PyExc_RuntimeError, "stack extend failed, NULL elements");
    return -1;
  }

  if (! KosStack_BLOCK(self)) {
    PyErr_SetString(PyExc_RuntimeError, "stack extend failed; no top block");
    return -1;
  }

  return KosStackBlock_ExtendPtr(&KosStack_BLOCK(self), elms,
                                 count, reverse);
}


int KosStack_EXTENDPTR(
    PyObject *self,
    PyObject *const *elms,
    size_t count,
    int reverse) {

  if (! count)
    return 0;

  if (! elms) {
    PyErr_SetString(PyExc_RuntimeError, "stack extend failed, NULL elements");
    return -1;
  }

  if (! KosStack_BLOCK(self)) {
    PyErr_SetString(PyExc_RuntimeError, "stack extend failed; no top block");
    return -1;
  }

  return KosStackBlock_EXTENDPTR(&KosStack_BLOCK(self), elms,
                                 count, reverse);
}


PyObject *KosStack_Peek(PyObject *self) {
  KosStackBlock *block = KosStack_BLOCK(self);

  if (! block) {
    PyErr_SetString(PyExc_RuntimeError, "stack peek failed; no top block");
    return NULL;
  }

  /* borrowed reference */
  return KosStackBlock_Peek(block);
}


PyObject *KosStack_PeekAt(PyObject *self, Py_ssize_t index) {
  KosStackBlock *block = KosStack_BLOCK(self);

  if (! block) {
    PyErr_SetString(PyExc_RuntimeError, "stack peek failed; no top block");
    return NULL;
  }

  /* borrowed reference */
  return KosStackBlock_PeekAt(block, index);
}


int KosStack_Pull(PyObject *self, size_t count) {

  if (! KosStack_BLOCK(self)) {
    PyErr_SetString(PyExc_RuntimeError, "stack pull failed; no top block");
    return -1;
  }

  return KosStackBlock_Pull(&KosStack_BLOCK(self), count);
}


PyObject *KosStack_Pop(PyObject *self) {
  if (! KosStack_BLOCK(self)) {
    PyErr_SetString(PyExc_RuntimeError, "stack pop failed; no top block");
    return NULL;
  }

  return KosStackBlock_Pop(&KosStack_BLOCK(self));
}


PyObject *KosStack_PopTail(PyObject *self, size_t count) {
  if (! KosStack_BLOCK(self)) {
    PyErr_SetString(PyExc_RuntimeError, "stack pop tail failed;"
                    " no top block");
    return NULL;
  }

  return KosStackBlock_PopTail(&KosStack_BLOCK(self), count);
}


PyObject *const *KosStack_TailPtr(PyObject *self, size_t count) {
  KosStackBlock *block = KosStack_BLOCK(self);

  if (! block) {
    PyErr_SetString(PyExc_RuntimeError, "stack tail ptr failed;"
                    " no top block");
    return NULL;
  }

  return KosStackBlock_TailPtr(block, count);
}


int KosStack_PopTailPtr(PyObject *self, size_t count, PyObject **dest) {
  if (! KosStack_BLOCK(self)) {
    PyErr_SetString(PyExc_RuntimeError, "stack pop tail ptr failed;"
                    " no top block");
    return -1;
  }

  return KosStackBlock_PopTailPtr(&KosStack_BLOCK(self), count, dest);
}


PyObject *KosStack_PopTailDict(PyObject *self, size_t count) {
  if (! KosStack_BLOCK(self)) {
    PyErr_SetString(PyExc_RuntimeError, "stack pop tail dict failed;"
                    " no top block");
    return NULL;
  }

  return KosStackBlock_PopTailDict(&KosStack_BLOCK(self), count);
}


PyObject *KosStack_PopTailDictNames(PyObject *self, size_t count) {
  if (! KosStack_BLOCK(self)) {
    PyErr_SetString(PyExc_RuntimeError, "stack pop tail dict failed;"
                    " no top block");
    return NULL;
  }

  return KosStackBlock_PopTailDictNames(&KosStack_BLOCK(self), count);
}


int KosStack_Drop(PyObject *self, size_t count) {
  if (! KosStack_BLOCK(self)) {
    PyErr_SetString(PyExc_RuntimeError, "stack drop failed;"
                    " no top block");
    return -1;
  }

  return KosStackBlock_Drop(&KosStack_BLOCK(self), count);
}


int KosStack_DropNoErr(PyObject *self, size_t count) {
  if (! KosStack_BLOCK(self))
    return -1;

  return KosStackBlock_DropNoErr(&KosStack_BLOCK(self), count);
}


int KosStack_Replace(PyObject *self, size_t index, PyObject *value) {
  if (! KosStack_BLOCK(self)) {
    PyErr_SetString(PyExc_RuntimeError, "stack replace failed;"
                    " no top block");
    return -1;
  }

  return KosStackBlock_Replace(&KosStack_BLOCK(self), index, value);
}


int KosStack_Duplicate(PyObject *self, size_t count) {

  if (! KosStack_BLOCK(self)) {
    PyErr_SetString(PyExc_RuntimeError, "stack dup failed; no top block");
    return -1;
  }

  return KosStackBlock_Duplicate(&KosStack_BLOCK(self), count);
}


int KosStack_PushBlock(PyObject *self) {
  KosStackBlock *top = KosStack_BLOCK(self);
  if (! top) {
    PyErr_SetString(PyExc_RuntimeError, "could not push block; no top block");
    return -1;
  }

  KosStackBlock *new = KosStackBlock_New(top);
  if (! new) {
    PyErr_SetString(PyExc_RuntimeError, "could not push block; alloc failed");
    return -1;
  }

  KosStack_BLOCK(self) = new;
  return 0;
}


int KosStack_PopBlock(PyObject *self, size_t count) {
  KosStackBlock *block = KosStack_BLOCK(self);

  if (! block) {
    PyErr_SetString(PyExc_RuntimeError, "pop block failed; no top block");
    return -1;
  }

  KosStackBlock *back = block->back;
  if (! back) {
    PyErr_SetString(PyExc_RuntimeError, "cannot pop root block");
    return -1;
  }

  if (count) {
    size_t len = block->len;
    if (count > len) {
      PyErr_SetString(PyExc_RuntimeError, "pop block underrun");
      return -1;
    }

    if (KosStackBlock_EnsureExclusive(&back))
      return -1;

    size_t nlen = back->len + count;
    while (nlen > back->max) {
      back = KosStackBlock_Grow(back);
      if (! back)
        return -1;
    }

    back->len = nlen;
    while (count--) {
      PyObject *v = block->objs[--len];
      back->objs[--nlen] = v;
      Py_INCREF(v);
    }
  }

  KosStack_BLOCK(self) = back;
  KosStackBlock_Release(block);
  return 0;
}


int KosStack_DropBlock(PyObject *self) {
  /*
    Same as KosStack_PopBlock(self, 0) but with different error
    messages
  */

  KosStackBlock *block = KosStack_BLOCK(self);

  if (! block) {
    PyErr_SetString(PyExc_RuntimeError, "drop block failed; no top block");
    return -1;
  }

  KosStackBlock *back = block->back;
  if (! back) {
    PyErr_SetString(PyExc_RuntimeError, "cannot drop root block");
    return -1;
  }

  KosStack_BLOCK(self) = back;
  KosStackBlock_Release(block);
  return 0;
}


int KosStack_Clear(PyObject *self) {
  KosStackBlock *block = KosStack_BLOCK(self);
  KosStackBlock *back = NULL;

  while (block) {
    back = block->back;

    if (back) {
      KosStackBlock_Release(block);
      block = back;

    } else {
      /* the first block in the stack. We keep this one but empty it.
         Update KosStack_BLOCK first since we may have released
         intermediate blocks above. */
      KosStack_BLOCK(self) = block;
      if (KosStackBlock_ClearBlock(&KosStack_BLOCK(self)))
        return -1;
      break;
    }
  }

  return 0;
}


/* === unit lifecycle === */


int koskari_stack_init(PyObject *mod) {
  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  if (PyType_Ready(&KosStackType))
    return -1;

  if (PyDict_SetItemString(dict, "Stack", (PyObject *) &KosStackType))
    return -1;

  return 0;
}


int koskari_stack_clear(PyObject *mod) {

  /* clear the freelist */
  while (block_freelist) {
    KosStackBlock *tmp = block_freelist;
    block_freelist = KosStackBlock_PARENT(tmp);
    PyMem_Free(tmp);
  }
  block_freecount = 0;

  return 0;
}


// The end.
