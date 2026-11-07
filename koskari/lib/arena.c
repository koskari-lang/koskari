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

   Arena allocation for KosCode blocks. See arena.h for the ownership
   model: the arena is a non-GC leaf owner of one anonymous mapping,
   kept alive by strong references from the KosCode objects placed in
   it, and a code allocator is the set of chunks one module load fills.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
*/


/* Python.h sets the feature test macros, so it comes before any
   system header: anonymous mappings need the GNU or Darwin extensions
   beyond plain POSIX, and pyconfig.h chooses them per platform */
#include "lib.h"
#include "hints.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>


#if ! defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#define MAP_ANONYMOUS MAP_ANON
#endif


/* A mapping is addressable to ASan as a whole, so the arena marks its
   own shadow: a chunk is poisoned when mapped, a reservation unpoisons
   exactly its slice, and a released block is poisoned again. A read or
   write past a block, into the unit padding or the unreserved tail,
   then reports where it happens. Not every runtime clears the shadow
   on munmap, so a region is unpoisoned before it is unmapped. */
#ifdef KOS_ASAN
#include <sanitizer/asan_interface.h>
#else
#define ASAN_POISON_MEMORY_REGION(addr, size)    ((void) (addr), (void) (size))
#define ASAN_UNPOISON_MEMORY_REGION(addr, size)  ((void) (addr), (void) (size))
#endif


/* === helpers === */


static size_t round_up(size_t value, size_t unit) {

  return ((value + unit - 1) / unit) * unit;
}


/* The aligned start a reservation of nbytes would take, or SIZE_MAX
   when the arena lacks the room or is sealed. */
static size_t arena_fit(KosArena *self, size_t nbytes) {

  size_t start = round_up(self->fill, KOS_ARENA_UNIT);

  if (self->sealed || start > self->size || nbytes > self->size - start)
    return SIZE_MAX;
  return start;
}


/* Commit a reservation of nbytes at a start arena_fit returned, or 0
   on an empty arena that holds nbytes. */
static void arena_take(
    KosArena *self,
    size_t start,
    size_t nbytes,
    void **out) {

  self->fill = start + nbytes;
  self->writing++;
  *out = (char *) self->base + start;
  ASAN_UNPOISON_MEMORY_REGION(*out, nbytes);
}


/* === public API === */


size_t KosArena_PageSize(void) {

#if KOS_NO_PROTECT
  return 0;
#else
  static size_t page = 0;

  /* a guessed page size would be handed to mmap and mprotect, so none
     is guessed: without one, no block is placed or protected */
  if (! page) {
    long page_size = sysconf(_SC_PAGE_SIZE);
    if (page_size > 0) {
      page = (size_t) page_size;
    }
  }
  return page;
#endif
}


size_t KosArena_ChunkUnit(void) {

  return KOS_ARENA_CHUNK_PAGES * KosArena_PageSize();
}


