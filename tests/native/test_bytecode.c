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
   tests/native/test_bytecode.c

   Native tests for KosCode bytecode type.

   Tests instruction processing, constant pool, position table
   delta encoding, and read-only memory protection.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
   :ai-assistant: Claude Opus 4.5 via Cursor
*/


#include "test_harness.h"
#include "../../koskari/lib/lib.h"


/* === Helper to create namespace spec === */


static PyObject *create_test_nspec(void) {

    PyObject *name = PyUnicode_FromString("test");
    PyObject *locals = PyTuple_New(0);
    PyObject *closures = PyTuple_New(0);

    PyObject *nspec = KosNamespaceSpec_New(name, locals, closures);

    Py_DECREF(name);
    Py_DECREF(locals);
    Py_DECREF(closures);

    return nspec;
}


/* === Helper to create instruction list === */


static PyObject *make_instr(int op, PyObject *arg) {

    PyObject *pair = PyTuple_New(2);
    PyTuple_SET_ITEM(pair, 0, PyLong_FromLong(op));
    Py_INCREF(arg);
    PyTuple_SET_ITEM(pair, 1, arg);
    return pair;
}


static PyObject *make_instr_int(int op, int arg) {

    PyObject *pair = PyTuple_New(2);
    PyTuple_SET_ITEM(pair, 0, PyLong_FromLong(op));
    PyTuple_SET_ITEM(pair, 1, PyLong_FromLong(arg));
    return pair;
}


static PyObject *make_position(int line, int col) {

    PyObject *pos = PyTuple_New(2);
    PyTuple_SET_ITEM(pos, 0, PyLong_FromLong(line));
    PyTuple_SET_ITEM(pos, 1, PyLong_FromLong(col));
    return pos;
}


/* === Code tests === */


static int test_code_create_empty_rejected(void) {
    /*
      Test that Code with empty instructions is rejected.
      All code must have at least one instruction (e.g. RETURN to return None).
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *instr = PyList_New(0);
    TEST_ASSERT_NOT_NULL(instr, "create empty instr list");

    PyObject *name = PyUnicode_FromString("test_func");
    PyObject *filename = PyUnicode_FromString("test.ki");

    PyErr_Clear();
    PyObject *code = KosCode_New(nspec, instr, name, filename);
    TEST_ASSERT_NULL(code, "empty Code is rejected");
    TEST_ASSERT(PyErr_Occurred(), "exception set");
    PyErr_Clear();

    Py_DECREF(filename);
    Py_DECREF(name);
    Py_DECREF(instr);
    Py_DECREF(nspec);
    return 0;
}


static int test_code_create_minimal_return_none(void) {
    /*
      Test minimal valid Code: single RETURN 0 (return None).
      Code must have at least one instruction.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *instr = PyList_New(2);
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_CONST, Py_None));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_RETURN, 1));

    PyObject *code = KosCode_New(nspec, instr, NULL, NULL);
    TEST_ASSERT_NOT_NULL(code, "create minimal Code");
    TEST_ASSERT(KosCode_CheckExact(code), "code type check");
    TEST_ASSERT_EQ(KosCode_CODE_LEN(code), 2, "two instructions");
    TEST_ASSERT_EQ(KosCode_CPOOL_LEN(code), 1, "one constant (None)");
    TEST_ASSERT_EQ(KosCode_GET_OP(code, 0), KOS_OP_CONST, "first op is CONST");
    TEST_ASSERT_EQ(KosCode_GET_OP(code, 1), KOS_OP_RETURN, "second op is RETURN");

    Py_DECREF(code);
    Py_DECREF(instr);
    Py_DECREF(nspec);
    return 0;
}


static int test_code_with_const(void) {
    /*
      Test Code with CONST instruction adds to constant pool.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *const_val = PyLong_FromLong(42);

    PyObject *instr = PyList_New(2);
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_CONST, const_val));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_RETURN, 1));

    PyObject *code = KosCode_New(nspec, instr, NULL, NULL);
    TEST_ASSERT_NOT_NULL(code, "create Code");

    /* Verify code length */
    TEST_ASSERT_EQ(KosCode_CODE_LEN(code), 2, "two instructions");

    /* Verify constant pool */
    TEST_ASSERT_EQ(KosCode_CPOOL_LEN(code), 1, "one constant");

    PyObject *retrieved = KosCode_GetConstant(code, 0);
    TEST_ASSERT_NOT_NULL(retrieved, "get constant");
    TEST_ASSERT_SAME(retrieved, const_val, "constant matches");

    /* Verify instruction */
    TEST_ASSERT_EQ(KosCode_GET_OP(code, 0), KOS_OP_CONST, "first op is CONST");
    TEST_ASSERT_EQ(KosCode_GET_ARG(code, 0), 0, "CONST arg is index 0");

    Py_DECREF(code);
    Py_DECREF(const_val);
    Py_DECREF(instr);
    Py_DECREF(nspec);
    return 0;
}


