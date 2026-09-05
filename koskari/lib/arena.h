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
   koskari/lib/arena.h

   Page-aligned arena allocation for packed KosCode blocks. A KosArena
   owns one page-aligned chunk: callers reserve slices while it is
   writable, then it is sealed read-only once with a single mprotect.
   Deallocation unseals and frees the chunk.

   The arena is a non-GC leaf owner of raw memory. It holds no
   references to the objects whose storage lives inside it; each packed
   KosCode instead holds a strong reference to its arena, so the arena
   dies (unseals, frees) when the last member does.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
*/


#ifndef KOSKARI_ARENA_H
#define KOSKARI_ARENA_H


#include <Python.h>


/* === arena structure === */


typedef struct KosArena {
  PyObject_HEAD

  void *base;      /* page-aligned chunk */
  size_t size;     /* total allocation, page-rounded */
  size_t fill;     /* bytes handed out so far */
  int sealed;      /* nonzero once mprotect(PROT_READ) applied */

  PyObject *weakreflist;  /* weak reference support */
} KosArena;


/* Every reservation starts on this boundary: members hold 32-byte
   instruction records that must not straddle a cache line, and a
   planner sizing an arena from its members' footprints aligns each
   to it. Exposed to Python as Arena.UNIT. */
#define KOS_ARENA_UNIT 32

#define KosArena_BASE(a)    (((KosArena *)(a))->base)
#define KosArena_SIZE(a)    (((KosArena *)(a))->size)
#define KosArena_FILL(a)    (((KosArena *)(a))->fill)
#define KosArena_SEALED(a)  (((KosArena *)(a))->sealed)


/* === type objects === */


extern PyTypeObject KosArenaType;


/* === type check macros === */


#define KosArena_Check(obj)                                     \
  ((obj) && PyType_IsSubtype((obj)->ob_type, &KosArenaType))

#define KosArena_CheckExact(obj)                \
  ((obj) && ((obj)->ob_type == &KosArenaType))


/* === public API === */


/**
   Create a new writable arena with capacity for at least nbytes,
   rounded up to page size.

   :param nbytes: minimum payload capacity; must be positive

   :return: new KosArena reference, or NULL on error
*/
PyObject *KosArena_New(size_t nbytes);


/**
   Reserve a pointer-aligned slice of nbytes from the arena.

   :param arena: KosArena object, unsealed
   :param nbytes: slice size in bytes
   :param out: receives the slice base

   :return: 0 on success; -1 with an error set when the arena is
     sealed or lacks capacity
*/
int KosArena_Reserve(PyObject *arena, size_t nbytes, void **out);


/**
   Seal the arena read-only with a single mprotect(PROT_READ).

   :param arena: KosArena object, unsealed

   :return: 0 on success; -1 with an error set when already sealed or
     when mprotect fails
*/
int KosArena_Seal(PyObject *arena);


/* === unit lifecycle === */


int koskari_arena_init(PyObject *mod);
int koskari_arena_clear(PyObject *mod);


#endif


// The end.