PyObject *KosArena_New(size_t nbytes) {

  size_t unit = KosArena_ChunkUnit();

  if (! unit) {
    PyErr_SetString(PyExc_OSError,
                    "Cannot map an arena: the system reports no page"
                    " size");
    return NULL;
  }

  if (! nbytes) {
    PyErr_SetString(PyExc_ValueError,
                    "Arena size must be positive");
    return NULL;
  }

  if (nbytes > SIZE_MAX - unit) {
    PyErr_SetString(PyExc_MemoryError,
                    "Arena size too large");
    return NULL;
  }

  size_t total = round_up(nbytes, unit);

  /* fresh anonymous pages read as zero; none is touched until a
     reservation is written */
  void *base = mmap(NULL, total, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (base == MAP_FAILED) {
    PyErr_SetString(PyExc_MemoryError,
                    "Failed to allocate arena storage");
    return NULL;
  }
  ASAN_POISON_MEMORY_REGION(base, total);

  KosArena *self = PyObject_New(KosArena, &KosArenaType);
  if (! self) {
    ASAN_UNPOISON_MEMORY_REGION(base, total);
    munmap(base, total);
    return NULL;
  }

  self->base = base;
  self->size = total;
  self->fill = 0;
  self->writing = 0;
  self->sealed = 0;
  self->weakreflist = NULL;

  return (PyObject *) self;
}


int KosArena_Reserve(PyObject *arena, size_t nbytes, void **out) {

  KosArena *self = (KosArena *) arena;

  size_t start = arena_fit(self, nbytes);
  if (start == SIZE_MAX) {
    if (self->sealed) {
      PyErr_SetString(PyExc_ValueError,
                      "Cannot reserve from a sealed arena");
    } else {
      PyErr_Format(PyExc_ValueError,
                   "Arena capacity exceeded: %zu bytes requested,"
                   " %zu of %zu used",
                   nbytes, self->fill, self->size);
    }
    return -1;
  }

  arena_take(self, start, nbytes, out);
  return 0;
}


void KosArena_Complete(PyObject *arena) {

  KosArena *self = (KosArena *) arena;

  self->writing--;
}


int KosArena_Seal(PyObject *arena) {

  KosArena *self = (KosArena *) arena;

  if (self->sealed) {
    PyErr_SetString(PyExc_ValueError,
                    "Arena is already sealed");
    return -1;
  }

  /* a reservation is written by code that can run Python meanwhile:
     sealing under it would make its next write fault */
  if (self->writing) {
    PyErr_SetString(PyExc_ValueError,
                    "Cannot seal an arena with a reservation"
                    " still being written");
    return -1;
  }

  /* trim the chunk to the whole pages its reservations fill: the tail
     was never written, so it was never resident, and unmapping it
     gives back the address space. A chunk whose reservations all died
     while it was open goes whole. The page size is known: a chunk is
     mapped only when it is. */
  size_t page = KosArena_PageSize();
  size_t keep = round_up(self->fill, page);
  if (keep < self->size) {
    ASAN_UNPOISON_MEMORY_REGION((char *) self->base + keep,
                                self->size - keep);
    if (munmap((char *) self->base + keep, self->size - keep)) {
      PyErr_SetFromErrno(PyExc_OSError);
      return -1;
    }
    self->size = keep;
    if (! keep) {
      self->base = NULL;
    }
  }

  if (self->size && mprotect(self->base, self->size, PROT_READ)) {
    PyErr_SetFromErrno(PyExc_OSError);
    return -1;
  }

  self->sealed = 1;
  return 0;
}


void KosArena_Release(PyObject *arena, void *base, size_t nbytes) {

  KosArena *self = (KosArena *) arena;
  char *start = (char *) self->base;

  if (self->sealed)
    return;

  /* the newest reservation ends where the fill does, on the unit
     boundary: an earlier release leaves the fill at its block's aligned
     start, above the exact end of the block before it */
  size_t end = (size_t) ((char *) base - start) + nbytes;
  if (round_up(end, KOS_ARENA_UNIT) == round_up(self->fill, KOS_ARENA_UNIT)) {
    self->fill = (size_t) ((char *) base - start);
  }

  /* dead whether or not its space was returned */
  ASAN_POISON_MEMORY_REGION(base, nbytes);
}


PyObject *KosCodeAllocator_New(void) {

  KosCodeAllocator *self = PyObject_New(KosCodeAllocator,
                                        &KosCodeAllocatorType);
  if (! self) {
    return NULL;
  }

  self->chunks = PyList_New(0);
  if (! self->chunks) {
    Py_DECREF(self);
    return NULL;
  }

  return (PyObject *) self;
}


int KosCodeAllocator_Reserve(
    PyObject *alloc,
    size_t nbytes,
    void **out,
    PyObject **out_chunk) {

  PyObject *chunks = KosCodeAllocator_CHUNKS(alloc);

  if (! chunks) {
    PyErr_SetString(PyExc_ValueError,
                    "Cannot reserve from a sealed code allocator");
    return -1;
  }

  /* first fit, in creation order */
  Py_ssize_t count = PyList_GET_SIZE(chunks);
  for (Py_ssize_t i = 0; i < count; i++) {
    PyObject *chunk = PyList_GET_ITEM(chunks, i);
    size_t start = arena_fit((KosArena *) chunk, nbytes);
    if (start != SIZE_MAX) {
      arena_take((KosArena *) chunk, start, nbytes, out);
      *out_chunk = chunk;
      return 0;
    }
  }

  /* nothing has room: a new chunk, sized up to the next whole unit,
     owned by the list */
  PyObject *chunk = KosArena_New(nbytes);
  if (! chunk) {
    return -1;
  }

  int res = PyList_Append(chunks, chunk);
  Py_DECREF(chunk);
  if (res) {
    return -1;
  }

  arena_take((KosArena *) chunk, 0, nbytes, out);
  *out_chunk = chunk;
  return 0;
}


PyObject *KosCodeAllocator_Seal(PyObject *alloc) {

  PyObject *chunks = KosCodeAllocator_CHUNKS(alloc);

  if (! chunks) {
    PyErr_SetString(PyExc_ValueError,
                    "Code allocator is already sealed");
    return NULL;
  }

  /* a chunk a failed seal left sealed is passed over, so a retry seals
     the remainder and closes */
  Py_ssize_t count = PyList_GET_SIZE(chunks);
  for (Py_ssize_t i = 0; i < count; i++) {
    PyObject *chunk = PyList_GET_ITEM(chunks, i);
    if (! KosArena_SEALED(chunk) && KosArena_Seal(chunk)) {
      return NULL;
    }
  }

  PyObject *result = PyList_AsTuple(chunks);
  if (! result) {
    return NULL;
  }

  Py_CLEAR(KosCodeAllocator_CHUNKS(alloc));
  return result;
}


/* === Arena type methods === */


static void arena_dealloc(PyObject *self) {

  KosArena *arena = (KosArena *) self;

  if (arena->weakreflist) {
    PyObject_ClearWeakRefs(self);
  }

  /* unmapping needs no write access, so a sealed chunk goes as it is.
     When it fails the mapping stays: a leak is the safe outcome, and
     the one channel a deallocator has reports it. */
  if (arena->base) {
    ASAN_UNPOISON_MEMORY_REGION(arena->base, arena->size);
    if (munmap(arena->base, arena->size)) {
      PyErr_SetFromErrno(PyExc_OSError);
      PyErr_WriteUnraisable(self);
    }
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


static PyGetSetDef arena_getset[] = {

  { "size", arena_get_size, NULL,
    "mapped bytes: whole chunk units while open, the whole pages the"
    " code fills once sealed", NULL },

  { "used", arena_get_used, NULL,
    "end of the newest reservation in bytes", NULL },

  { "sealed", arena_get_sealed, NULL,
    "True once the arena has been sealed read-only", NULL },

  { NULL, },
};


/* === CodeAllocator type methods === */


static PyObject *code_allocator_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  static char *keywords[] = {
    NULL,
  };

  if (! PyArg_ParseTupleAndKeywords(args, kwds, ":CodeAllocator",
                                    keywords)) {
    return NULL;
  }

  return KosCodeAllocator_New();
}


static void code_allocator_dealloc(PyObject *self) {

  /* chunks still open stay alive, writable, through their members */
  Py_CLEAR(KosCodeAllocator_CHUNKS(self));
  Py_TYPE(self)->tp_free(self);
}


static PyObject *code_allocator_repr(PyObject *self) {

  PyObject *chunks = KosCodeAllocator_CHUNKS(self);

  if (! chunks) {
    return PyUnicode_FromFormat("<CodeAllocator sealed at %p>", self);
  }
  return PyUnicode_FromFormat("<CodeAllocator %zd chunks at %p>",
                              PyList_GET_SIZE(chunks), self);
}


static PyObject *code_allocator_m_seal(PyObject *self, PyObject *unused) {

  (void) unused;
  return KosCodeAllocator_Seal(self);
}


static PyObject *code_allocator_get_chunks(PyObject *self, void *_unused) {

  PyObject *chunks = KosCodeAllocator_CHUNKS(self);

  (void) _unused;

  if (! chunks) {
    return PyTuple_New(0);
  }
  return PyList_AsTuple(chunks);
}


static PyObject *code_allocator_get_sealed(PyObject *self, void *_unused) {

  (void) _unused;
  return PyBool_FromLong(! KosCodeAllocator_CHUNKS(self));
}


static PyMethodDef code_allocator_methods[] = {

  { "seal", (PyCFunction) code_allocator_m_seal, METH_NOARGS,
    "C.seal() => tuple of Arena. Seal every chunk read-only and close"
    " the allocator; raises ValueError if already sealed" },

  { NULL, },
};


static PyGetSetDef code_allocator_getset[] = {

  { "chunks", code_allocator_get_chunks, NULL,
    "tuple of the open chunks, in creation order; empty once sealed",
    NULL },

  { "sealed", code_allocator_get_sealed, NULL,
    "True once the allocator has been sealed", NULL },

  { NULL, },
};


/* === type definitions === */


PyTypeObject KosArenaType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "koskari.lib.Arena",
  sizeof(KosArena),
  0,

  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_doc = ("one chunk of a CodeAllocator's storage for Code blocks,"
             " sealed read-only once filled"),
  .tp_repr = arena_repr,
  .tp_getset = arena_getset,
  .tp_weaklistoffset = offsetof(KosArena, weakreflist),
  .tp_dealloc = arena_dealloc,
};


PyTypeObject KosCodeAllocatorType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "koskari.lib.CodeAllocator",
  sizeof(KosCodeAllocator),
  0,

  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_doc = ("CodeAllocator() => the arena chunks one module load"
             " places its function Code blocks into"),
  .tp_repr = code_allocator_repr,
  .tp_getset = code_allocator_getset,
  .tp_methods = code_allocator_methods,
  .tp_new = code_allocator_new,
  .tp_dealloc = code_allocator_dealloc,
};


