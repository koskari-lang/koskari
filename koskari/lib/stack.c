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
   Stack and StackBlock types

   The Stack type provides a block-structured value stack for the
   evaluator. Each block can hold values and has associated metadata
   for control flow handling.

   author: Christopher O'Brien <obriencj@gmail.com>
   license: GPL v.3
*/


#include "lib.h"


/* === BlockStyle enum === */


#define int_enum(enum_dict, ek)                     \
  do {                                              \
    PyObject *_v = PyLong_FromLong(ek);             \
    PyDict_SetItemString(enum_dict, #ek, _v);       \
    Py_DECREF(_v);                                  \
  } while(0)


static PyObject *create_block_style_enum(void) {
  PyObject *enum_module = NULL;
  PyObject *enum_dict = NULL;

  enum_module = PyImport_ImportModule("enum");
  if (! enum_module)
    return NULL;

  enum_dict = PyDict_New();
  if (! enum_dict) {
    Py_DECREF(enum_module);
    return NULL;
  }

  int_enum(enum_dict, KOS_BLOCK_BEGIN);
  int_enum(enum_dict, KOS_BLOCK_FINALLY);
  int_enum(enum_dict, KOS_BLOCK_FOREACH);
  int_enum(enum_dict, KOS_BLOCK_LOOP);
  int_enum(enum_dict, KOS_BLOCK_NS);
  int_enum(enum_dict, KOS_BLOCK_TRY);
  int_enum(enum_dict, KOS_BLOCK_WITH);
  int_enum(enum_dict, KOS_BLOCK_AWAIT_FOREACH);
  int_enum(enum_dict, KOS_BLOCK_AWAIT_WITH);

  PyObject *res = PyObject_CallMethod(enum_module, "IntEnum", "sO",
                                      "BlockStyle", enum_dict);

  Py_DECREF(enum_dict);
  Py_DECREF(enum_module);

  return res;
}


/* === StackBlock === */


#define KosStackBlock_NEEDED_SIZE(count)                        \
  (sizeof(KosStackBlock) + ((sizeof(PyObject *)) * count))


#define BLOCK_NEW_SIZE 16
#define BLOCK_MAX_SIZE 256

#define BLOCK_MAX_FREE 256

static size_t block_freecount = 0;
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
    KosStackBlock_MAX(self) = BLOCK_NEW_SIZE;
    SAY(" no free, allocated new");
  }

  KosStackBlock_PARENT(self) = parent;
  KosStackBlock_BLOCK_STYLE(self) = KOS_BLOCK_BEGIN;
  KosStackBlock_ENABLED(self) = 1;
  KosStackBlock_LABEL_A(self) = 0;
  KosStackBlock_LABEL_B(self) = 0;
  KosStackBlock_LEN(self) = 0;

  SAYF("KosStackBlock_New() -> %p", (void *) self);
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


static void KosStackBlock_Clear(KosStackBlock *self) {
  for (size_t i = self->len; i--; ) {
    Py_DECREF(self->objs[i]);
  }
  self->len = 0;
  self->enabled = 1;
}


static void KosStackBlock_Free(KosStackBlock *self) {
  SAYF("KosStackBlock_Free(%p)", (void *) self);

  KosStackBlock_Clear(self);

  if (block_freecount < BLOCK_MAX_FREE) {
    block_freecount += 1;
    KosStackBlock_PARENT(self) = block_freelist;
    block_freelist = self;
    SAYF(" deposited into freelist, new block_freecount: %zu", block_freecount);

  } else {
    PyMem_Free(self);
    SAY(" at max freelist size, dealloced");
  }

  SAY("KosStackBlock_Free -> void");
}


static KosStackBlock *KosStackBlock_Push(KosStackBlock *self,
                                         PyObject *val) {

  /* grow if needed */
  while (self->len >= self->max) {
    self = KosStackBlock_Grow(self);
    if (! self)
      return NULL;
  }

  Py_INCREF(val);
  self->objs[self->len++] = val;

  return self;
}


