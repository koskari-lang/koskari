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
   Memory tests for Stack type

   Tests reference counting, push/pop operations, and block management
   for Stack and StackBlock types.

   author: Christopher O'Brien <obriencj@gmail.com>
   license: GPL v.3
*/


#include "test_harness.h"
#include "../../koskari/lib/lib.h"


/* Use large values to avoid Python's small integer cache */
#define TEST_VAL_1 10001
#define TEST_VAL_2 10002
#define TEST_VAL_3 10003
#define TEST_VAL_4 10004


/* === Stack tests === */


static int test_stack_create(void) {
    /*
      Test that Stack can be created.
    */
    PyObject *stack = KosStack_New();
    TEST_ASSERT_NOT_NULL(stack, "create Stack");
    TEST_ASSERT(KosStack_CheckExact(stack), "stack type check");

    /* Verify initial state */
    KosStackBlock *block = KosStack_BLOCK(stack);
    TEST_ASSERT_NOT_NULL(block, "has initial block");
    TEST_ASSERT_EQ(KosStackBlock_LEN(block), 0, "block is empty");
    TEST_ASSERT_NULL(KosStackBlock_PARENT(block), "no parent block");

    Py_DECREF(stack);
    return 0;
}


static int test_stack_push_pop(void) {
    /*
      Test basic push and pop operations.

      Pop returns a stolen reference - the stack releases its reference
      and hands it to the caller. Since popped == value, we end up with
      the same refcount as before (just transferred ownership).
    */
    PyObject *stack = KosStack_New();
    TEST_ASSERT_NOT_NULL(stack, "create Stack");

    PyObject *value = PyLong_FromLong(TEST_VAL_1);
    TEST_ASSERT_NOT_NULL(value, "create value");

    Py_ssize_t val_initial = Py_REFCNT(value);

    /* Push increments refcount */
    int result = KosStack_Push(stack, value);
    TEST_ASSERT_EQ(result, 0, "push succeeded");
    TEST_ASSERT_EQ(Py_REFCNT(value), val_initial + 1, "refcount incremented");

    /* Verify length */
    KosStackBlock *block = KosStack_BLOCK(stack);
    TEST_ASSERT_EQ(KosStackBlock_LEN(block), 1, "block has one element");

    /* Pop returns stolen reference - stack releases ref, gives it to us */
    PyObject *popped = KosStack_Pop(stack);
    TEST_ASSERT_NOT_NULL(popped, "pop succeeded");
    TEST_ASSERT_SAME(popped, value, "popped same value");

    /* Stack released its ref, but gave us the stolen ref.
       Since popped == value, refcount is unchanged from after push
       (we had val_initial+1, stack released 1, we got 1 back = val_initial+1) */
    TEST_ASSERT_EQ(Py_REFCNT(value), val_initial + 1, "refcount unchanged after pop");

    TEST_ASSERT_EQ(KosStackBlock_LEN(block), 0, "block is empty after pop");

    /* Decref the stolen reference we received from pop */
    Py_DECREF(popped);
    TEST_ASSERT_EQ(Py_REFCNT(value), val_initial, "refcount restored");

    /* Decref our original reference */
    Py_DECREF(value);
    Py_DECREF(stack);
    return 0;
}


static int test_stack_push_stolen(void) {
    /*
      Test PUSH with stolen reference.
    */
    PyObject *stack = KosStack_New();
    TEST_ASSERT_NOT_NULL(stack, "create Stack");

    PyObject *value = PyLong_FromLong(TEST_VAL_2);
    TEST_ASSERT_NOT_NULL(value, "create value");

    Py_ssize_t val_initial = Py_REFCNT(value);

    /* PUSH steals reference - give it an extra ref to steal */
    Py_INCREF(value);
    TEST_ASSERT_EQ(Py_REFCNT(value), val_initial + 1, "extra ref added");

    int result = KosStack_PUSH(stack, value);
    TEST_ASSERT_EQ(result, 0, "PUSH succeeded");
    /* Still same refcount (stolen the extra ref we gave) */
    TEST_ASSERT_EQ(Py_REFCNT(value), val_initial + 1, "refcount unchanged (stolen)");

    /* Pop steals back */
    PyObject *popped = KosStack_Pop(stack);
    TEST_ASSERT_NOT_NULL(popped, "pop succeeded");
    TEST_ASSERT_SAME(popped, value, "popped same value");

    Py_DECREF(popped);
    Py_DECREF(value);
    Py_DECREF(stack);
    return 0;
}


static int test_stack_peek(void) {
    /*
      Test peek operation.
    */
    PyObject *stack = KosStack_New();
    TEST_ASSERT_NOT_NULL(stack, "create Stack");

    PyObject *value = PyLong_FromLong(TEST_VAL_3);
    TEST_ASSERT_NOT_NULL(value, "create value");

    KosStack_Push(stack, value);

    /* Peek returns borrowed reference */
    PyObject *peeked = KosStack_Peek(stack);
    TEST_ASSERT_NOT_NULL(peeked, "peek succeeded");
    TEST_ASSERT_SAME(peeked, value, "peeked same value");

    /* Block still has the element */
    KosStackBlock *block = KosStack_BLOCK(stack);
    TEST_ASSERT_EQ(KosStackBlock_LEN(block), 1, "element still there");

    Py_DECREF(stack);
    Py_DECREF(value);
    return 0;
}


