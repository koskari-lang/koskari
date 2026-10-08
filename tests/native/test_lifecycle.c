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
   Module lifecycle: the statics a clear empties come back on the next
   init over a fresh module object, which is the sequence a re-import
   after a collection performs.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "test_harness.h"
#include "../../koskari/lib/lib.h"


static struct PyModuleDef lifecycle_mod = {
  .m_base = PyModuleDef_HEAD_INIT,
  .m_name = "_koskari_lifecycle",
  .m_doc = "A module to init, clear and init again",
  .m_size = 0,
};


static int test_lifecycle_init_clear_init(void) {
  PyObject *mod;
  PyObject *name;
  PyObject *before;
  PyObject *after;
  PyObject *again;

  mod = PyModule_Create(&lifecycle_mod);
  TEST_ASSERT_NOT_NULL(mod, "lifecycle module");
  TEST_ASSERT_EQ(koskari_lib_init(mod), 0, "init over live statics");
  TEST_ASSERT_NOT_NULL(PyDict_GetItemString(PyModule_GetDict(mod), "symbol"),
                       "init filled the module dict");

  name = PyUnicode_FromString("lifecycle");
  TEST_ASSERT_NOT_NULL(name, "name");
  before = KosSymbol_FromString(name);
  TEST_ASSERT_NOT_NULL(before, "a symbol before the clear");

  TEST_ASSERT_EQ(koskari_lib_clear(mod), 0, "clear");
  TEST_ASSERT_NULL(PyDict_GetItemString(PyModule_GetDict(mod), "symbol"),
                   "clear emptied the module dict");
  Py_DECREF(mod);

  /* a re-import makes a new module object and runs the init over it */
  mod = PyModule_Create(&lifecycle_mod);
  TEST_ASSERT_NOT_NULL(mod, "a fresh module after the clear");
  if (koskari_lib_init(mod) < 0) {
    PyErr_Print();
    TEST_ASSERT(0, "init after the clear");
  }
  TEST_ASSERT_NOT_NULL(PyDict_GetItemString(PyModule_GetDict(mod), "symbol"),
                       "the second init filled the fresh module dict");

  after = KosSymbol_FromString(name);
  TEST_ASSERT_NOT_NULL(after, "a symbol after the second init");
  again = KosSymbol_FromString(name);
  TEST_ASSERT_NOT_NULL(again, "the same symbol once more");
  TEST_ASSERT_SAME(after, again, "the rebuilt table interns");
  TEST_ASSERT_NOT_SAME(before, after,
                       "the symbol from the old table is not the new one");

  /* the old atom's dealloc leaves the new table's entry in place */
  Py_DECREF(before);
  TEST_ASSERT_NULL(PyErr_Occurred(), "no error from the old atom's dealloc");
  Py_DECREF(again);
  again = KosSymbol_FromString(name);
  TEST_ASSERT_NOT_NULL(again, "the symbol after the old one died");
  TEST_ASSERT_SAME(after, again, "the new table still interns the name");

  Py_DECREF(again);
  Py_DECREF(after);
  Py_DECREF(name);
  Py_DECREF(mod);
  return 0;
}


int run_lifecycle_tests(void) {
  BEGIN_TEST_SUITE("Module lifecycle");
  RUN_TEST(test_lifecycle_init_clear_init);
  return 0;
}


// The end.
