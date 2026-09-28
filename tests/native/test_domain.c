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
   Native tests for the per-thread record behind the ambient context
   stack: one OS thread swapping between two thread states keeps a
   separate active-context stack for each, and clearing one thread
   state from the other leaves the survivor answering.

   author: Christopher O'Brien <obriencj@gmail.com>
   license: GNU General Public License v3
*/


#include "test_harness.h"
#include "../../koskari/lib/lib.h"


static int test_domain_two_thread_states_on_one_os_thread(void) {
  PyThreadState *first = PyThreadState_Get();
  PyThreadState *second;
  KosContext *a;
  KosContext *b;

  second = PyThreadState_New(PyThreadState_GetInterpreter(first));
  TEST_ASSERT_NOT_NULL(second, "second thread state");

  a = KosContext_New(NULL);
  TEST_ASSERT_NOT_NULL(a, "context a");
  b = KosContext_New(NULL);
  TEST_ASSERT_NOT_NULL(b, "context b");

  kos_domain_push_scope(a);
  TEST_ASSERT_SAME(kos_ctx_current(), a, "a is current on the first state");

  PyThreadState_Swap(second);
  TEST_ASSERT_NULL(kos_ctx_current(), "the second state starts empty");
  kos_domain_push_scope(b);
  TEST_ASSERT_SAME(kos_ctx_current(), b, "b is current on the second state");

  PyThreadState_Swap(first);
  TEST_ASSERT_SAME(kos_ctx_current(), a,
                   "a is current again after the swap back");
  kos_domain_pop_scope(a);
  TEST_ASSERT_NULL(kos_ctx_current(), "the first state is empty after pop");

  PyThreadState_Swap(second);
  TEST_ASSERT_SAME(kos_ctx_current(), b,
                   "b is current again after the swap back");
  kos_domain_pop_scope(b);
  TEST_ASSERT_NULL(kos_ctx_current(), "the second state is empty after pop");

  /* clearing the second state from the first frees its record; the
     first state's record still answers */
  PyThreadState_Swap(first);
  PyThreadState_Clear(second);
  PyThreadState_Delete(second);

  kos_domain_push_scope(a);
  TEST_ASSERT_SAME(kos_ctx_current(), a,
                   "a is current after the other state is gone");
  kos_domain_pop_scope(a);
  TEST_ASSERT_NULL(kos_ctx_current(), "the first state is empty at the end");

  Py_DECREF(a);
  Py_DECREF(b);
  return 0;
}


int run_domain_tests(void) {
  BEGIN_TEST_SUITE("Domain");
  RUN_TEST(test_domain_two_thread_states_on_one_os_thread);
  return 0;
}


// The end.