static int test_stack_peek_at(void) {
    /*
      Test peek_at operation with index.
    */
    PyObject *stack = KosStack_New();
    TEST_ASSERT_NOT_NULL(stack, "create Stack");

    PyObject *v0 = PyLong_FromLong(TEST_VAL_1);
    PyObject *v1 = PyLong_FromLong(TEST_VAL_2);
    PyObject *v2 = PyLong_FromLong(TEST_VAL_3);

    KosStack_Push(stack, v0);
    KosStack_Push(stack, v1);
    KosStack_Push(stack, v2);

    /* Positive indices from bottom */
    PyObject *p0 = KosStack_PeekAt(stack, 0);
    PyObject *p1 = KosStack_PeekAt(stack, 1);
    PyObject *p2 = KosStack_PeekAt(stack, 2);

    TEST_ASSERT_SAME(p0, v0, "index 0 is v0");
    TEST_ASSERT_SAME(p1, v1, "index 1 is v1");
    TEST_ASSERT_SAME(p2, v2, "index 2 is v2");

    /* Negative indices from top */
    PyObject *pm1 = KosStack_PeekAt(stack, -1);
    PyObject *pm2 = KosStack_PeekAt(stack, -2);

    TEST_ASSERT_SAME(pm1, v2, "index -1 is v2");
    TEST_ASSERT_SAME(pm2, v1, "index -2 is v1");

    Py_DECREF(stack);
    Py_DECREF(v0);
    Py_DECREF(v1);
    Py_DECREF(v2);
    return 0;
}


static int test_stack_drop(void) {
    /*
      Test drop operation.
    */
    PyObject *stack = KosStack_New();
    TEST_ASSERT_NOT_NULL(stack, "create Stack");

    PyObject *v0 = PyLong_FromLong(TEST_VAL_1);
    PyObject *v1 = PyLong_FromLong(TEST_VAL_2);
    PyObject *v2 = PyLong_FromLong(TEST_VAL_3);

    Py_ssize_t v1_initial = Py_REFCNT(v1);
    Py_ssize_t v2_initial = Py_REFCNT(v2);

    KosStack_Push(stack, v0);
    KosStack_Push(stack, v1);
    KosStack_Push(stack, v2);

    /* Drop 2 elements */
    int result = KosStack_Drop(stack, 2);
    TEST_ASSERT_EQ(result, 0, "drop succeeded");

    KosStackBlock *block = KosStack_BLOCK(stack);
    TEST_ASSERT_EQ(KosStackBlock_LEN(block), 1, "one element left");

    /* Refcounts should be restored */
    TEST_ASSERT_EQ(Py_REFCNT(v1), v1_initial, "v1 refcount restored");
    TEST_ASSERT_EQ(Py_REFCNT(v2), v2_initial, "v2 refcount restored");

    Py_DECREF(stack);
    Py_DECREF(v0);
    Py_DECREF(v1);
    Py_DECREF(v2);
    return 0;
}


static int test_stack_duplicate(void) {
    /*
      Test duplicate operation.
    */
    PyObject *stack = KosStack_New();
    TEST_ASSERT_NOT_NULL(stack, "create Stack");

    PyObject *v0 = PyLong_FromLong(TEST_VAL_1);
    PyObject *v1 = PyLong_FromLong(TEST_VAL_2);

    KosStack_Push(stack, v0);
    KosStack_Push(stack, v1);

    Py_ssize_t v1_before = Py_REFCNT(v1);

    /* Duplicate top element */
    int result = KosStack_Duplicate(stack, 1);
    TEST_ASSERT_EQ(result, 0, "duplicate succeeded");

    KosStackBlock *block = KosStack_BLOCK(stack);
    TEST_ASSERT_EQ(KosStackBlock_LEN(block), 3, "three elements");

    /* v1 refcount should be incremented */
    TEST_ASSERT_EQ(Py_REFCNT(v1), v1_before + 1, "v1 refcount incremented");

    Py_DECREF(stack);
    Py_DECREF(v0);
    Py_DECREF(v1);
    return 0;
}