static int test_code_const_dedup(void) {
    /*
      Test that duplicate constants are deduplicated in pool.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *const_val = PyLong_FromLong(999);

    PyObject *instr = PyList_New(3);
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_CONST, const_val));
    PyList_SET_ITEM(instr, 1, make_instr(KOS_OP_CONST, const_val));
    PyList_SET_ITEM(instr, 2, make_instr_int(KOS_OP_RETURN, 1));

    PyObject *code = KosCode_New(nspec, instr, NULL, NULL);
    TEST_ASSERT_NOT_NULL(code, "create Code");

    /* Both CONST instructions should reference same pool index */
    TEST_ASSERT_EQ(KosCode_CPOOL_LEN(code), 1, "only one constant (deduped)");
    TEST_ASSERT_EQ(KosCode_GET_ARG(code, 0), 0, "first CONST uses index 0");
    TEST_ASSERT_EQ(KosCode_GET_ARG(code, 1), 0, "second CONST uses index 0");

    Py_DECREF(code);
    Py_DECREF(const_val);
    Py_DECREF(instr);
    Py_DECREF(nspec);
    return 0;
}


static int test_code_position_delta(void) {
    /*
      Test that position table uses delta encoding - only records changes.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    /* Create instructions with position changes */
    PyObject *instr = PyList_New(6);
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_POSITION, make_position(1, 0)));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_CONST, 0));
    PyList_SET_ITEM(instr, 2, make_instr(KOS_OP_POSITION, make_position(1, 0)));  /* same - not recorded */
    PyList_SET_ITEM(instr, 3, make_instr_int(KOS_OP_CONST, 0));
    PyList_SET_ITEM(instr, 4, make_instr(KOS_OP_POSITION, make_position(2, 5)));  /* different - recorded */
    PyList_SET_ITEM(instr, 5, make_instr_int(KOS_OP_RETURN, 1));

    /* Need to add a constant for CONST ops to reference */
    PyObject *const_list = PyList_New(7);
    PyList_SET_ITEM(const_list, 0, make_instr(KOS_OP_POSITION, make_position(1, 0)));
    PyObject *const_val = PyLong_FromLong(100);
    PyList_SET_ITEM(const_list, 1, make_instr(KOS_OP_CONST, const_val));
    PyList_SET_ITEM(const_list, 2, make_instr(KOS_OP_POSITION, make_position(1, 0)));  /* same */
    PyList_SET_ITEM(const_list, 3, make_instr(KOS_OP_CONST, const_val));
    PyList_SET_ITEM(const_list, 4, make_instr(KOS_OP_POSITION, make_position(2, 5)));  /* different */
    PyList_SET_ITEM(const_list, 5, make_instr(KOS_OP_CONST, const_val));
    PyList_SET_ITEM(const_list, 6, make_instr_int(KOS_OP_RETURN, 1));

    PyObject *code = KosCode_New(nspec, const_list, NULL, NULL);
    TEST_ASSERT_NOT_NULL(code, "create Code");

    /* Position table should only have 2 entries (initial + change) */
    TEST_ASSERT_EQ(KosCode_POS_LEN(code), 2, "two position entries");

    /* Verify position lookup */
    uint16_t line, col;

    KosCode_GetPosition(code, 0, &line, &col);
    TEST_ASSERT_EQ(line, 1, "line at index 0");
    TEST_ASSERT_EQ(col, 0, "col at index 0");

    KosCode_GetPosition(code, 1, &line, &col);
    TEST_ASSERT_EQ(line, 1, "line at index 1");
    TEST_ASSERT_EQ(col, 0, "col at index 1");

    KosCode_GetPosition(code, 2, &line, &col);
    TEST_ASSERT_EQ(line, 2, "line at index 2");
    TEST_ASSERT_EQ(col, 5, "col at index 2");

    Py_DECREF(code);
    Py_DECREF(const_val);
    Py_DECREF(const_list);
    Py_DECREF(instr);
    Py_DECREF(nspec);
    return 0;
}


