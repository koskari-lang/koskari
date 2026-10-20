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

   Arena allocation for KosCode blocks. A KosArena owns one chunk, an
   anonymous mapping sized in whole chunk units: callers reserve slices
   while it is writable, then it is sealed read-only once with a single
   mprotect. Deallocation unmaps the chunk.

   The arena is a non-GC leaf owner of raw memory. It holds no
   references to the objects whose storage lives inside it; each member
   KosCode instead holds a strong reference to its arena, so the arena
   dies (unmaps) when the last member does.

   A KosCodeAllocator is the set of chunks one module load fills. A
   code block is placed into the first chunk with room for it, or into
   a new chunk when none has room, and stays at that address for its
   whole life. Sealing the allocator seals every chunk and closes it.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
*/


#ifndef KOSKARI_ARENA_H
#define KOSKARI_ARENA_H


#include <Python.h>


/* === arena structure === */


typedef struct KosArena {
  PyObject_HEAD

  void *base;      /* page-aligned anonymous mapping */
  size_t size;     /* mapped bytes, a whole number of chunk units */
  size_t fill;     /* end of the newest reservation */
  int sealed;      /* nonzero once mprotect(PROT_READ) applied */

  PyObject *weakreflist;  /* weak reference support */
} KosArena;


typedef struct KosCodeAllocator {
  PyObject_HEAD

  PyObject *chunks;  /* list of KosArena, in creation order; NULL once
                        sealed */
} KosCodeAllocator;


/* Every reservation starts on this boundary: members hold 32-byte
   instruction records that must not straddle a cache line. Exposed to
   Python as Arena.UNIT. */
#define KOS_ARENA_UNIT 32

/* The requested chunk size. Every chunk is a whole number of chunk
   units, where the unit is this size rounded up to the system page
   size, so a system with larger pages uses one of its own pages.
   Exposed to Python as Arena.CHUNK, after the rounding. */
#define KOS_ARENA_CHUNK_BYTES 4096

#define KosArena_BASE(a)    (((KosArena *)(a))->base)
#define KosArena_SIZE(a)    (((KosArena *)(a))->size)
#define KosArena_FILL(a)    (((KosArena *)(a))->fill)
#define KosArena_SEALED(a)  (((KosArena *)(a))->sealed)

#define KosCodeAllocator_CHUNKS(a)  (((KosCodeAllocator *)(a))->chunks)


/* === type objects === */


extern PyTypeObject KosArenaType;
extern PyTypeObject KosCodeAllocatorType;


/* === type check macros === */


#define KosArena_Check(obj)                                     \
  ((obj) && PyType_IsSubtype((obj)->ob_type, &KosArenaType))

#define KosArena_CheckExact(obj)                \
  ((obj) && ((obj)->ob_type == &KosArenaType))

#define KosCodeAllocator_Check(obj)                                     \
  ((obj) && PyType_IsSubtype((obj)->ob_type, &KosCodeAllocatorType))

#define KosCodeAllocator_CheckExact(obj)                \
  ((obj) && ((obj)->ob_type == &KosCodeAllocatorType))


/* === public API === */


/**
   The chunk unit in bytes: KOS_ARENA_CHUNK_BYTES rounded up to the
   system page size.
*/
size_t KosArena_ChunkUnit(void);


/**
   Create a new writable arena with capacity for at least nbytes,
   rounded up to a whole number of chunk units.

   :param nbytes: minimum payload capacity; must be positive

   :return: new KosArena reference, or NULL on error
*/
PyObject *KosArena_New(size_t nbytes);


/**
   Reserve a slice of nbytes from the arena, aligned to KOS_ARENA_UNIT.

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


/**
   Return a reservation to the arena when it is the newest one and the
   arena is unsealed; otherwise do nothing. Its bytes are then handed
   out again by the next reservation.

   :param arena: KosArena object
   :param base: the reservation's base, as KosArena_Reserve gave it
   :param nbytes: the reservation's size, as requested
*/
void KosArena_Release(PyObject *arena, void *base, size_t nbytes);


/**
   Create a new open code allocator holding no chunks.

   :return: new KosCodeAllocator reference, or NULL on error
*/
PyObject *KosCodeAllocator_New(void);


/**
   Reserve nbytes for one code block: from the first chunk with room
   for it, or from a new chunk of a whole number of chunk units when
   none has room.

   :param alloc: KosCodeAllocator object, not yet sealed
   :param nbytes: block size in bytes; must be positive
   :param out: receives the block base
   :param out_chunk: receives the chunk holding the block, borrowed

   :return: 0 on success; -1 with an error set when the allocator is
     sealed or a chunk cannot be mapped
*/
int KosCodeAllocator_Reserve(
    PyObject *alloc,
    size_t nbytes,
    void **out,
    PyObject **out_chunk);


/**
   Seal every chunk read-only and close the allocator. Later
   reservations raise.

   :param alloc: KosCodeAllocator object, not yet sealed

   :return: new tuple of the sealed chunks, or NULL on error
*/
PyObject *KosCodeAllocator_Seal(PyObject *alloc);


/* === unit lifecycle === */


int koskari_arena_init(PyObject *mod);
int koskari_arena_clear(PyObject *mod);


#endif


// The end.