static int test_stack_replace(void) {
    /*
      Test replace operation.
    */
    PyObject *stack = KosStack_New();
    TEST_ASSERT_NOT_NULL(stack, "create Stack");

    PyObject *v0 = PyLong_FromLong(TEST_VAL_1);
    PyObject *v1 = PyLong_FromLong(TEST_VAL_2);
    PyObject *new_v = PyLong_FromLong(TEST_VAL_3);

    KosStack_Push(stack, v0);
    KosStack_Push(stack, v1);

    Py_ssize_t v0_before = Py_REFCNT(v0);
    Py_ssize_t new_v_initial = Py_REFCNT(new_v);

    /* Replace index 0 */
    int result = KosStack_Replace(stack, 0, new_v);
    TEST_ASSERT_EQ(result, 0, "replace succeeded");

    /* Old value refcount decremented, new value incremented */
    TEST_ASSERT_EQ(Py_REFCNT(v0), v0_before - 1, "v0 refcount decremented");
    TEST_ASSERT_EQ(Py_REFCNT(new_v), new_v_initial + 1, "new_v refcount incremented");

    /* Verify replacement */
    PyObject *peeked = KosStack_PeekAt(stack, 0);
    TEST_ASSERT_SAME(peeked, new_v, "element replaced");

    Py_DECREF(stack);
    Py_DECREF(v0);
    Py_DECREF(v1);
    Py_DECREF(new_v);
    return 0;
}


/* === Block tests === */


static int test_stack_push_block(void) {
    /*
      Test pushing a new block.
    */
    PyObject *stack = KosStack_New();
    TEST_ASSERT_NOT_NULL(stack, "create Stack");

    PyObject *value = PyLong_FromLong(TEST_VAL_1);
    KosStack_Push(stack, value);

    KosStackBlock *old_block = KosStack_BLOCK(stack);

    /* Push new block */
    int result = KosStack_PushBlock(stack);
    TEST_ASSERT_EQ(result, 0, "push block succeeded");

    KosStackBlock *new_block = KosStack_BLOCK(stack);
    TEST_ASSERT_NOT_SAME(new_block, old_block, "new block is different");
    TEST_ASSERT_SAME(KosStackBlock_PARENT(new_block), old_block, "parent is old block");
    TEST_ASSERT_EQ(KosStackBlock_LEN(new_block), 0, "new block is empty");

    Py_DECREF(stack);
    Py_DECREF(value);
    return 0;
}


static int test_stack_pop_block(void) {
    /*
      Test popping a block with values.
    */
    PyObject *stack = KosStack_New();
    TEST_ASSERT_NOT_NULL(stack, "create Stack");

    PyObject *v0 = PyLong_FromLong(TEST_VAL_1);
    PyObject *v1 = PyLong_FromLong(TEST_VAL_2);
    PyObject *v2 = PyLong_FromLong(TEST_VAL_3);

    /* Push to root block */
    KosStack_Push(stack, v0);

    /* Push new block and add values */
    KosStack_PushBlock(stack);
    KosStack_Push(stack, v1);
    KosStack_Push(stack, v2);

    /* Pop block, keeping top value */
    int result = KosStack_PopBlock(stack, 1);
    TEST_ASSERT_EQ(result, 0, "pop block succeeded");

    /* Should now have 2 elements in root block */
    KosStackBlock *block = KosStack_BLOCK(stack);
    TEST_ASSERT_EQ(KosStackBlock_LEN(block), 2, "two elements");

    /* v2 should be on top */
    PyObject *peeked = KosStack_Peek(stack);
    TEST_ASSERT_SAME(peeked, v2, "v2 on top");

    Py_DECREF(stack);
    Py_DECREF(v0);
    Py_DECREF(v1);
    Py_DECREF(v2);
    return 0;
}


static int test_stack_drop_block(void) {
    /*
      Test dropping a block discards its contents.
    */
    PyObject *stack = KosStack_New();
    TEST_ASSERT_NOT_NULL(stack, "create Stack");

    PyObject *v0 = PyLong_FromLong(TEST_VAL_1);
    PyObject *v1 = PyLong_FromLong(TEST_VAL_2);

    Py_ssize_t v1_initial = Py_REFCNT(v1);

    /* Push to root block */
    KosStack_Push(stack, v0);

    /* Push new block and add value */
    KosStack_PushBlock(stack);
    KosStack_Push(stack, v1);
    TEST_ASSERT_EQ(Py_REFCNT(v1), v1_initial + 1, "v1 refcount incremented");

    /* Drop block - should release v1 */
    int result = KosStack_DropBlock(stack);
    TEST_ASSERT_EQ(result, 0, "drop block succeeded");
    TEST_ASSERT_EQ(Py_REFCNT(v1), v1_initial, "v1 refcount restored");

    /* Root block should still have v0 */
    KosStackBlock *block = KosStack_BLOCK(stack);
    TEST_ASSERT_EQ(KosStackBlock_LEN(block), 1, "one element");

    PyObject *peeked = KosStack_Peek(stack);
    TEST_ASSERT_SAME(peeked, v0, "v0 still there");

    Py_DECREF(stack);
    Py_DECREF(v0);
    Py_DECREF(v1);
    return 0;
}


