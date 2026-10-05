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


#ifndef KOSKARI_STACK_H
#define KOSKARI_STACK_H


#include <Python.h>

#include "hints.h"


/* === KosSegment structure === */


/**
   KosSegment is a storage-only operand stack segment.

   Control flow metadata (handler and jump targets, enabled flag) lives
   on the TRY, FINALLY, LOOP and FOREACH nodes of the dynamic chain, the
   region kinds. See dynchain.h.

   Each segment holds a flexible array of PyObject pointers and has a
   single owner (a dynamic-chain node); no sharing.

   Invariant: a segment can only grow -- and therefore move, via
   realloc -- while it is the live push target, and only the innermost
   segment receives pushes.  The sole tolerated exception is a
   parent-slot delivery push, which writes through the owning slot and
   so survives the move.

   The back field is an unwind cache: the owning slot (the address of
   the enclosing owner's stack field) of the segment that was live
   when this segment's owner was installed.  It serves two distinct
   consumers: (a) uninstall restoration -- re-aiming the eval state's
   live block slot when the owner pops, a read-only use -- and (b)
   parent delivery, pushing a value through the enclosing slot, the
   tolerated mutation of a non-active segment noted above.  It is set
   at install time only; DeepCopy results are born with back NULL; a
   NULL back means fall back to the dynamic-chain walk, which remains
   the semantic definition.  The link is meaningful only while the
   owner is on the live chain of the current eval state: any operation
   that detaches a span must clear the backs of the segments it
   detaches.  While a segment sits on the freelist the field is reused
   as the freelist link.
*/

struct KosSegment {
  KosSegment **back;  /* unwind cache: enclosing owning slot, or NULL */

  size_t max;         /* allocated count of objs */
  size_t len;         /* used objs */

  PyObject *objs[0];  /* flexible array member */
};


/* === inline segments === */


#define KOS_SEGMENT_INLINE_SLOTS 8


/**
   Embedded segment storage: a KosSegment header immediately followed
   by its slot array, laid out inside an owning structure (a dynamic
   chain node). Never heap-freed and never freelisted; growth spills
   the contents to a heap segment (KosStack_Grow handles this
   transparently through the owning slot). Discriminated from heap
   segments by capacity: inline capacity sits below the heap birth
   size.
*/
typedef struct KosInlineSegment {
  KosSegment hdr;
  PyObject *slots[KOS_SEGMENT_INLINE_SLOTS];
} KosInlineSegment;


#define KosSegment_IS_INLINE(seg) \
  (KosSegment_MAX(seg) <= KOS_SEGMENT_INLINE_SLOTS)


/**
   Reset embedded storage to an empty segment and return its header.
*/
static inline KosSegment *kos_inline_segment_init(KosInlineSegment *is) {
  is->hdr.back = NULL;
  is->hdr.max = KOS_SEGMENT_INLINE_SLOTS;
  is->hdr.len = 0;
  return &is->hdr;
}


/**
   Detach a segment for storage away from its owning node. A heap
   segment is returned as-is. An inline segment cannot outlive or
   leave its node: its contents move (refcount-free) to a fresh heap
   segment, the inline storage is left empty, and the heap segment is
   returned. Returns NULL on allocation failure (source untouched).
   The caller owns re-pointing its slot.
*/
KosSegment *KosStack_Detach(KosSegment *seg);


/* === accessor macros === */


#define KosSegment_BACK(sb)                \
  (((KosSegment *) sb)->back)

#define KosSegment_MAX(sb)                   \
  (((KosSegment *) sb)->max)

#define KosSegment_LEN(sb)                   \
  (((KosSegment *) sb)->len)


/* === public API === */


/**
   Release a stack segment. Caller must hold exclusive ownership.
 */
void KosStack_Release(KosSegment *block);


/**
   Traverse a KosSegment segment for GC.

   Used when traversing owning frames and dynamic-chain nodes for GC.
   Visits all PyObject pointers stored in the segment.

   :param block: segment (may be NULL)
   :param visit: visitproc from Python GC
   :param arg: opaque visitor argument

   :return: 0 on success, non-zero if visit returns non-zero
 */