static int test_code_yields_flag(void) {
    /*
      Test that YIELD instruction sets the yields flag.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *instr = PyList_New(2);
    PyList_SET_ITEM(instr, 0, make_instr_int(KOS_OP_YIELD, 1));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_RETURN, 0));

    PyObject *code = KosCode_New(nspec, instr, NULL, NULL);
    TEST_ASSERT_NOT_NULL(code, "create Code");

    TEST_ASSERT(KosCode_YIELDS(code), "yields flag is set");

    Py_DECREF(code);
    Py_DECREF(instr);
    Py_DECREF(nspec);
    return 0;
}


static int test_code_no_yields_flag(void) {
    /*
      Test that Code without YIELD does not have yields flag.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *instr = PyList_New(1);
    PyList_SET_ITEM(instr, 0, make_instr_int(KOS_OP_RETURN, 0));

    PyObject *code = KosCode_New(nspec, instr, NULL, NULL);
    TEST_ASSERT_NOT_NULL(code, "create Code");

    TEST_ASSERT(!KosCode_YIELDS(code), "yields flag is not set");

    Py_DECREF(code);
    Py_DECREF(instr);
    Py_DECREF(nspec);
    return 0;
}


static int test_code_label_resolution(void) {
    /*
      Test that LABEL and JUMP instructions are resolved correctly.

      LABEL and NOOP are compile-time pseudo-ops that don't occupy
      slots in the output bytecode. The JUMP target points to the
      instruction index where the label was defined.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *label_name = PyUnicode_FromString("target");

    /* JUMP -> POP -> LABEL -> RETURN
       Output: JUMP(1) -> POP -> RETURN
       NOOP/LABEL don't occupy output slots */
    PyObject *instr = PyList_New(4);
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_JUMP, label_name));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_POP, 1));
    PyList_SET_ITEM(instr, 2, make_instr(KOS_OP_LABEL, label_name));
    PyList_SET_ITEM(instr, 3, make_instr_int(KOS_OP_RETURN, 0));

    PyObject *code = KosCode_New(nspec, instr, NULL, NULL);
    TEST_ASSERT_NOT_NULL(code, "create Code");

    /* LABEL doesn't occupy a slot, so we have 3 instructions */
    TEST_ASSERT_EQ(KosCode_CODE_LEN(code), 3, "three instructions");

    /* JUMP should point to index 2 (where RETURN is, after LABEL) */
    TEST_ASSERT_EQ(KosCode_GET_OP(code, 0), KOS_OP_JUMP, "first op is JUMP");
    TEST_ASSERT_EQ(KosCode_GET_ARG(code, 0), 2, "JUMP targets index 2");

    /* Verify instruction sequence */
    TEST_ASSERT_EQ(KosCode_GET_OP(code, 1), KOS_OP_POP, "second op is POP");
    TEST_ASSERT_EQ(KosCode_GET_OP(code, 2), KOS_OP_RETURN, "third op is RETURN");

    Py_DECREF(code);
    Py_DECREF(label_name);
    Py_DECREF(instr);
    Py_DECREF(nspec);
    return 0;
}


static int test_code_var_in_cpool(void) {
    /*
      Test that VAR instruction stores variable name in constant pool.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *var_name = PyUnicode_FromString("my_var");

    PyObject *instr = PyList_New(2);
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_VAR, var_name));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_RETURN, 1));

    PyObject *code = KosCode_New(nspec, instr, NULL, NULL);
    TEST_ASSERT_NOT_NULL(code, "create Code");

    /* Variable name should be in constant pool */
    TEST_ASSERT_EQ(KosCode_CPOOL_LEN(code), 1, "one constant (var name)");

    PyObject *stored_name = KosCode_GetConstant(code, 0);
    TEST_ASSERT_NOT_NULL(stored_name, "get constant");

    int cmp = PyUnicode_Compare(stored_name, var_name);
    TEST_ASSERT_EQ(cmp, 0, "stored name equals var name");

    Py_DECREF(code);
    Py_DECREF(var_name);
    Py_DECREF(instr);
    Py_DECREF(nspec);
    return 0;
}


static int test_code_opcode_style(void) {
    /*
      Test kos_opcode_style function.
    */
    TEST_ASSERT_EQ(kos_opcode_style(KOS_OP_NOOP), KOSTYLE_NOARG, "NOOP style");
    TEST_ASSERT_EQ(kos_opcode_style(KOS_OP_CONST), KOSTYLE_CONST, "CONST style");
    TEST_ASSERT_EQ(kos_opcode_style(KOS_OP_JUMP), KOSTYLE_LABEL, "JUMP style");
    TEST_ASSERT_EQ(kos_opcode_style(KOS_OP_VAR), KOSTYLE_VAR, "VAR style");
    TEST_ASSERT_EQ(kos_opcode_style(KOS_OP_FAST), KOSTYLE_FASTVAR, "FAST style");
    TEST_ASSERT_EQ(kos_opcode_style(KOS_OP_POP), KOSTYLE_COUNT, "POP style");
    TEST_ASSERT_EQ(kos_opcode_style(KOS_OP_LABEL), KOSTYLE_COMPILE, "LABEL style");

    return 0;
}