/* === unit lifecycle === */


static int set_type_constant(
    PyTypeObject *type,
    const char *name,
    size_t value) {

  PyObject *num = PyLong_FromSize_t(value);
  int res;

  if (! num) {
    return -1;
  }
  res = PyDict_SetItemString(type->tp_dict, name, num);
  Py_DECREF(num);
  return res;
}


int koskari_arena_init(PyObject *mod) {

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict) {
    return -1;
  }

  if (PyType_Ready(&KosArenaType) ||
      PyType_Ready(&KosCodeAllocatorType)) {
    return -1;
  }

  if (PyDict_SetItemString(dict, "Arena", (PyObject *) &KosArenaType) ||
      PyDict_SetItemString(dict, "CodeAllocator",
                           (PyObject *) &KosCodeAllocatorType)) {
    return -1;
  }

  /* the reservation alignment, and the chunk unit in bytes: 0 on a
     system that reports no page size or a build without read-only
     code protection, where no block is placed */
  if (set_type_constant(&KosArenaType, "UNIT", KOS_ARENA_UNIT) ||
      set_type_constant(&KosArenaType, "CHUNK", KosArena_ChunkUnit())) {
    return -1;
  }
  PyType_Modified(&KosArenaType);

  return 0;
}


int koskari_arena_clear(PyObject *mod) {

  (void) mod;
  return 0;
}


// The end.