int KosStack_Traverse(
    KosSegment *block,
    visitproc visit,
    void *arg);


/**
   Deep-copy a stack segment: new segment, same values (new refs).
   Caller must Release the result. Returns NULL if block is NULL or on error.
 */
KosSegment *KosStack_DeepCopy(KosSegment *block);


/**
   Allocate an empty operand stack segment. Caller must Release. Used
   when installing dyn nodes that own a stack.
 */
KosSegment *KosStack_NewEmpty(void);


/**
   Push val onto the segment (increments ref). val is borrowed; the
   segment gains a new reference. For use by eval/dynchain on frame
   stacks. Grows as needed.

   :return: 0 on success, -1 on error
 */
int KosStack_PushRef(KosSegment **block, PyObject *val);


/**
   Push val onto the segment, stealing the reference. val is consumed
   (ownership transferred). Saves an incref/decref compared to PushRef
   when the caller already owns val. On success, the segment owns val.
   On failure (-1) will Py_DECREF val.

   :return: 0 on success, -1 on error
 */
int KosStack_PUSH(KosSegment **block, PyObject *val);


/**
   Pop one value from the segment. Caller owns the returned reference.

   :return: value or NULL on underrun (sets exception)
 */
PyObject *KosStack_PopRef(KosSegment **block);


/**
   Duplicate the last count elements onto the segment.

   Sets err and returns -1 if underrun or overrun
 */
int KosStack_Duplicate(KosSegment **self, size_t count);


/**
   Pointer to count elements back from top of the segment. Does not
   modify the segment.

   Sets err and returns NULL if underrun
 */
PyObject *const *KosStack_TailPtr(KosSegment *self, size_t count);


/**
   Borrow reference to TOS. Sets err and returns NULL if underrun.
 */
PyObject *KosStack_Peek(KosSegment *self);


/**
   Borrow reference at index. 0 = bottom, -1 = TOS.
   Sets err and returns NULL if over/underrun.
 */
PyObject *KosStack_PeekAt(KosSegment *self, Py_ssize_t index);


/**
   Remove the last count elements. Sets err on underrun.
 */
int KosStack_Drop(KosSegment **self, size_t count);


/**
   Remove the last element. Sets err on underrun.
*/
int KosStack_Drop1(KosSegment **self);


/**
   Replace the last two elements with val, an owned reference that the
   stack takes: val holds the lower position before either old element
   is released. Sets err on underrun.
*/
int KosStack_Replace2(KosSegment **self, PyObject *val);

/* Settle a pair whose left reference the caller consumed: val, owned,
   takes the left's cell and the right is released; or with val NULL
   the right slides down into the left's cell. For an in-place append
   on the left, whose result replaces it and whose failure has already
   released it. */
int KosStack_Settle2(KosSegment **self, PyObject *val);


/**
   Remove the last count elements. Does not set err.
 */
int KosStack_DropNoErr(KosSegment **self, size_t count);


/**
   Steal the last count elements into a new tuple.
 */
PyObject *KosStack_PopTail(KosSegment **self, size_t count);


/**
   Steal the last count elements into dest[0..count-1]. The segment
   loses the refs; caller owns them. Returns -1 on underrun.
 */
int KosStack_PopTailPtr(KosSegment **self, size_t count, PyObject **dest);


/* === Opcode-loop fast paths (API bypass) === */


/*
   Inline fast paths for the hot stack operations in the opcode
   dispatch loop.  The common case -- a segment with spare capacity for
   a push, or enough occupied slots for a pop/peek/drop -- is a couple
   of loads and one store, with no function call.  Every slow or
   failing case (grow, empty-segment allocation, underrun reporting)
   delegates to the out-of-line implementations in stack.c, so the
   segment-management logic and the error strings live in exactly one
   place.

   Semantics are bit-identical to the out-of-line functions, including
   the count == 0 quirks (TailPtr returns NULL with no error; DropNoErr
   and PopTailPtr return 0).

   Under KOS_STACK_TRACE builds these collapse to the out-of-line
   calls so the stack chatter stays complete.
*/

