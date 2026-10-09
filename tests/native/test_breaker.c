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
   tests/native/test_breaker.c

   The eval breaker as the evaluator reads it through the compat
   layer: the word reads clear at rest, reads up once a signal is
   pending, drives the call core's install result, and reads clear
   again once the signal has been handled. A wrong struct prefix for
   the running Python fails here rather than in a signal test that
   happens to pass another way.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "test_harness.h"
#include "../../koskari/lib/lib.h"


static int test_breaker_reads_the_interpreter(void) {
  const volatile KosBreakerWord *w = koskari_eval_breaker_locate();
  TEST_ASSERT_NOT_NULL((void *) w, "breaker located for this thread");
  TEST_ASSERT(w != &koskari_breaker_always,
              "a mapped Python does not fall back to the always-up word");

  TEST_ASSERT_EQ((*w & KOS_BREAKER_MASK) != 0, 0, "breaker clear at rest");
  TEST_ASSERT_EQ(koskari_gil_drop_requested(), 0,
                 "no GIL drop requested at rest");

  KosContext *ctx = KosContext_New(NULL);
  TEST_ASSERT_NOT_NULL(ctx, "create context");
  KosEvalState es;
  KosEvalState_Init(&es, ctx, NULL);
  TEST_ASSERT_SAME((void *) KosEvalState_BREAKER(&es), (void *) w,
                   "Init caches the located word");
  TEST_ASSERT_EQ(KOS_EVAL_BREAKER_UP(&es) != 0, 0,
                 "cooperation test is clear at rest");
  TEST_ASSERT_EQ(kos_call_installed(&es).kind, KOS_RES_NORMAL,
                 "an install threads on when the breaker is clear");

  /* A simulated SIGINT: CPython's own tripwire raises the signal
     input of the breaker, as a real signal's C handler would. */
  PyErr_SetInterrupt();
  TEST_ASSERT_EQ((*w & KOS_BREAKER_MASK) != 0, 1,
                 "breaker up with a signal pending");
  TEST_ASSERT_EQ(KOS_EVAL_BREAKER_UP(&es) != 0, 1,
                 "cooperation test sees the pending signal");
  TEST_ASSERT_EQ(kos_call_installed(&es).kind, KOS_RES_POLL,
                 "an install polls before threading on with the breaker up");
  TEST_ASSERT_EQ(koskari_gil_drop_requested(), 0,
                 "a signal is not a GIL drop request");

  /* Handling the signal the way the trampoline's poll does: the
     default SIGINT handler raises KeyboardInterrupt, and a handler
     that raised leaves the input up so the interpreter re-checks; the
     next handling pass finds nothing tripped and clears it. */
  int rc = Py_MakePendingCalls();
  TEST_ASSERT_EQ(rc, -1, "the pending signal ran its handler");
  TEST_ASSERT(PyErr_ExceptionMatches(PyExc_KeyboardInterrupt),
              "the handler raised KeyboardInterrupt");
  PyErr_Clear();
  TEST_ASSERT_EQ(Py_MakePendingCalls(), 0, "nothing left to handle");
  TEST_ASSERT_EQ((*w & KOS_BREAKER_MASK) != 0, 0,
                 "breaker clear once the signal is handled");
  TEST_ASSERT_EQ(kos_call_installed(&es).kind, KOS_RES_NORMAL,
                 "installs thread on again");

  Py_DECREF(ctx);
  return 0;
}


int run_breaker_tests(void) {
  printf("\n=== eval breaker tests ===\n");

  RUN_TEST(test_breaker_reads_the_interpreter);
  return 0;
}


// The end.
