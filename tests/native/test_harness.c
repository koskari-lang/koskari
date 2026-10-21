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
   Test harness main entry point

   Initializes Python, loads the koskari.lib module, and runs all
   native memory tests.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "test_harness.h"
#include "../../koskari/lib/lib.h"


/* Global test context */
TestContext test_ctx = {0, 0, NULL};


/* Module reference kept for duration of tests */
static PyObject *kos_lib_module = NULL;


static int initialize_python(void) {
    /* Initialize the Python interpreter */
    Py_Initialize();

    if (!Py_IsInitialized()) {
        fprintf(stderr, "Failed to initialize Python interpreter\n");
        return -1;
    }

    return 0;
}


static int initialize_types(void) {
    /*
      Initialize our native types directly.

      Since we statically link the library code into the test harness,
      we run the library's own init over a module of our own rather than
      importing the Python module (which would load a second copy of the
      code). The init is the one PyInit_lib runs, so the suites exercise
      the statics as the module path leaves them.
    */

    static struct PyModuleDef testmod = {
        .m_base = PyModuleDef_HEAD_INIT,
        .m_name = "_koskari_test",
        .m_doc = "Test module for koskari types",
        .m_size = 0,
    };

    kos_lib_module = PyModule_Create(&testmod);
    if (!kos_lib_module) {
        fprintf(stderr, "Failed to create test module\n");
        if (PyErr_Occurred()) {
            PyErr_Print();
        }
        return -1;
    }

    if (koskari_lib_init(kos_lib_module)) {
        fprintf(stderr, "Failed to initialize koskari.lib\n");
        if (PyErr_Occurred()) {
            PyErr_Print();
        }
        return -1;
    }

    return 0;
}


/**
   Release module refs and finalize Python so Valgrind leak reports
   reflect our code rather than interpreter shutdown.
*/
static void cleanup(void) {
    if (kos_lib_module) {
        koskari_lib_clear(kos_lib_module);
        Py_XDECREF(kos_lib_module);
        kos_lib_module = NULL;
    }

    /* The last exception a suite's handler caught is still the thread's
       handled exception, holding its traceback and the frames injected
       into it; release it and collect what it kept alive, so the leak
       report at finalization shows our residue and not that state. */
    PyErr_Clear();
    PyErr_SetHandledException(NULL);
    PyGC_Collect();

    if (Py_IsInitialized()) {
        Py_Finalize();
    }
}


static void print_summary(void) {
    int total = test_ctx.passed + test_ctx.failed;

    printf("\n");
    printf("========================================\n");
    printf("Test Summary\n");
    printf("========================================\n");
    printf("  Passed: %d\n", test_ctx.passed);
    printf("  Failed: %d\n", test_ctx.failed);
    printf("  Total:  %d\n", total);
    printf("========================================\n");

    if (test_ctx.failed == 0) {
        printf("All tests passed!\n");
    } else {
        printf("FAILURES DETECTED\n");
    }
}


int main(int argc, char *argv[]) {
    int result = 0;

    (void)argc;  /* unused */
    (void)argv;  /* unused */

    printf("Koskari Native Memory Test Harness\n");
    printf("========================================\n");

    /* Initialize Python and our types */
    if (initialize_python() != 0) {
        cleanup();
        return 1;
    }

    if (initialize_types() != 0) {
        cleanup();
        return 1;
    }

    /* Run all test suites */
    run_arena_tests();
    run_atom_tests();
    run_pair_tests();
    run_matcher_tests();
    run_namespace_tests();
    run_bytecode_tests();
    run_formals_tests();
    run_values_tests();
    run_stack_tests();
    run_dynchain_tests();
    run_eval_tests();
    run_paramref_tests();
    run_property_tests();
    run_partial_tests();
    run_demand_tests();
    run_iter_request_tests();
    run_resume_tests();
    run_breaker_tests();
    run_domain_tests();
    /* last: it clears and rebuilds the statics every suite above used */
    run_lifecycle_tests();

    /* Future units:
    run_tco_tests();
    */

    /* Print summary */
    print_summary();

    /* Cleanup */
    cleanup();

    /* Return non-zero if any tests failed */
    result = (test_ctx.failed > 0) ? 1 : 0;

    return result;
}


/* The end. */
