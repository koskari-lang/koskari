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


/* === StackBlock structure === */


/**
   KosStackBlock is a storage-only operand stack segment.

   Control flow metadata (block style, labels, enabled flag) has been
   moved to KosDynRegion nodes in the dynamic chain. See dynchain.h.

   The stack is organized as a linked list of blocks. Each block holds
   a flexible array of PyObject pointers. Each block chain has a single
   owner; no sharing.
*/

struct KosStackBlock {
  KosStackBlock *back;    /* previous block (toward bottom) */

  size_t max;             /* allocated count of objs */
  size_t len;             /* used objs */

  PyObject *objs[0];      /* flexible array member */
};


/* === Stack type === */


extern PyTypeObject KosStackType;


typedef struct KosStack {
  PyObject_HEAD

  KosStackBlock *block;
} KosStack;


/* === type check macros === */


#define KosStack_Check(obj)                                     \
  ((obj) && PyType_IsSubtype((obj)->ob_type, &KosStackType))

#define KosStack_CheckExact(obj)                \
  ((obj) && ((obj)->ob_type == &KosStackType))


/* === accessor macros === */


#define KosStack_BLOCK(s)                       \
  (((KosStack *) s)->block)

#define KosStackBlock_PARENT(sb)                \
  (((KosStackBlock *) sb)->back)

#define KosStackBlock_MAX(sb)                   \
  (((KosStackBlock *) sb)->max)

#define KosStackBlock_LEN(sb)                   \
  (((KosStackBlock *) sb)->len)


/* === public API === */


PyObject *KosStack_New(void);


/**
   Produces a new copy of stack. Performs a full deep copy; the result
   is independent of the original.
 */
PyObject *KosStack_Copy(PyObject *original);


/**
   Release a stack block. Caller must hold exclusive ownership.
 */
void KosStackBlock_Release(KosStackBlock *block);


/**
   Release a stack block but do not DECREF any slot equal to exclude
   (used when the block chain contains a reference to the object being
   deallocated). exclude may be NULL for normal release.
 */
void KosStackBlock_ReleaseExclude(KosStackBlock *block, PyObject *exclude);


/**
   Release a full chain; exclude is passed to each block (see
   KosStackBlock_ReleaseExclude).
 */
void KosStackBlock_ReleaseChainExclude(KosStackBlock *block, PyObject *exclude);


/**
   Deep-copy a stack block chain: new blocks, same values (new refs).
   Caller must Release the result. Returns NULL if block is NULL or on error.
 */
KosStackBlock *KosStackBlock_DeepCopy(KosStackBlock *block);


/**
   Push val onto the block chain (increments ref). val is borrowed; stack
   gains a new reference. For use by eval/dynchain on frame stacks.
   Grows as needed.

   :return: 0 on success, -1 on error
 */
int KosStackBlock_PushRef(KosStackBlock **block, PyObject *val);


/**
   Push val onto the block chain, stealing the reference. val is consumed
   (ownership transferred). Saves an incref/decref compared to PushRef
   when the caller already owns val. On success, the block owns val.
   On failure (-1) will Py_DECREF val.

   :return: 0 on success, -1 on error
 */
int KosStackBlock_PUSH(KosStackBlock **block, PyObject *val);


/**
   Pop one value from the block chain. Caller owns the returned reference.

   :return: value or NULL on underrun (sets exception)
 */
PyObject *KosStackBlock_PopRef(KosStackBlock **block);


/**
   Appends val to the current block's stack, incrementing the reference.

   :return: 0 if successful, sets err and returns -1 if overrun
 */
int KosStack_Push(PyObject *stack, PyObject *val);


/**
   Appends val to current block's stack, stealing the reference.

   If unsuccessful, calls Py_DECREF on val

   :return: 0 if successful, sets err and returns -1 if overrun
 */
int KosStack_PUSH(PyObject *stack, PyObject *val);


/**
   Extends elements from seq in-order onto the current block's stack.

   If reverse is non-zero then the elements will be pushed onto the
   stack in reverse order (first element will be TOS)

   :return: 0 if successful, sets err and returns -1 if overrun
 */
int KosStack_Extend(PyObject *stack, PyObject *seq, int reverse);


/**
   Extends elements to the current block's stack, incrementing references.

   If reverse is non-zero then the elements will be pushed onto the
   stack in reverse order (first element will be TOS)

   :return: 0 if successful, sets err and returns -1 if overrun
 */