static int test_stack_pull(void) {
    /*
      Test pulling values from parent block.
    */
    PyObject *stack = KosStack_New();
    TEST_ASSERT_NOT_NULL(stack, "create Stack");

    PyObject *v0 = PyLong_FromLong(TEST_VAL_1);
    PyObject *v1 = PyLong_FromLong(TEST_VAL_2);

    /* Push to root block */
    KosStack_Push(stack, v0);
    KosStack_Push(stack, v1);

    Py_ssize_t v1_before = Py_REFCNT(v1);

    /* Push new block */
    KosStack_PushBlock(stack);

    /* Pull top value from parent */
    int result = KosStack_Pull(stack, 1);
    TEST_ASSERT_EQ(result, 0, "pull succeeded");

    /* v1 refcount should be incremented (copied, not moved) */
    TEST_ASSERT_EQ(Py_REFCNT(v1), v1_before + 1, "v1 refcount incremented");

    /* Current block should have v1 */
    PyObject *peeked = KosStack_Peek(stack);
    TEST_ASSERT_SAME(peeked, v1, "pulled v1");

    Py_DECREF(stack);
    Py_DECREF(v0);
    Py_DECREF(v1);
    return 0;
}


static int test_stack_clear(void) {
    /*
      Test clearing all blocks.
    */
    PyObject *stack = KosStack_New();
    TEST_ASSERT_NOT_NULL(stack, "create Stack");

    PyObject *v0 = PyLong_FromLong(TEST_VAL_1);
    PyObject *v1 = PyLong_FromLong(TEST_VAL_2);
    PyObject *v2 = PyLong_FromLong(TEST_VAL_3);

    Py_ssize_t v0_initial = Py_REFCNT(v0);
    Py_ssize_t v1_initial = Py_REFCNT(v1);
    Py_ssize_t v2_initial = Py_REFCNT(v2);

    /* Build nested structure */
    KosStack_Push(stack, v0);
    KosStack_PushBlock(stack);
    KosStack_Push(stack, v1);
    KosStack_PushBlock(stack);
    KosStack_Push(stack, v2);

    /* Clear all */
    int result = KosStack_Clear(stack);
    TEST_ASSERT_EQ(result, 0, "clear succeeded");

    /* All refcounts should be restored */
    TEST_ASSERT_EQ(Py_REFCNT(v0), v0_initial, "v0 refcount restored");
    TEST_ASSERT_EQ(Py_REFCNT(v1), v1_initial, "v1 refcount restored");
    TEST_ASSERT_EQ(Py_REFCNT(v2), v2_initial, "v2 refcount restored");

    /* Should have empty root block */
    KosStackBlock *block = KosStack_BLOCK(stack);
    TEST_ASSERT_NOT_NULL(block, "has root block");
    TEST_ASSERT_NULL(KosStackBlock_PARENT(block), "no parent");
    TEST_ASSERT_EQ(KosStackBlock_LEN(block), 0, "block is empty");

    Py_DECREF(stack);
    Py_DECREF(v0);
    Py_DECREF(v1);
    Py_DECREF(v2);
    return 0;
}


static int test_stack_pop_tail(void) {
    /*
      Test pop_tail returns tuple of elements.
    */
    PyObject *stack = KosStack_New();
    TEST_ASSERT_NOT_NULL(stack, "create Stack");

    PyObject *v0 = PyLong_FromLong(TEST_VAL_1);
    PyObject *v1 = PyLong_FromLong(TEST_VAL_2);
    PyObject *v2 = PyLong_FromLong(TEST_VAL_3);

    KosStack_Push(stack, v0);
    KosStack_Push(stack, v1);
    KosStack_Push(stack, v2);

    /* Pop last 2 elements */
    PyObject *tail = KosStack_PopTail(stack, 2);
    TEST_ASSERT_NOT_NULL(tail, "pop_tail succeeded");
    TEST_ASSERT(PyTuple_CheckExact(tail), "returns tuple");
    TEST_ASSERT_EQ(PyTuple_GET_SIZE(tail), 2, "tuple has 2 elements");

    /* Elements should be in order (bottom to top) */
    TEST_ASSERT_SAME(PyTuple_GET_ITEM(tail, 0), v1, "first is v1");
    TEST_ASSERT_SAME(PyTuple_GET_ITEM(tail, 1), v2, "second is v2");

    Py_DECREF(tail);
    Py_DECREF(stack);
    Py_DECREF(v0);
    Py_DECREF(v1);
    Py_DECREF(v2);
    return 0;
}


static int test_stack_extend(void) {
    /*
      Test extend operation.
    */
    PyObject *stack = KosStack_New();
    TEST_ASSERT_NOT_NULL(stack, "create Stack");

    PyObject *seq = PyList_New(3);
    PyList_SET_ITEM(seq, 0, PyLong_FromLong(TEST_VAL_1));
    PyList_SET_ITEM(seq, 1, PyLong_FromLong(TEST_VAL_2));
    PyList_SET_ITEM(seq, 2, PyLong_FromLong(TEST_VAL_3));

    int result = KosStack_Extend(stack, seq, 0);
    TEST_ASSERT_EQ(result, 0, "extend succeeded");

    KosStackBlock *block = KosStack_BLOCK(stack);
    TEST_ASSERT_EQ(KosStackBlock_LEN(block), 3, "three elements");

    Py_DECREF(seq);
    Py_DECREF(stack);
    return 0;
}