#if KOS_STACK_TRACE

#define kos_stack_push_ref_fast     KosStack_PushRef
#define kos_stack_push_steal_fast   KosStack_PUSH
#define kos_stack_pop_ref_fast      KosStack_PopRef
#define kos_stack_drop1_fast        KosStack_Drop1
#define kos_stack_replace2_fast     KosStack_Replace2
#define kos_stack_settle2_fast      KosStack_Settle2
#define kos_stack_drop_noerr_fast   KosStack_DropNoErr
#define kos_stack_peek_fast         KosStack_Peek
#define kos_stack_tail_ptr_fast     KosStack_TailPtr
#define kos_stack_pop_tail_ptr_fast KosStack_PopTailPtr
#define kos_stack_clear_fast        KosStack_Clear

#else  /* ! KOS_STACK_TRACE */

static inline int kos_stack_push_ref_fast(
        KosSegment **block,
        PyObject *val) {

  KosSegment *s = *block;
  if (likely(s && s->len < s->max)) {
    Py_INCREF(val);
    s->objs[s->len++] = val;
    return 0;
  }
  return KosStack_PushRef(block, val);
}


static inline int kos_stack_push_steal_fast(
        KosSegment **block,
        PyObject *val) {

  KosSegment *s = *block;
  if (likely(s && s->len < s->max)) {
    s->objs[s->len++] = val;
    return 0;
  }
  return KosStack_PUSH(block, val);
}


static inline PyObject *kos_stack_pop_ref_fast(KosSegment **block) {

  KosSegment *s = *block;
  if (likely(s && s->len))
    return s->objs[--s->len];
  return KosStack_PopRef(block);
}


static inline int kos_stack_drop1_fast(KosSegment **block) {

  KosSegment *s = *block;
  if (likely(s && s->len)) {
    Py_DECREF(s->objs[--s->len]);
    return 0;
  }
  return KosStack_Drop1(block);
}


/* No underrun guard: a caller holding a tail pointer of two has
   already proved the length. */
static inline int kos_stack_replace2_fast(KosSegment **block, PyObject *val) {

  KosSegment *s = *block;
  PyObject *left = s->objs[s->len - 2];
  PyObject *right = s->objs[s->len - 1];

  s->objs[s->len - 2] = val;
  s->len -= 1;
  Py_DECREF(right);
  Py_DECREF(left);
  return 0;
}


/* No underrun guard, as replace2. */
static inline int kos_stack_settle2_fast(KosSegment **block, PyObject *val) {

  KosSegment *s = *block;
  PyObject *right = s->objs[s->len - 1];

  s->len -= 1;
  if (val) {
    s->objs[s->len - 1] = val;
    Py_DECREF(right);
  } else {
    s->objs[s->len - 1] = right;
  }
  return 0;
}


static inline int kos_stack_drop_noerr_fast(
        KosSegment **block,
        size_t count) {

  KosSegment *s = *block;
  if (likely(s && count <= s->len)) {
    size_t len = s->len;
    while (count--)
      Py_DECREF(s->objs[--len]);
    s->len = len;
    return 0;
  }
  return KosStack_DropNoErr(block, count);
}


/* No NULL guard, mirroring KosStack_Peek exactly. */
static inline PyObject *kos_stack_peek_fast(KosSegment *s) {

  if (likely(s->len))
    return s->objs[s->len - 1];
  return KosStack_Peek(s);
}


static inline PyObject *const *kos_stack_tail_ptr_fast(
        KosSegment *s,
        size_t count) {

  if (likely(s && count && count <= s->len))
    return &s->objs[s->len - count];
  return KosStack_TailPtr(s, count);
}


static inline int kos_stack_pop_tail_ptr_fast(
        KosSegment **block,
        size_t count,
        PyObject **dest) {

  KosSegment *s = *block;
  if (likely(s && count && count <= s->len)) {
    size_t len = s->len;
    while (count--)
      dest[count] = s->objs[--len];
    s->len = len;
    return 0;
  }
  return KosStack_PopTailPtr(block, count, dest);
}


