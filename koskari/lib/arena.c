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
   koskari/lib/arena.c

   Page-aligned arena allocation for packed KosCode blocks. See
   arena.h for the ownership model: the arena is a non-GC leaf owner
   of one page-aligned chunk, kept alive by strong references from the
   KosCode objects packed into it.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
*/


/* Feature test macros must come before any includes */
#define _POSIX_C_SOURCE 200809L

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "lib.h"


/* === helpers === */


static size_t arena_page_size(void) {

  ssize_t page_size = (ssize_t) sysconf(_SC_PAGE_SIZE);
  if (page_size <= 0) {
    page_size = 4096;
  }
  return (size_t) page_size;
}


/* === public API === */


PyObject *KosArena_New(size_t nbytes) {

  if (! nbytes) {
    PyErr_SetString(PyExc_ValueError,
                    "Arena size must be positive");
    return NULL;
  }

  size_t page_size = arena_page_size();
  size_t total = ((nbytes + page_size - 1) / page_size) * page_size;

  void *base = NULL;
  if (posix_memalign(&base, page_size, total)) {
    PyErr_SetString(PyExc_MemoryError,
                    "Failed to allocate arena storage");
    return NULL;
  }

  KosArena *self = PyObject_New(KosArena, &KosArenaType);
  if (! self) {
    free(base);
    return NULL;
  }

  memset(base, 0, total);

  self->base = base;
  self->size = total;
  self->fill = 0;
  self->sealed = 0;
  self->weakreflist = NULL;

  return (PyObject *) self;
}


int KosArena_Reserve(PyObject *arena, size_t nbytes, void **out) {

  KosArena *self = (KosArena *) arena;
  size_t unit = KOS_ARENA_UNIT;
  size_t start = ((self->fill + unit - 1) / unit) * unit;

  if (self->sealed) {
    PyErr_SetString(PyExc_ValueError,
                    "Cannot reserve from a sealed arena");
    return -1;
  }

  if (start > self->size || nbytes > self->size - start) {
    PyErr_Format(PyExc_ValueError,
                 "Arena capacity exceeded: %zu bytes requested,"
                 " %zu of %zu used",
                 nbytes, self->fill, self->size);
    return -1;
  }

  self->fill = start + nbytes;
  *out = (char *) self->base + start;

  return 0;
}


int KosArena_Seal(PyObject *arena) {

  KosArena *self = (KosArena *) arena;

  if (self->sealed) {
    PyErr_SetString(PyExc_ValueError,
                    "Arena is already sealed");
    return -1;
  }

  if (mprotect(self->base, self->size, PROT_READ)) {
    PyErr_SetFromErrno(PyExc_OSError);
    return -1;
  }

  self->sealed = 1;
  return 0;
}


/* === Python type methods === */


static PyObject *arena_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  Py_ssize_t nbytes = 0;

  static char *keywords[] = {
    "size", NULL,
  };

  if (! PyArg_ParseTupleAndKeywords(args, kwds, "n:Arena", keywords,
                                    &nbytes)) {
    return NULL;
  }

  if (nbytes <= 0) {
    PyErr_SetString(PyExc_ValueError,
                    "Arena size must be positive");
    return NULL;
  }

  return KosArena_New((size_t) nbytes);
}


static void arena_dealloc(PyObject *self) {

  KosArena *arena = (KosArena *) self;

  if (arena->weakreflist) {
    PyObject_ClearWeakRefs(self);
  }

  if (arena->base) {
    if (arena->sealed) {
      mprotect(arena->base, arena->size, PROT_WRITE);
    }
    free(arena->base);
  }

  arena->base = NULL;
  arena->size = 0;
  arena->fill = 0;
  arena->sealed = 0;

  Py_TYPE(self)->tp_free(self);
}


static PyObject *arena_repr(PyObject *self) {

  return PyUnicode_FromFormat("<Arena %zu/%zu bytes%s at %p>",
                              KosArena_FILL(self), KosArena_SIZE(self),
                              KosArena_SEALED(self) ? " sealed" : "",
                              self);
}


static PyObject *arena_m_seal(PyObject *self, PyObject *unused) {

  (void) unused;

  if (KosArena_Seal(self)) {
    return NULL;
  }
  Py_RETURN_NONE;
}


static PyObject *arena_get_size(PyObject *self, void *_unused) {

  (void) _unused;
  return PyLong_FromSize_t(KosArena_SIZE(self));
}


static PyObject *arena_get_used(PyObject *self, void *_unused) {

  (void) _unused;
  return PyLong_FromSize_t(KosArena_FILL(self));
}


static PyObject *arena_get_sealed(PyObject *self, void *_unused) {

  (void) _unused;
  return PyBool_FromLong(KosArena_SEALED(self));
}


static PyMethodDef arena_methods[] = {

  { "seal", (PyCFunction) arena_m_seal, METH_NOARGS,
    "A.seal() => None. Seal the arena read-only; raises ValueError if"
    " already sealed" },

  { NULL, },
};


static PyGetSetDef arena_getset[] = {

  { "size", arena_get_size, NULL,
    "total allocation in bytes (page-rounded)", NULL },

  { "used", arena_get_used, NULL,
    "bytes reserved so far", NULL },

  { "sealed", arena_get_sealed, NULL,
    "True once the arena has been sealed read-only", NULL },

  { NULL, },
};


/* === type definition === */


PyTypeObject KosArenaType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "koskari.lib.Arena",
  sizeof(KosArena),
  0,

  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_doc = ("Arena(size) => page-aligned storage for packed Code"
             " blocks, sealed read-only once filled"),
  .tp_repr = arena_repr,
  .tp_getset = arena_getset,
  .tp_methods = arena_methods,
  .tp_new = arena_new,
  .tp_weaklistoffset = offsetof(KosArena, weakreflist),
  .tp_dealloc = arena_dealloc,
};


/* === unit lifecycle === */


int koskari_arena_init(PyObject *mod) {

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict) {
    return -1;
  }

  if (PyType_Ready(&KosArenaType)) {
    return -1;
  }

  if (PyDict_SetItemString(dict, "Arena", (PyObject *) &KosArenaType)) {
    return -1;
  }

  /* the reservation unit, for a planner sizing an arena from its
     members' footprints */
  {
    PyObject *unit = PyLong_FromLong(KOS_ARENA_UNIT);
    int res;
    if (! unit) {
      return -1;
    }
    res = PyDict_SetItemString(KosArenaType.tp_dict, "UNIT", unit);
    Py_DECREF(unit);
    if (res) {
      return -1;
    }
  }

  return 0;
}


int koskari_arena_clear(PyObject *mod) {

  (void) mod;
  return 0;
}


// The end.