/* === deep copy tests === */


static int test_stack_copy_independent(void) {
    /*
      Test that KosStack_Copy creates an independent deep copy.
      Both stacks have same values but different block pointers.
    */
    PyObject *stack1 = KosStack_New();
    TEST_ASSERT_NOT_NULL(stack1, "create Stack 1");

    PyObject *v0 = PyLong_FromLong(TEST_VAL_1);
    PyObject *v1 = PyLong_FromLong(TEST_VAL_2);

    KosStack_Push(stack1, v0);
    KosStack_Push(stack1, v1);

    KosStackBlock *block1 = KosStack_BLOCK(stack1);

    /* Copy the stack */
    PyObject *stack2 = KosStack_Copy(stack1);
    TEST_ASSERT_NOT_NULL(stack2, "copy Stack succeeded");

    /* Blocks should differ (deep copy) */
    KosStackBlock *block2 = KosStack_BLOCK(stack2);
    TEST_ASSERT_NOT_SAME(block1, block2, "blocks are independent");

    /* Peek should return same values */
    PyObject *p1 = KosStack_PeekAt(stack1, 0);
    PyObject *p2 = KosStack_PeekAt(stack2, 0);
    TEST_ASSERT_SAME(p1, p2, "peek returns same objects");

    Py_DECREF(stack1);
    Py_DECREF(stack2);
    Py_DECREF(v0);
    Py_DECREF(v1);
    return 0;
}


static int test_stack_copy_independent_modify_push(void) {
    /*
      Test that copy is independent: push on one does not affect the other.
    */
    PyObject *stack1 = KosStack_New();
    TEST_ASSERT_NOT_NULL(stack1, "create Stack 1");

    PyObject *v0 = PyLong_FromLong(TEST_VAL_1);
    KosStack_Push(stack1, v0);

    /* Copy the stack */
    PyObject *stack2 = KosStack_Copy(stack1);
    TEST_ASSERT_NOT_NULL(stack2, "copy Stack succeeded");

    TEST_ASSERT_NOT_SAME(KosStack_BLOCK(stack1), KosStack_BLOCK(stack2),
                         "copy has independent blocks");

    /* Push to stack1 */
    PyObject *v1 = PyLong_FromLong(TEST_VAL_2);
    int result = KosStack_Push(stack1, v1);
    TEST_ASSERT_EQ(result, 0, "push succeeded");

    /* stack1 has 2 elements, stack2 unchanged with 1 */
    TEST_ASSERT_EQ(KosStackBlock_LEN(KosStack_BLOCK(stack1)), 2,
                   "stack1 has 2 elements");
    TEST_ASSERT_EQ(KosStackBlock_LEN(KosStack_BLOCK(stack2)), 1,
                   "stack2 unchanged with 1 element");

    Py_DECREF(stack1);
    Py_DECREF(stack2);
    Py_DECREF(v0);
    Py_DECREF(v1);
    return 0;
}


static int test_stack_copy_independent_modify_pop(void) {
    /*
      Test that copy is independent: pop from one does not affect the other.
    */
    PyObject *stack1 = KosStack_New();
    TEST_ASSERT_NOT_NULL(stack1, "create Stack 1");

    PyObject *v0 = PyLong_FromLong(TEST_VAL_1);
    PyObject *v1 = PyLong_FromLong(TEST_VAL_2);
    KosStack_Push(stack1, v0);
    KosStack_Push(stack1, v1);

    /* Copy the stack */
    PyObject *stack2 = KosStack_Copy(stack1);
    TEST_ASSERT_NOT_NULL(stack2, "copy Stack succeeded");

    /* Pop from stack1 */
    PyObject *popped = KosStack_Pop(stack1);
    TEST_ASSERT_NOT_NULL(popped, "pop succeeded");
    TEST_ASSERT_SAME(popped, v1, "popped correct value");

    /* Lengths differ */
    TEST_ASSERT_EQ(KosStackBlock_LEN(KosStack_BLOCK(stack1)), 1,
                   "stack1 has 1 element");
    TEST_ASSERT_EQ(KosStackBlock_LEN(KosStack_BLOCK(stack2)), 2,
                   "stack2 unchanged with 2 elements");

    Py_DECREF(popped);
    Py_DECREF(stack1);
    Py_DECREF(stack2);
    Py_DECREF(v0);
    Py_DECREF(v1);
    return 0;
}


