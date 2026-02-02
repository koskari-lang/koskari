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

   author: Christopher O'Brien <obriencj@gmail.com>
   license: GPL v.3
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
      we call the initialization functions directly rather than importing
      the Python module (which would load a second copy of the code).
    */

    /* Create a dummy module for initialization */
    static struct PyModuleDef testmod = {
        .m_base = PyModuleDef_HEAD_INIT,
        .m_name = "_koskari_test",
        .m_doc = "Test module for koskari types",
        .m_size = -1,
    };

    kos_lib_module = PyModule_Create(&testmod);
    if (!kos_lib_module) {
        fprintf(stderr, "Failed to create test module\n");
        if (PyErr_Occurred()) {
            PyErr_Print();
        }
        return -1;
    }

    /* Initialize compat first */
    if (koskari_compat_init(kos_lib_module)) {
        fprintf(stderr, "Failed to initialize compat\n");
        return -1;
    }

    /* Initialize atom types (symbol, keyword) */
    if (koskari_atom_init(kos_lib_module)) {
        fprintf(stderr, "Failed to initialize atom types\n");
        return -1;
    }

    /* Initialize pair types (pair, nil) */
    if (koskari_pair_init(kos_lib_module)) {
        fprintf(stderr, "Failed to initialize pair types\n");
        return -1;
    }

    /* Initialize ref types */
    if (koskari_ref_init(kos_lib_module)) {
        fprintf(stderr, "Failed to initialize ref types\n");
        return -1;
    }

    /* Initialize namespace types */
    if (koskari_namespace_init(kos_lib_module)) {
        fprintf(stderr, "Failed to initialize namespace types\n");
        return -1;
    }

    /* Initialize bytecode types */
    if (koskari_bytecode_init(kos_lib_module)) {
        fprintf(stderr, "Failed to initialize bytecode types\n");
        return -1;
    }

    /* Initialize formals types */
    if (koskari_formals_init(kos_lib_module)) {
        fprintf(stderr, "Failed to initialize formals types\n");
        return -1;
    }

    /* Initialize values types */
    if (koskari_values_init(kos_lib_module)) {
        fprintf(stderr, "Failed to initialize values types\n");
        return -1;
    }

    /* Initialize stack types */
    if (koskari_stack_init(kos_lib_module)) {
        fprintf(stderr, "Failed to initialize stack types\n");
        return -1;
    }

    return 0;
}


static void cleanup(void) {
    Py_XDECREF(kos_lib_module);
    kos_lib_module = NULL;

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
        return 1;
    }

    if (initialize_types() != 0) {
        cleanup();
        return 1;
    }

    /* Run all test suites */
    run_atom_tests();
    run_pair_tests();
    run_namespace_tests();
    run_bytecode_tests();
    run_formals_tests();
    run_values_tests();
    run_stack_tests();

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