int KosStack_ExtendPtr(
    PyObject *stack,
    PyObject *const *elements,
    size_t count,
    int reverse);


/**
   Extends elements to the current block's stack, stealing references.

   If reverse is non-zero then the elements will be pushed onto the
   stack in reverse order (first element will be TOS)

   If unsuccessful, calls Py_DECREF on all elements

   :return: 0 if successful, sets err and returns -1 if overrun
 */
int KosStack_EXTENDPTR(
    PyObject *stack,
    PyObject *const *elements,
    size_t count,
    int reverse);


/**
   Borrows reference to the last element of the current block's stack

   Sets err and returns NULL if underrun
 */
PyObject *KosStack_Peek(PyObject *stack);


/**
   Borrows reference to the element at index of the current block's
   stack. 0 is the index of the bottom of the stack. An index of -1 is
   the same as KosStack_Peek.

   Sets err and returns NULL if over/underrun
 */
PyObject *KosStack_PeekAt(PyObject *stack, Py_ssize_t index);


/**
   Copies references from the top of the parent block's stack in order
   to the current block's stack

   Sets err and returns -1 if underrun on parent block or overrun on
   current block
 */
int KosStack_Pull(PyObject *stack, size_t count);


/**
   Steals the last element from the current block's stack and returns
   it, lowering the block's stack by one

   Sets err and returns NULL if underrun
 */
PyObject *KosStack_Pop(PyObject *stack);


/**
   Steals the last count elements from the current block's stack,
   returns them inside a new PyTuple reference, lowers the block's stack
   by count

   Sets err and returns NULL if underrun
 */
PyObject *KosStack_PopTail(PyObject *stack, size_t count);


/**
   Steals the last (count * 2) elements from the current block's
   stack, returns them inside a new PyDict reference, lowers the
   block's stack by (count * 2)

   Elements from the stack must be in key, val order. They are
   inserted into the dict from bottom up

   Sets err and returns NULL if underrun
 */
PyObject *KosStack_PopTailDict(PyObject *stack, size_t count);


/**
   Top of stack is expected to be a tuple of keys. Below that, count
   elements correspond to the values for those keys.

   Returns a new PyDict reference mapping keys to values.

   Sets err and returns NULL if underrun
 */
PyObject *KosStack_PopTailDictNames(PyObject *stack, size_t count);


/**
   Pointer to count elements back from top of current block's stack

   Sets err and returns NULL if underrun
 */
PyObject *const *KosStack_TailPtr(PyObject *stack, size_t count);


/**
   Steals the last count elements from the current block's stack, copies
   them to an array at dest

   dest must have at least count elements available, or it will result
   in a segfault.

   Sets err and returns -1 if underrun
 */
int KosStack_PopTailPtr(PyObject *stack, size_t count, PyObject **dest);


/**
   Removes the last count elements from the current block's stack

   Sets err and returns -1 if underrun
 */
int KosStack_Drop(PyObject *stack, size_t count);


/**
   Removes the last count elements from the current block's stack

   Returns -1 if underrun, does not set err
 */
int KosStack_DropNoErr(PyObject *stack, size_t count);


/**
   Duplicates the last count elements of the current block's stack
   onto the same block

   Sets err and returns -1 if underrun or overrun
 */
int KosStack_Duplicate(PyObject *stack, size_t count);


/**
   Replaces stack entry at index (counted from bottom) with value

   Sets err and returns -1 if underrun or if value is NULL
 */
int KosStack_Replace(PyObject *stack, size_t index, PyObject *value);


/**
   Block-level API for eval: duplicate the last count elements.
   Same semantics as KosStack_Duplicate but operates on block chain.
 */
int KosStackBlock_Duplicate(KosStackBlock **self, size_t count);


/**
   Block-level API for eval: pointer to count elements back from top.
   Same semantics as KosStack_TailPtr. Does not modify stack.
 */
PyObject *const *KosStackBlock_TailPtr(KosStackBlock *self, size_t count);


/**
   Block-level API for eval: borrow reference to TOS. Same semantics as
   KosStack_Peek. Sets err and returns NULL if underrun.
 */
PyObject *KosStackBlock_Peek(KosStackBlock *self);


/**
   Block-level API for eval: borrow reference at index. 0 = bottom, -1 = TOS.
   Same semantics as KosStack_PeekAt. Sets err and returns NULL if over/underrun.
 */