static int test_stack_copy_independent_modify_drop(void) {
    /*
      Test that copy is independent: drop on one does not affect the other.
    */
    PyObject *stack1 = KosStack_New();
    TEST_ASSERT_NOT_NULL(stack1, "create Stack 1");

    PyObject *v0 = PyLong_FromLong(TEST_VAL_1);
    PyObject *v1 = PyLong_FromLong(TEST_VAL_2);
    KosStack_Push(stack1, v0);
    KosStack_Push(stack1, v1);

    /* Copy the stack */
    PyObject *stack2 = KosStack_Copy(stack1);
    TEST_ASSERT_NOT_NULL(stack2, "copy Stack succeeded");

    /* Drop from stack1 */
    int result = KosStack_Drop(stack1, 1);
    TEST_ASSERT_EQ(result, 0, "drop succeeded");

    /* stack1 has 1 element, stack2 unchanged with 2 */
    TEST_ASSERT_EQ(KosStackBlock_LEN(KosStack_BLOCK(stack2)), 2, "stack2 still has 2");

    Py_DECREF(stack1);
    Py_DECREF(stack2);
    Py_DECREF(v0);
    Py_DECREF(v1);
    return 0;
}


static int test_stack_copy_independent_modify_replace(void) {
    /*
      Test that copy is independent: replace on one does not affect the other.
    */
    PyObject *stack1 = KosStack_New();
    TEST_ASSERT_NOT_NULL(stack1, "create Stack 1");

    PyObject *v0 = PyLong_FromLong(TEST_VAL_1);
    PyObject *new_val = PyLong_FromLong(TEST_VAL_3);
    KosStack_Push(stack1, v0);

    /* Copy the stack */
    PyObject *stack2 = KosStack_Copy(stack1);
    TEST_ASSERT_NOT_NULL(stack2, "copy Stack succeeded");

    /* Replace in stack1 */
    int result = KosStack_Replace(stack1, 0, new_val);
    TEST_ASSERT_EQ(result, 0, "replace succeeded");

    /* Values should differ; stacks are independent */
    PyObject *p1 = KosStack_PeekAt(stack1, 0);
    PyObject *p2 = KosStack_PeekAt(stack2, 0);
    TEST_ASSERT_SAME(p1, new_val, "stack1 has new value");
    TEST_ASSERT_SAME(p2, v0, "stack2 has original value");

    Py_DECREF(stack1);
    Py_DECREF(stack2);
    Py_DECREF(v0);
    Py_DECREF(new_val);
    return 0;
}


static int test_stack_mutate_in_place(void) {
    /*
      Test that operations on a sole-owner stack mutate in place.
    */
    PyObject *stack = KosStack_New();
    TEST_ASSERT_NOT_NULL(stack, "create Stack");

    PyObject *v0 = PyLong_FromLong(TEST_VAL_1);
    KosStack_Push(stack, v0);

    /* Push again - mutation in place */
    PyObject *v1 = PyLong_FromLong(TEST_VAL_2);
    KosStack_Push(stack, v1);

    TEST_ASSERT_EQ(KosStackBlock_LEN(KosStack_BLOCK(stack)), 2, "has 2 elements");

    Py_DECREF(stack);
    Py_DECREF(v0);
    Py_DECREF(v1);
    return 0;
}


static int test_stack_copy_block_chain(void) {
    /*
      Test copy with nested blocks - copy gets independent block chain.
    */
    PyObject *stack1 = KosStack_New();
    TEST_ASSERT_NOT_NULL(stack1, "create Stack 1");

    PyObject *v0 = PyLong_FromLong(TEST_VAL_1);
    PyObject *v1 = PyLong_FromLong(TEST_VAL_2);

    /* Build stack with 2 blocks */
    KosStack_Push(stack1, v0);
    KosStack_PushBlock(stack1);
    KosStack_Push(stack1, v1);

    KosStackBlock *top1 = KosStack_BLOCK(stack1);
    KosStackBlock *root1 = KosStackBlock_PARENT(top1);
    TEST_ASSERT_NOT_NULL(root1, "has parent block");

    /* Copy the stack */
    PyObject *stack2 = KosStack_Copy(stack1);
    TEST_ASSERT_NOT_NULL(stack2, "copy succeeded");

    KosStackBlock *top2 = KosStack_BLOCK(stack2);
    KosStackBlock *root2 = KosStackBlock_PARENT(top2);

    /* Copy has independent blocks */
    TEST_ASSERT_NOT_SAME(top1, top2, "top blocks independent");
    TEST_ASSERT_NOT_SAME(root1, root2, "root blocks independent");

    Py_DECREF(stack1);
    Py_DECREF(stack2);
    Py_DECREF(v0);
    Py_DECREF(v1);
    return 0;
}