static int test_code_instruction_method(void) {
    /*
      Test GetInstruction accessor.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *instr = PyList_New(2);
    PyList_SET_ITEM(instr, 0, make_instr_int(KOS_OP_POP, 3));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_RETURN, 1));

    PyObject *code = KosCode_New(nspec, instr, NULL, NULL);
    TEST_ASSERT_NOT_NULL(code, "create Code");

    PyObject *tup = KosCode_GetInstruction(code, 0);
    TEST_ASSERT_NOT_NULL(tup, "get instruction");
    TEST_ASSERT(PyTuple_Check(tup), "result is tuple");
    TEST_ASSERT_EQ(PyTuple_GET_SIZE(tup), 2, "tuple has 2 elements");

    long op = PyLong_AsLong(PyTuple_GET_ITEM(tup, 0));
    long arg = PyLong_AsLong(PyTuple_GET_ITEM(tup, 1));
    TEST_ASSERT_EQ(op, KOS_OP_POP, "op is POP");
    TEST_ASSERT_EQ(arg, 3, "arg is 3");

    Py_DECREF(tup);
    Py_DECREF(code);
    Py_DECREF(instr);
    Py_DECREF(nspec);
    return 0;
}


static int test_code_extent_derivation(void) {
    /*
      Test that PUSH_TRY/POP_TRY produces extent and kos_extent_top returns it.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *label_h = PyUnicode_FromString("handler");
    PyObject *instr = PyList_New(6);
    PyList_SET_ITEM(instr, 0, make_instr_int(KOS_OP_CONST, 0));
    PyList_SET_ITEM(instr, 1, make_instr(KOS_OP_PUSH_TRY, label_h));
    PyList_SET_ITEM(instr, 2, make_instr_int(KOS_OP_CONST, 1));
    PyList_SET_ITEM(instr, 3, make_instr_int(KOS_OP_POP_TRY, 0));
    PyList_SET_ITEM(instr, 4, make_instr_int(KOS_OP_RETURN, 1));
    PyList_SET_ITEM(instr, 5, make_instr(KOS_OP_LABEL, label_h));

    PyObject *code = KosCode_New(nspec, instr, NULL, NULL);
    TEST_ASSERT_NOT_NULL(code, "create Code with TRY");

    TEST_ASSERT_EQ(KosCode_EXTENT_LEN(code), 1, "one extent");
    TEST_ASSERT_NOT_NULL(KosCode_EXTENTS(code), "extents table");
    TEST_ASSERT_NOT_NULL(KosCode_TOP_EXTENT_ID(code), "top_extent_id table");

    uint32_t top0 = kos_extent_top(code, 0);
    TEST_ASSERT_EQ(top0, KOS_EXTENT_NONE, "pc 0 before TRY body");
    uint32_t top1 = kos_extent_top(code, 1);
    TEST_ASSERT_EQ(top1, 0, "pc 1 at PUSH_TRY (start of extent)");
    uint32_t top2 = kos_extent_top(code, 2);
    TEST_ASSERT_EQ(top2, 0, "pc 2 inside TRY body");
    uint32_t top3 = kos_extent_top(code, 3);
    TEST_ASSERT_EQ(top3, KOS_EXTENT_NONE, "pc 3 at POP_TRY (outside block)");
    uint32_t top4 = kos_extent_top(code, 4);
    TEST_ASSERT_EQ(top4, KOS_EXTENT_NONE, "pc 4 after TRY");

    KosExtentDesc *e = kos_extent_desc(code, 0);
    TEST_ASSERT_NOT_NULL(e, "extent 0");
    TEST_ASSERT_EQ(e->kind, KOS_EXT_TRY, "kind is TRY");
    TEST_ASSERT_EQ(e->push_pc, 1, "push at 1");
    TEST_ASSERT_EQ(e->pop_pc, 3, "pop at 3");

    Py_DECREF(code);
    Py_DECREF(label_h);
    Py_DECREF(instr);
    Py_DECREF(nspec);
    return 0;
}


static int test_code_extent_find(void) {
    /*
      Test kos_extent_find finds TRY extent by kind.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *label_h = PyUnicode_FromString("handler");
    PyObject *instr = PyList_New(6);
    PyList_SET_ITEM(instr, 0, make_instr_int(KOS_OP_CONST, 0));
    PyList_SET_ITEM(instr, 1, make_instr(KOS_OP_PUSH_TRY, label_h));
    PyList_SET_ITEM(instr, 2, make_instr_int(KOS_OP_CONST, 1));
    PyList_SET_ITEM(instr, 3, make_instr_int(KOS_OP_POP_TRY, 0));
    PyList_SET_ITEM(instr, 4, make_instr_int(KOS_OP_RETURN, 1));
    PyList_SET_ITEM(instr, 5, make_instr(KOS_OP_LABEL, label_h));

    PyObject *code = KosCode_New(nspec, instr, NULL, NULL);
    TEST_ASSERT_NOT_NULL(code, "create Code");

    uint32_t eid = kos_extent_find(code, 2, KOS_EXT_TRY);
    TEST_ASSERT_EQ(eid, 0, "find TRY at pc 2");

    eid = kos_extent_find(code, 4, KOS_EXT_TRY);
    TEST_ASSERT_EQ(eid, KOS_EXTENT_NONE, "no TRY at pc 4");

    Py_DECREF(code);
    Py_DECREF(label_h);
    Py_DECREF(instr);
    Py_DECREF(nspec);
    return 0;
}


static int test_code_loc_lookup(void) {
    /*
      Test kos_loc_lookup API.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *pos = make_position(10, 5);
    PyObject *instr = PyList_New(3);
    PyList_SET_ITEM(instr, 0, make_instr(KOS_OP_POSITION, pos));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_CONST, 0));
    PyList_SET_ITEM(instr, 2, make_instr_int(KOS_OP_RETURN, 1));

    PyObject *code = KosCode_New(nspec, instr, NULL, NULL);
    TEST_ASSERT_NOT_NULL(code, "create Code");

    uint16_t line = 0, col = 0;
    int ret = kos_loc_lookup(code, 0, &line, &col);
    TEST_ASSERT_EQ(ret, 0, "lookup succeeds");
    TEST_ASSERT_EQ(line, 10, "line at pc 0");
    TEST_ASSERT_EQ(col, 5, "col at pc 0");

    Py_DECREF(code);
    Py_DECREF(pos);
    Py_DECREF(instr);
    Py_DECREF(nspec);
    return 0;
}


static int test_code_no_extent(void) {
    /*
      Code with no PUSH/POP blocks has no extents.
    */
    PyObject *nspec = create_test_nspec();
    TEST_ASSERT_NOT_NULL(nspec, "create nspec");

    PyObject *instr = PyList_New(2);
    PyList_SET_ITEM(instr, 0, make_instr_int(KOS_OP_CONST, 0));
    PyList_SET_ITEM(instr, 1, make_instr_int(KOS_OP_RETURN, 1));

    PyObject *code = KosCode_New(nspec, instr, NULL, NULL);
    TEST_ASSERT_NOT_NULL(code, "create Code");

    TEST_ASSERT_EQ(KosCode_EXTENT_LEN(code), 0, "no extents");
    TEST_ASSERT_NULL(KosCode_EXTENTS(code), "extents is NULL");
    TEST_ASSERT_NULL(KosCode_TOP_EXTENT_ID(code), "top_extent_id is NULL");

    uint32_t top = kos_extent_top(code, 0);
    TEST_ASSERT_EQ(top, KOS_EXTENT_NONE, "no extent at pc 0");

    Py_DECREF(code);
    Py_DECREF(instr);
    Py_DECREF(nspec);
    return 0;
}


/* === Test runner === */


int run_bytecode_tests(void) {

    BEGIN_TEST_SUITE("Bytecode Tests");

    RUN_TEST(test_code_create_empty_rejected);
    RUN_TEST(test_code_create_minimal_return_none);
    RUN_TEST(test_code_with_const);
    RUN_TEST(test_code_const_dedup);
    RUN_TEST(test_code_position_delta);
    RUN_TEST(test_code_yields_flag);
    RUN_TEST(test_code_no_yields_flag);
    RUN_TEST(test_code_label_resolution);
    RUN_TEST(test_code_var_in_cpool);
    RUN_TEST(test_code_opcode_style);
    RUN_TEST(test_code_instruction_method);
    RUN_TEST(test_code_extent_derivation);
    RUN_TEST(test_code_extent_find);
    RUN_TEST(test_code_loc_lookup);
    RUN_TEST(test_code_no_extent);

    return 0;
}


// The end.