PyObject *KosStackBlock_PeekAt(KosStackBlock *self, Py_ssize_t index);


/**
   Block-level API for eval: remove last count elements. Sets err on underrun.
 */
int KosStackBlock_Drop(KosStackBlock **self, size_t count);


/**
   Block-level API for eval: remove last element. Sets err on underrun.
*/
int KosStackBlock_Drop1(KosStackBlock **self);


/**
   Block-level API for eval: remove last count elements. Does not set err.
 */
int KosStackBlock_DropNoErr(KosStackBlock **self, size_t count);


/**
   Block-level API for eval: steal last count elements into a new tuple.
 */
PyObject *KosStackBlock_PopTail(KosStackBlock **self, size_t count);


/**
   Block-level API for eval: steal last count elements into dest[0..count-1].
   Block loses refs; caller owns them. Returns -1 on underrun.
 */
int KosStackBlock_PopTailPtr(KosStackBlock **self, size_t count, PyObject **dest);


/* === KosStackSteal: fast-path tail extraction for call sites === */


#define KOS_STACK_STEAL_FAST_SIZE 16


/**
   KosStackSteal holds stolen tail elements from a stack block. Uses an
   embedded array for small counts; allocates for larger. Caller must
   invoke KosStackSteal_Free when done.
*/
typedef struct KosStackSteal {
  PyObject *fast[KOS_STACK_STEAL_FAST_SIZE];
  size_t len;
  PyObject **vals;   /* points to fast or malloc'd array */
} KosStackSteal;


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
    KosStackBlock **block);


/**
   Release all references in s and free the array if malloc'd. Safe to
   call on a failed or partially initialized KosStackSteal.
 */
void KosStackSteal_Free(KosStackSteal *s);


/**
   Block-level API for eval: pop count * 2 elements as key,val pairs;
   return a new dict. Stack layout: key_0, val_0, ..., key_{n-1}, val_{n-1}
   with val_{n-1} at TOS. Caller owns the returned reference.
 */
PyObject *KosStackBlock_PopTailDict(KosStackBlock **self, size_t pair_count);


/**
   Block-level API for eval: TOS = names tuple, below it count values.
   Pops names and count values, returns a new dict mapping each name to
   its value. Caller owns the returned reference.
 */
PyObject *KosStackBlock_PopTailDictNames(KosStackBlock **self, size_t count);


/**
   Block-level API for eval: given a names tuple (not on stack), pop
   len(names) values and return a new dict mapping each name to its
   value. Stack layout: val_0 deepest, val_{n-1} at TOS; names[i] maps
   to val_i. Caller owns the returned reference.
 */
PyObject *KosStackBlock_PopTailDictFromNames(
    KosStackBlock **self,
    PyObject *names);


/**
   Push a new empty block onto the block chain. *block is updated to the
   new top. For use by eval when entering loop/foreach blocks.

   :return: 0 on success, -1 on error
 */
int KosStackBlock_PushBlock(KosStackBlock **block);


/**
   Drop the top block without moving values to the parent. *block must
   have a parent (cannot drop root block).

   :return: 0 on success, -1 on error
 */
int KosStackBlock_DropBlock(KosStackBlock **block);


/**
   Steal the last count elements from the current block, append them
   to the parent block, then drop the current block. For count 0,
   same as DropBlock.

   :return: 0 on success, -1 on error
 */
int KosStackBlock_PopBlock(KosStackBlock **block, size_t count);


/**
   Clear the current block in place: drop refs to all elements and set
   length to 0. Does not drop or push blocks.

   :return: 0 on success, -1 on error
 */
int KosStackBlock_ClearBlock(KosStackBlock **block);


/**
   Allocates a new block and makes it the current block
 */
int KosStack_PushBlock(PyObject *stack);


/**
   Steals the last count elements from the current block's stack,
   then pops the block, and pushes the stolen elements onto the
   parent block's stack.
 */
int KosStack_PopBlock(PyObject *stack, size_t count);


/**
   Drops references to all elements in the current block's stack,
   frees the current block, reveals the parent block
 */
int KosStack_DropBlock(PyObject *stack);


/**
   Drops references to all elements in all blocks, and frees all
   but the first block, leaving it empty and current
 */
int KosStack_Clear(PyObject *stack);


/* === unit lifecycle === */


int koskari_stack_init(PyObject *mod);
int koskari_stack_clear(PyObject *mod);


#endif


// The end.