static int test_stack_copy_independent_pop_block(void) {
    /*
      Test that copy is independent: pop_block on one does not affect the other.
    */
    PyObject *stack1 = KosStack_New();
    TEST_ASSERT_NOT_NULL(stack1, "create Stack 1");

    PyObject *v0 = PyLong_FromLong(TEST_VAL_1);
    PyObject *v1 = PyLong_FromLong(TEST_VAL_2);

    /* Build structure: root has v0, child has v1 */
    KosStack_Push(stack1, v0);
    KosStack_PushBlock(stack1);
    KosStack_Push(stack1, v1);

    /* Copy the stack */
    PyObject *stack2 = KosStack_Copy(stack1);
    TEST_ASSERT_NOT_NULL(stack2, "copy succeeded");

    /* Pop block on stack1, moving v1 to parent */
    int result = KosStack_PopBlock(stack1, 1);
    TEST_ASSERT_EQ(result, 0, "pop block succeeded");

    /* stack1 now has single block with 2 elements; stack2 unchanged */
    KosStackBlock *new_root1 = KosStack_BLOCK(stack1);

    /* stack2 unchanged (DeepCopy may flatten block structure; values preserved) */
    size_t stack2_len = 0;
    for (KosStackBlock *b = KosStack_BLOCK(stack2); b; b = KosStackBlock_PARENT(b))
      stack2_len += KosStackBlock_LEN(b);
    TEST_ASSERT_EQ(stack2_len, 2, "stack2 still has 2 elements");

    /* stack1 now has 2 elements in single block */
    TEST_ASSERT_EQ(KosStackBlock_LEN(new_root1), 2, "stack1 root has 2 elements");
    TEST_ASSERT_NULL(KosStackBlock_PARENT(new_root1), "stack1 is now single block");

    Py_DECREF(stack1);
    Py_DECREF(stack2);
    Py_DECREF(v0);
    Py_DECREF(v1);
    return 0;
}


static int test_stack_copy_multiple_independent(void) {
    /*
      Test that multiple copies are each independent.
    */
    PyObject *stack1 = KosStack_New();
    TEST_ASSERT_NOT_NULL(stack1, "create Stack 1");

    PyObject *v0 = PyLong_FromLong(TEST_VAL_1);
    KosStack_Push(stack1, v0);

    /* Make 3 copies */
    PyObject *stack2 = KosStack_Copy(stack1);
    PyObject *stack3 = KosStack_Copy(stack1);
    PyObject *stack4 = KosStack_Copy(stack1);

    /* All have independent blocks */
    TEST_ASSERT_NOT_SAME(KosStack_BLOCK(stack1), KosStack_BLOCK(stack2),
                         "stack2 independent");
    TEST_ASSERT_NOT_SAME(KosStack_BLOCK(stack1), KosStack_BLOCK(stack3),
                         "stack3 independent");
    TEST_ASSERT_NOT_SAME(KosStack_BLOCK(stack1), KosStack_BLOCK(stack4),
                         "stack4 independent");

    /* Modify stack2 - only stack2 changes */
    PyObject *v1 = PyLong_FromLong(TEST_VAL_2);
    KosStack_Push(stack2, v1);

    TEST_ASSERT_EQ(KosStackBlock_LEN(KosStack_BLOCK(stack2)), 2,
                   "stack2 has 2 elements");
    TEST_ASSERT_EQ(KosStackBlock_LEN(KosStack_BLOCK(stack1)), 1,
                   "stack1 unchanged");

    Py_DECREF(stack1);
    Py_DECREF(stack2);
    Py_DECREF(stack3);
    Py_DECREF(stack4);
    Py_DECREF(v0);
    Py_DECREF(v1);
    return 0;
}


static int test_stack_block_deepcopy_direct(void) {
    /*
      Test KosStackBlock_DeepCopy directly.
    */
    PyObject *stack = KosStack_New();
    TEST_ASSERT_NOT_NULL(stack, "create Stack");

    PyObject *v0 = PyLong_FromLong(TEST_VAL_1);
    KosStack_Push(stack, v0);

    KosStackBlock *block = KosStack_BLOCK(stack);

    /* DeepCopy returns new chain */
    KosStackBlock *copy = KosStackBlock_DeepCopy(block);
    TEST_ASSERT_NOT_NULL(copy, "DeepCopy succeeded");
    TEST_ASSERT_NOT_SAME(copy, block, "DeepCopy returns new chain");

    TEST_ASSERT_EQ(KosStackBlock_LEN(copy), 1, "copy has same length");
    TEST_ASSERT_EQ(KosStackBlock_LEN(block), 1, "original unchanged");

    KosStackBlock_ReleaseChain(copy);
    Py_DECREF(stack);
    Py_DECREF(v0);
    return 0;
}


static int test_stack_copy_release_independent(void) {
    /*
      Test that destroying one copy does not affect the other.
    */
    PyObject *stack1 = KosStack_New();
    TEST_ASSERT_NOT_NULL(stack1, "create Stack 1");

    PyObject *v0 = PyLong_FromLong(TEST_VAL_1);
    Py_ssize_t v0_initial = Py_REFCNT(v0);

    KosStack_Push(stack1, v0);
    TEST_ASSERT_EQ(Py_REFCNT(v0), v0_initial + 1, "v0 refcount after push");

    /* Copy - v0 gets another ref from the deep copy */
    PyObject *stack2 = KosStack_Copy(stack1);
    TEST_ASSERT_EQ(Py_REFCNT(v0), v0_initial + 2, "v0 refcount after copy");

    /* Destroy stack1 - v0 ref from stack1 released */
    Py_DECREF(stack1);
    TEST_ASSERT_EQ(Py_REFCNT(v0), v0_initial + 1, "v0 refcount after stack1 freed");

    /* Destroy stack2 - v0 ref from stack2 released */
    Py_DECREF(stack2);
    TEST_ASSERT_EQ(Py_REFCNT(v0), v0_initial, "v0 refcount restored");

    Py_DECREF(v0);
    return 0;
}


