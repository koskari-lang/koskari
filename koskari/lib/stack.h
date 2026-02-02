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


/* === BlockStyle enum === */


typedef enum KosBlockStyle {
  KOS_BLOCK_BEGIN = 0,
  KOS_BLOCK_FINALLY,
  KOS_BLOCK_FOREACH,
  KOS_BLOCK_LOOP,
  KOS_BLOCK_NS,
  KOS_BLOCK_TRY,
  KOS_BLOCK_WITH,
  KOS_BLOCK_AWAIT_FOREACH,
  KOS_BLOCK_AWAIT_WITH,
} KosBlockStyle;


/* === StackBlock structure === */


typedef struct KosStackBlock KosStackBlock;


struct KosStackBlock {
  KosStackBlock *back;           /* previous block */

  size_t refcount;               /* for copy-on-write */

  KosBlockStyle block_style;     /* determines signal handling */
  int enabled;                   /* 0/1 if handler should trigger */
  Py_ssize_t a_label;            /* behavior jump label */
  Py_ssize_t b_label;            /* optional second label */

  size_t max;                    /* allocated count of objs */
  size_t len;                    /* used objs */

  PyObject *objs[0];             /* flexible array member */
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

#define KosStackBlock_REFCOUNT(sb)              \
  (((KosStackBlock *) sb)->refcount)

#define KosStackBlock_MAX(sb)                   \
  (((KosStackBlock *) sb)->max)

#define KosStackBlock_LEN(sb)                   \
  (((KosStackBlock *) sb)->len)

#define KosStackBlock_BLOCK_STYLE(sb)           \
  (((KosStackBlock *) sb)->block_style)

#define KosStackBlock_ENABLED(sb)               \
  (((KosStackBlock *) sb)->enabled)

#define KosStackBlock_LABEL_A(sb)               \
  (((KosStackBlock *) sb)->a_label)

#define KosStackBlock_LABEL_B(sb)               \
  (((KosStackBlock *) sb)->b_label)


/* === public API === */


PyObject *KosStack_New(void);


/**
   Produces a new copy of stack. This is copy-on-write optimized to
   point to the same stack blocks.
 */
PyObject *KosStack_Copy(PyObject *original);


/**
   Obtain a COW reference to a stack block chain. Increments refcount
   on all blocks in the chain.
 */
KosStackBlock *KosStackBlock_COW(KosStackBlock *block);


/**
   Release a stack block (decrement refcount, free if zero).
 */
void KosStackBlock_Release(KosStackBlock *block);


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
int KosStack_ExtendPtr(PyObject *stack,
                       PyObject *const *elements, size_t count,
                       int reverse);


/**
   Extends elements to the current block's stack, stealing references.

   If reverse is non-zero then the elements will be pushed onto the
   stack in reverse order (first element will be TOS)

   If unsuccessful, calls Py_DECREF on all elements

   :return: 0 if successful, sets err and returns -1 if overrun
 */
int KosStack_EXTENDPTR(PyObject *stack,
                       PyObject *const *elements, size_t count,
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
   Allocates a new block and makes it the current block
 */
int KosStack_PushBlock(PyObject *stack);


int KosStack_PushBlockFull(PyObject *stack, KosBlockStyle style,
                           Py_ssize_t label_a, Py_ssize_t label_b);


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