/* Release every value on the segment and empty it; the segment stays
   attached. Mirrors KosStack_Clear, which never fails. */
static inline void kos_stack_clear_fast(KosSegment **block) {

  KosSegment *s = *block;
  size_t len = s->len;

  while (len)
    Py_DECREF(s->objs[--len]);
  s->len = 0;
}

#endif  /* KOS_STACK_TRACE */


/**
   Pop tail slots into an aggregate of PyObject * fields only. Count is
   sizeof(agg) / sizeof(PyObject *). Field order must be deepest stack slot
   first, TOS last (matches PopTailPtr dest layout). Returns 0 on success,
   -1 on underrun.
 */
#define KOS_STACK_BLOCK_POP_ARGS(block_pp, agg) \
  kos_stack_pop_tail_ptr_fast( \
      (block_pp), \
      sizeof(agg) / sizeof(PyObject *), \
      (PyObject **)(void *)&(agg))


/* === KosStackSteal: fast-path tail extraction for call sites === */


#define KOS_STACK_STEAL_FAST_SIZE 16


/**
   KosStackSteal holds stolen tail elements from a segment. Uses an
   embedded array for small counts; allocates for larger. Caller must
   invoke KosStackSteal_Free when done.

   owned_start: indices [0, owned_start) are borrowed (not DECREF'd by Free).
   Default 0 (own all). TailPrepend sets 1 so vals[0] (prefix) is skipped.
*/
struct KosStackSteal {
  PyObject *fast[KOS_STACK_STEAL_FAST_SIZE];
  size_t len;
  size_t owned_start;   /* skip DECREF for indices < this */
  PyObject **vals;      /* points to fast or malloc'd array */
};


/**
   Steal the last count elements from the block into s. Copies refs
   (no incref/decref); block loses ownership. Block len is reduced.
   Use when the caller must clear the stack before invoking code that
   may push (e.g. KosCallable install). On failure, s is left safe for
   KosStackSteal_Free (len 0, vals = fast).

   :return: 0 on success, -1 on underrun or alloc failure
 */
int KosStackSteal_Tail(
    KosStackSteal *s,
    size_t count,
    KosSegment **block);


/**
   Steal the last count elements from the block into s, with prefix
   prepended as vals[0]. prefix is borrowed (never DECREF'd by Free).
   s->len = count + 1, s->owned_start = 1.

   :return: 0 on success, -1 on underrun or alloc failure
 */
int KosStackSteal_TailPrepend(
    KosStackSteal *s,
    PyObject *prefix,
    size_t count,
    KosSegment **block);


/**
   Release all references in s and free the array if malloc'd. Safe to
   call on a failed or partially initialized KosStackSteal.
 */
void KosStackSteal_Free(KosStackSteal *s);


/**
   Pop count * 2 elements as key,val pairs; return a new dict. Stack layout: key_0, val_0, ..., key_{n-1}, val_{n-1}
   with val_{n-1} at TOS. Caller owns the returned reference.
 */
PyObject *KosStack_PopTailDict(KosSegment **self, size_t pair_count);


/**
   TOS = names tuple, below it count values. Pops names and count values, returns a new dict mapping each name to
   its value. Caller owns the returned reference.
 */
PyObject *KosStack_PopTailDictNames(KosSegment **self, size_t count);


/**
   Given a names tuple (not on stack), pop len(names) values and return a new dict mapping each name to its
   value. Stack layout: val_0 deepest, val_{n-1} at TOS; names[i] maps
   to val_i. Caller owns the returned reference.
 */
PyObject *KosStack_PopTailDictFromNames(
    KosSegment **self,
    PyObject *names);


/**
   Clear the segment in place: drop refs to all elements and set
   length to 0.

   :return: 0 on success, -1 on error
 */
int KosStack_Clear(KosSegment **block);


/* === unit lifecycle === */


int koskari_stack_init(PyObject *mod);
int koskari_stack_clear(PyObject *mod);


#endif


// The end.