/* === stress tests === */


static int test_stack_stress_push_pop(void) {
    /*
      Stress test: many push/pop cycles.
    */
    PyObject *stack = KosStack_New();
    TEST_ASSERT_NOT_NULL(stack, "create Stack");

    for (int cycle = 0; cycle < 10; cycle++) {
        /* Push 200 elements */
        for (int i = 0; i < 200; i++) {
            PyObject *val = PyLong_FromLong(10000 + i);
            int result = KosStack_Push(stack, val);
            Py_DECREF(val);
            TEST_ASSERT_EQ(result, 0, "push succeeded");
        }

        KosStackBlock *block = KosStack_BLOCK(stack);
        TEST_ASSERT_EQ(KosStackBlock_LEN(block), 200, "200 elements");

        /* Pop all */
        for (int i = 199; i >= 0; i--) {
            PyObject *val = KosStack_Pop(stack);
            TEST_ASSERT_NOT_NULL(val, "pop succeeded");
            TEST_ASSERT_EQ(PyLong_AsLong(val), 10000 + i, "correct value");
            Py_DECREF(val);
        }

        TEST_ASSERT_EQ(KosStackBlock_LEN(block), 0, "empty after pop");
    }

    Py_DECREF(stack);
    return 0;
}


static int test_stack_nested_blocks(void) {
    /*
      Test deeply nested blocks - push_block creates new empty block,
      so values pushed before push_block stay in parent.

      Structure after loop:
        - root: val0
        - block1: val1
        - ...
        - block19: val19
        - block20: empty (current)

      When we pop_block(0), we discard each empty or non-empty block.
      Only root's val0 survives since we never discard root.
    */
    PyObject *stack = KosStack_New();
    TEST_ASSERT_NOT_NULL(stack, "create Stack");

    /* Push 20 values to root block */
    for (int i = 0; i < 20; i++) {
        PyObject *val = PyLong_FromLong(10000 + i);
        KosStack_Push(stack, val);
        Py_DECREF(val);
    }

    /* Now create 20 nested empty blocks */
    for (int i = 0; i < 20; i++) {
        KosStack_PushBlock(stack);
    }

    /* Count depth */
    int depth = 0;
    KosStackBlock *b = KosStack_BLOCK(stack);
    while (b) {
        depth++;
        b = KosStackBlock_PARENT(b);
    }
    TEST_ASSERT_EQ(depth, 21, "21 blocks (root + 20 empty)");

    /* Pop all empty blocks */
    for (int i = 0; i < 20; i++) {
        int result = KosStack_PopBlock(stack, 0);
        TEST_ASSERT_EQ(result, 0, "pop block succeeded");
    }

    /* Should have root block with all 20 values */
    KosStackBlock *root = KosStack_BLOCK(stack);
    TEST_ASSERT_EQ(KosStackBlock_LEN(root), 20, "20 elements in root");

    Py_DECREF(stack);
    return 0;
}


/* === test suite runner === */


int run_stack_tests(void) {
    BEGIN_TEST_SUITE("Stack Memory Tests");

    /* Basic operations */
    RUN_TEST(test_stack_create);
    RUN_TEST(test_stack_push_pop);
    RUN_TEST(test_stack_push_stolen);
    RUN_TEST(test_stack_peek);
    RUN_TEST(test_stack_peek_at);
    RUN_TEST(test_stack_drop);
    RUN_TEST(test_stack_duplicate);
    RUN_TEST(test_stack_replace);

    /* Block operations */
    RUN_TEST(test_stack_push_block);
    RUN_TEST(test_stack_pop_block);
    RUN_TEST(test_stack_drop_block);
    RUN_TEST(test_stack_pull);
    RUN_TEST(test_stack_clear);
    RUN_TEST(test_stack_pop_tail);
    RUN_TEST(test_stack_extend);

    /* Deep copy tests */
    RUN_TEST(test_stack_copy_independent);
    RUN_TEST(test_stack_copy_independent_modify_push);
    RUN_TEST(test_stack_copy_independent_modify_pop);
    RUN_TEST(test_stack_copy_independent_modify_drop);
    RUN_TEST(test_stack_copy_independent_modify_replace);
    RUN_TEST(test_stack_mutate_in_place);
    RUN_TEST(test_stack_copy_block_chain);
    RUN_TEST(test_stack_copy_independent_pop_block);
    RUN_TEST(test_stack_copy_multiple_independent);
    RUN_TEST(test_stack_block_deepcopy_direct);
    RUN_TEST(test_stack_copy_release_independent);

    /* Stress tests */
    RUN_TEST(test_stack_stress_push_pop);
    RUN_TEST(test_stack_nested_blocks);

    return 0;
}


/* The end. */