static KosStackBlock *KosStackBlock_PUSH(KosStackBlock *self,
                                         PyObject *val) {

  /* grow if needed */
  while (self->len >= self->max) {
    self = KosStackBlock_Grow(self);
    if (! self)
      return NULL;
  }

  /* stolen reference */
  self->objs[self->len++] = val;

  return self;
}


static KosStackBlock *KosStackBlock_ExtendPtr(KosStackBlock *self,
                                              PyObject *const *elms,
                                              size_t count,
                                              int reverse) {
  if (! count)
    return self;

  size_t needed = self->len + count;
  while (needed >= self->max) {
    self = KosStackBlock_Grow(self);
    if (! self)
      return NULL;
  }

  size_t offset = self->len;
  PyObject *val = NULL;

  if (reverse) {
    while (count--) {
      val = elms[count];
      Py_XINCREF(val);
      self->objs[offset++] = val;
    }
  } else {
    for (size_t i = 0; i < count; i++) {
      val = elms[i];
      Py_XINCREF(val);
      self->objs[offset++] = val;
    }
  }
  self->len = offset;

  return self;
}


static KosStackBlock *KosStackBlock_EXTENDPTR(KosStackBlock *self,
                                              PyObject *const *elms,
                                              size_t count,
                                              int reverse) {
  if (! count)
    return self;

  size_t needed = self->len + count;
  while (needed >= self->max) {
    self = KosStackBlock_Grow(self);
    if (! self) {
      while (count--) {
        Py_XDECREF(elms[count]);
      }
      return NULL;
    }
  }

  size_t offset = self->len;

  if (reverse) {
    while (count--) {
      self->objs[offset++] = elms[count];
    }
  } else {
    for (size_t i = 0; i < count; i++) {
      self->objs[offset++] = elms[i];
    }
  }
  self->len = offset;

  return self;
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


static PyObject *KosStackBlock_PeekAt(KosStackBlock *self, Py_ssize_t index) {

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


static KosStackBlock *KosStackBlock_Pull(KosStackBlock *self, size_t count) {
  if (! count)
    return self;

  KosStackBlock *parent = KosStackBlock_PARENT(self);
  if (! parent) {
    PyErr_SetString(PyExc_RuntimeError, "stack pull failed; no parent block");
    return NULL;
  }

  if (parent->len < count) {
    PyErr_SetString(PyExc_RuntimeError, "stack pull failed; parent underrun");
    return NULL;
  }

  while (self->len + count >= self->max) {
    self = KosStackBlock_Grow(self);
    if (! self)
      return NULL;
  }

  for (size_t i = parent->len - count; count--; i++) {
    PyObject *val = parent->objs[i];
    Py_INCREF(val);
    self->objs[self->len++] = val;
  }

  return self;
}


static PyObject *KosStackBlock_Pop(KosStackBlock *self) {

  size_t len = self->len;
  if (! len) {
    PyErr_SetString(PyExc_RuntimeError, "stack pop underrun");
    return NULL;
  }

  self->len = --len;

  /* stolen reference */
  return self->objs[len];
}


static int KosStackBlock_Drop(KosStackBlock *self, size_t count) {
  if (! count)
    return 0;

  size_t len = self->len;
  if (count > len) {
    PyErr_SetString(PyExc_RuntimeError, "stack drop underrun");
    return -1;
  }

  while (count--) {
    Py_DECREF(self->objs[--len]);
  }

  self->len = len;
  return 0;
}


static int KosStackBlock_DropNoErr(KosStackBlock *self, size_t count) {
  if (! count)
    return 0;

  int res = 0;

  size_t len = self->len;
  if (count > len) {
    count = len;
    res = -1;
  }

  while (count--) {
    Py_DECREF(self->objs[--len]);
  }

  self->len = len;
  return res;
}


static KosStackBlock *KosStackBlock_Duplicate(KosStackBlock *self,
                                              size_t count) {

  if (! count) {
    return self;
  }

  if (count > self->len) {
    PyErr_SetString(PyExc_RuntimeError, "stack dup underrun");
    return NULL;
  }

  /* grow if needed */
  while (self->len + count >= self->max) {
    self = KosStackBlock_Grow(self);
    if (! self)
      return NULL;
  }

  for (size_t i = count; i--; ) {
    PyObject *val = self->objs[self->len - count];
    Py_INCREF(val);
    self->objs[self->len++] = val;
  }

  return self;
}


static int KosStackBlock_Replace(KosStackBlock *self,
                                 size_t index, PyObject *value) {

  if (! value) {
    PyErr_SetString(PyExc_RuntimeError, "stack replace cannot assign NULL");
    return -1;
  }

  if (index >= self->len) {
    PyErr_SetString(PyExc_RuntimeError, "stack replace underrun");
    return -1;
  }

  Py_INCREF(value);
  Py_DECREF(self->objs[index]);
  self->objs[index] = value;

  return 0;
}


static PyObject *KosStackBlock_PopTail(KosStackBlock *self, size_t count) {

  if (! count) {
    return PyTuple_New(0);
  }

  size_t len = self->len;

  if (count > len) {
    PyErr_Format(PyExc_RuntimeError, "stack pop tail underrun."
                 " requested: %zu, available: %zu", count, len);
    return NULL;
  }

  PyObject *ret = PyTuple_New(count);
  if (! ret)
    return NULL;

  while (count--) {
    PyTuple_SET_ITEM(ret, count, self->objs[--len]);
  }

  self->len = len;
  return ret;
}


static PyObject *const *KosStackBlock_TailPtr(KosStackBlock *self,
                                              size_t count) {
  if (! count) {
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


static int KosStackBlock_PopTailPtr(KosStackBlock *self,
                                    size_t count, PyObject **dest) {

  if (! count) {
    return 0;
  }

  size_t len = self->len;
  if (count > len) {
    PyErr_Format(PyExc_RuntimeError, "stack pop tail ptr underrun."
                 " requested: %zu, available: %zu", count, len);
    return -1;
  }

  while (count--) {
    dest[count] = self->objs[--len];
  }

  self->len = len;
  return 0;
}


static PyObject *KosStackBlock_PopTailDict(KosStackBlock *self, size_t count) {

  if (! count) {
    return PyDict_New();
  }

  size_t tcount = count * 2;

  if (tcount > self->len) {
    PyErr_SetString(PyExc_RuntimeError, "stack pop tail dict underrun");
    return NULL;
  }

  PyObject *ret = PyDict_NewPresized(count);
  if (! ret)
    return NULL;

  self->len -= tcount;

  size_t i = self->len;
  while (count--) {
    PyObject *key = self->objs[i++];
    PyObject *val = self->objs[i++];

    PyDict_SetItem(ret, key, val);

    Py_DECREF(key);
    Py_DECREF(val);
  }

  return ret;
}


static PyObject *KosStackBlock_PopTailDictNames(KosStackBlock *self,
                                                size_t count) {

  PyObject *ret = NULL;

  PyObject *names = KosStackBlock_Pop(self);
  if (! names) {
    return NULL;
  }

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

  if (count > self->len) {
    PyErr_SetString(PyExc_RuntimeError, "stack pop tail dict underrun");
    goto failure;
  }

  ret = PyDict_NewPresized(count);
  if (! ret)
    goto failure;

  /* set the stack height down ahead of time. We'll work our way back
     up in order from there, because we want to insert in the same
     order things arrived on the stack */
  self->len -= count;

  size_t i = self->len, j = 0;
  while (count--) {
    PyObject *key = PyTuple_GET_ITEM(names, j++);
    PyObject *val = self->objs[i++];

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


static PyObject *stack_m_push_block(PyObject *self, PyObject *args) {
  KosBlockStyle style = KOS_BLOCK_BEGIN;
  Py_ssize_t style_v = (Py_ssize_t) style, label_a = 0, label_b = 0;

  if (! PyArg_ParseTuple(args, "|nnn:Stack.push_block",
                         &style_v, &label_a, &label_b))
    return NULL;

  style = (KosBlockStyle) style_v;

  if (KosStack_PushBlockFull(self, style, label_a, label_b)) {
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


static PyObject *stack_m_block_disable(PyObject *self) {
  KosStackBlock *block = KosStack_BLOCK(self);
  KosStackBlock_ENABLED(block) = 0;
  Py_RETURN_NONE;
}


static PyObject *stack_m_block_status(PyObject *self) {
  KosStackBlock *bl = KosStack_BLOCK(self);
  PyObject *res = PyTuple_New(4);

  PyTuple_SET_ITEM(res, 0, PyLong_FromLong(KosStackBlock_BLOCK_STYLE(bl)));
  PyTuple_SET_ITEM(res, 1, PyBool_FromLong(KosStackBlock_ENABLED(bl)));
  PyTuple_SET_ITEM(res, 2, PyLong_FromSsize_t(KosStackBlock_LABEL_A(bl)));
  PyTuple_SET_ITEM(res, 3, PyLong_FromSsize_t(KosStackBlock_LABEL_B(bl)));

  return res;
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

  { "push_block", (PyCFunction) stack_m_push_block, METH_VARARGS,
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

  { "disable", (PyCFunction) stack_m_block_disable, METH_NOARGS,
    "Disable the handler in the current block\n"
    "S.disable() => None" },

  { "status", (PyCFunction) stack_m_block_status, METH_NOARGS,
    "The handler status of the current block\n"
    "S.status() => (block_style, enabled, label_a, label_b)" },

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

  { NULL, NULL, 0, NULL },
};


/* === Stack type === */


static PyObject *stack_new(PyTypeObject *type,
                           PyObject *args, PyObject *kwds) {

  if (PyTuple_GET_SIZE(args) || (kwds && PyDict_Size(kwds))) {
    PyErr_SetString(PyExc_TypeError, "Stack takes no arguments");
    return NULL;
  }

  return KosStack_New();
}


static void stack_dealloc(PyObject *self) {
  KosStack_Clear(self);

  KosStackBlock *block = KosStack_BLOCK(self);
  if (block)
    KosStackBlock_Free(block);

  Py_TYPE(self)->tp_free(self);
}


static int stack_traverse(PyObject *self, visitproc visit, void *arg) {
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
  KosStack_Clear(self);

  KosStackBlock *block = KosStack_BLOCK(self);
  if (block)
    KosStackBlock_Free(block);

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
  PyObject *self = PyObject_New(PyObject, &KosStackType);
  if (! self)
    return NULL;

  KosStack_BLOCK(self) = KosStackBlock_New(NULL);
  return self;
}


int KosStack_Push(PyObject *self, PyObject *val) {
  KosStackBlock *block = KosStack_BLOCK(self);
  KosStackBlock *nb = NULL;

  if (! val) {
    PyErr_SetString(PyExc_RuntimeError, "stack push failed; NULL value");
    return -1;
  }

  if (! block) {
    PyErr_SetString(PyExc_RuntimeError, "stack push failed; no top block");
    return -1;
  }

  nb = KosStackBlock_Push(block, val);
  if (! nb) {
    return -1;

  } else if (nb != block) {
    KosStack_BLOCK(self) = nb;
  }

  return 0;
}


int KosStack_PUSH(PyObject *self, PyObject *val) {
  KosStackBlock *block = KosStack_BLOCK(self);
  KosStackBlock *nb = NULL;

  if (! val) {
    PyErr_SetString(PyExc_RuntimeError, "stack push failed; NULL value");
    return -1;
  }

  if (! block) {
    PyErr_SetString(PyExc_RuntimeError, "stack push failed; no top block");
    Py_DECREF(val);
    return -1;
  }

  nb = KosStackBlock_PUSH(block, val);
  if (! nb) {
    Py_DECREF(val);
    return -1;

  } else if (nb != block) {
    KosStack_BLOCK(self) = nb;
  }

  return 0;
}


int KosStack_Extend(PyObject *self, PyObject *seq, int reverse) {
  KosStackBlock *block = KosStack_BLOCK(self);
  KosStackBlock *nb = NULL;

  if (! seq) {
    PyErr_SetString(PyExc_RuntimeError, "stack extend failed; NULL sequence");
    return -1;
  }

  if (! block) {
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

  nb = KosStackBlock_ExtendPtr(block, elms, (size_t) count, reverse);
  Py_DECREF(seq);

  if (! nb) {
    return -1;

  } else if (nb != block) {
    KosStack_BLOCK(self) = nb;
  }

  return 0;
}


int KosStack_ExtendPtr(PyObject *self,
                       PyObject *const *elms, size_t count,
                       int reverse) {

  KosStackBlock *block = KosStack_BLOCK(self);
  KosStackBlock *nb = NULL;

  if (! count)
    return 0;

  if (! elms) {
    PyErr_SetString(PyExc_RuntimeError, "stack extend failed, NULL elements");
    return -1;
  }

  if (! block) {
    PyErr_SetString(PyExc_RuntimeError, "stack extend failed; no top block");
    return -1;
  }

  nb = KosStackBlock_ExtendPtr(block, elms, count, reverse);
  if (! nb) {
    return -1;

  } else if (nb != block) {
    KosStack_BLOCK(self) = nb;
  }

  return 0;
}


int KosStack_EXTENDPTR(PyObject *self,
                       PyObject *const *elms, size_t count,
                       int reverse) {

  KosStackBlock *block = KosStack_BLOCK(self);
  KosStackBlock *nb = NULL;

  if (! count)
    return 0;

  if (! elms) {
    PyErr_SetString(PyExc_RuntimeError, "stack extend failed, NULL elements");
    return -1;
  }

  if (! block) {
    PyErr_SetString(PyExc_RuntimeError, "stack extend failed; no top block");
    return -1;
  }

  nb = KosStackBlock_EXTENDPTR(block, elms, count, reverse);
  if (! nb) {
    return -1;

  } else if (nb != block) {
    KosStack_BLOCK(self) = nb;
  }

  return 0;
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

  KosStackBlock *block = KosStack_BLOCK(self);
  KosStackBlock *nb = NULL;

  if (! block) {
    PyErr_SetString(PyExc_RuntimeError, "stack pull failed; no top block");
    return -1;
  }

  nb = KosStackBlock_Pull(block, count);
  if (! nb) {
    return -1;

  } else if (nb != block) {
    KosStack_BLOCK(self) = nb;
  }

  return 0;
}


PyObject *KosStack_Pop(PyObject *self) {
  KosStackBlock *block = KosStack_BLOCK(self);

  if (! block) {
    PyErr_SetString(PyExc_RuntimeError, "stack pop failed; no top block");
    return NULL;
  }

  return KosStackBlock_Pop(block);
}


PyObject *KosStack_PopTail(PyObject *self, size_t count) {
  KosStackBlock *block = KosStack_BLOCK(self);

  if (! block) {
    PyErr_SetString(PyExc_RuntimeError, "stack pop tail failed;"
                    " no top block");
    return NULL;
  }

  return KosStackBlock_PopTail(block, count);
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
  KosStackBlock *block = KosStack_BLOCK(self);

  if (! block) {
    PyErr_SetString(PyExc_RuntimeError, "stack pop tail ptr failed;"
                    " no top block");
    return -1;
  }

  return KosStackBlock_PopTailPtr(block, count, dest);
}


PyObject *KosStack_PopTailDict(PyObject *self, size_t count) {
  KosStackBlock *block = KosStack_BLOCK(self);

  if (! block) {
    PyErr_SetString(PyExc_RuntimeError, "stack pop tail dict failed;"
                    " no top block");
    return NULL;
  }

  return KosStackBlock_PopTailDict(block, count);
}


PyObject *KosStack_PopTailDictNames(PyObject *self, size_t count) {
  KosStackBlock *block = KosStack_BLOCK(self);

  if (! block) {
    PyErr_SetString(PyExc_RuntimeError, "stack pop tail dict failed;"
                    " no top block");
    return NULL;
  }

  return KosStackBlock_PopTailDictNames(block, count);
}


int KosStack_Drop(PyObject *self, size_t count) {
  KosStackBlock *block = KosStack_BLOCK(self);

  if (! block) {
    PyErr_SetString(PyExc_RuntimeError, "stack drop failed;"
                    " no top block");
    return -1;
  }

  return KosStackBlock_Drop(block, count);
}


int KosStack_DropNoErr(PyObject *self, size_t count) {
  KosStackBlock *block = KosStack_BLOCK(self);

  if (! block) {
    return -1;
  }

  return KosStackBlock_DropNoErr(block, count);
}


int KosStack_Replace(PyObject *self, size_t index, PyObject *value) {
  KosStackBlock *block = KosStack_BLOCK(self);

  if (! block) {
    PyErr_SetString(PyExc_RuntimeError, "stack replace failed;"
                    " no top block");
    return -1;
  }

  return KosStackBlock_Replace(block, index, value);
}


int KosStack_Duplicate(PyObject *self, size_t count) {

  KosStackBlock *block = KosStack_BLOCK(self);
  KosStackBlock *nb = NULL;

  if (! block) {
    PyErr_SetString(PyExc_RuntimeError, "stack dup failed; no top block");
    return -1;
  }

  nb = KosStackBlock_Duplicate(block, count);
  if (! nb) {
    return -1;

  } else if (nb != block) {
    KosStack_BLOCK(self) = nb;
  }

  return 0;
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


int KosStack_PushBlockFull(PyObject *self, KosBlockStyle style,
                           Py_ssize_t label_a, Py_ssize_t label_b) {

  int res = KosStack_PushBlock(self);
  if (res)
    return res;

  KosStackBlock *bl = KosStack_BLOCK(self);
  KosStackBlock_BLOCK_STYLE(bl) = style;
  KosStackBlock_LABEL_A(bl) = label_a;
  KosStackBlock_LABEL_B(bl) = label_b;

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

    size_t nlen = back->len + count;
    while (nlen > back->max) {
      back = KosStackBlock_Grow(back);
      if (! back)
        return -1;
    }

    back->len = nlen;
    while (count--) {
      back->objs[--nlen] = block->objs[--len];
    }
    block->len = len;
  }

  KosStack_BLOCK(self) = back;
  KosStackBlock_Free(block);
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
  KosStackBlock_Free(block);
  return 0;
}


int KosStack_Clear(PyObject *self) {
  KosStackBlock *block = KosStack_BLOCK(self);
  KosStackBlock *back = NULL;

  while (block) {
    back = block->back;

    if (back) {
      KosStackBlock_Free(block);
      block = back;

    } else {
      /* the first block in the stack. We keep this one but empty it */
      KosStackBlock_Clear(block);
      KosStack_BLOCK(self) = block;
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

  PyObject *block_style_enum = create_block_style_enum();
  if (! block_style_enum)
    return -1;

  if (PyDict_SetItemString(dict, "Stack", (PyObject *) &KosStackType))
    return -1;

  if (PyDict_SetItemString(dict, "BlockStyle", block_style_enum)) {
    Py_DECREF(block_style_enum);
    return -1;
  }
  Py_DECREF(block_style_enum);

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
