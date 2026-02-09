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
   koskari/lib/bytecode.c

   Bytecode storage implementation for Koskari. Handles instruction
   processing, constant pool management, delta-encoded position table,
   and read-only memory protection.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
   :ai-assistant: Claude Opus 4.5 via Cursor
*/


/* Feature test macros must come before any includes */
#define _POSIX_C_SOURCE 200809L

#include <sys/mman.h>
#include <stdlib.h>
#include <unistd.h>

#include "lib.h"
#include "debug.h"


/* === Static string constants === */


static PyObject *_str_anon = NULL;


/* === Opcode styles table === */


#define OP(name, style)   \
  KOSTYLE_##style ,

static const KosOpStyle opcode_styles[] = {
#include "opcodes.incl"
};

#undef OP


KosOpStyle kos_opcode_style(KosOpCode op) {

  if (op <= KOS_OP_THEEND) {
    return opcode_styles[op];
  }
  return (KosOpStyle) -1;
}


/* === KosCode getset === */


static PyObject *code_get_yields(PyObject *self, void *_unused) {

  return PyBool_FromLong(KosCode_YIELDS(self));
}


static PyObject *code_get_cpool(PyObject *self, void *_unused) {

  uint32_t l = KosCode_CPOOL_LEN(self);
  PyObject *cpool = PyTuple_New(l);

  if (! cpool) {
    return NULL;
  }

  while (l--) {
    PyObject *val = KosCode_GET_CONST(self, l);
    Py_INCREF(val);
    PyTuple_SET_ITEM(cpool, l, val);
  }

  return cpool;
}


static PyGetSetDef code_getset[] = {

  { "yields", code_get_yields, NULL,
    "bool; whether this code will yield like a generator",
    NULL },

  { "constants_pool", code_get_cpool, NULL,
    "tuple; constants pool",
    NULL },

  { NULL, },
};


/* === KosCode members === */


static PyMemberDef code_members[] = {

  { "name", T_OBJECT,
    offsetof(KosCode, name), READONLY,
    "The name of this Code" },

  { "filename", T_OBJECT,
    offsetof(KosCode, filename), READONLY,
    "The source filename of this Code" },

  { "namespace_spec", T_OBJECT,
    offsetof(KosCode, nspec), READONLY,
    "The baseline NamespaceSpec required for this Code" },

  { NULL },
};


/* === KosCode methods === */


static PyObject *code_m_constant(PyObject *self, PyObject *index) {

  if (! PyLong_Check(index)) {
    PyErr_Format(PyExc_TypeError, "Code constants pool index must be"
                 " int, not %R", index);
    return NULL;
  }

  long i = PyLong_AsLong(index);
  if (i < 0) {
    PyErr_Format(PyExc_IndexError, "Code constants pool index out"
                 " of bounds: %ld", i);
    return NULL;
  }

  PyObject *res = KosCode_GetConstant(self, (uint32_t) i);
  Py_XINCREF(res);
  return res;
}


static PyObject *code_m_oparg(PyObject *self, PyObject *index) {

  if (! PyLong_Check(index)) {
    PyErr_Format(PyExc_TypeError, "Code oparg index must be int,"
                 " not %R", index);
    return NULL;
  }

  long i = PyLong_AsLong(index);
  uint32_t l = KosCode_CODE_LEN(self);

  if (i < 0 || (uint32_t) i >= l) {
    PyErr_Format(PyExc_IndexError, "Code oparg index out of"
                 " bounds: %ld", i);
    return NULL;
  }

  KosOp op = KosCode_CODE(self)[i];

  PyObject *p = PyTuple_New(2);
  if (! p) {
    return NULL;
  }

  PyTuple_SET_ITEM(p, 0, PyLong_FromLong((long) op.op));
  PyTuple_SET_ITEM(p, 1, PyLong_FromLong((long) op.arg));

  return p;
}


static PyObject *code_m_length(PyObject *self) {

  return PyLong_FromLong(KosCode_CODE_LEN(self));
}


static PyObject *code_m_position(PyObject *self, PyObject *index) {

  if (! PyLong_Check(index)) {
    PyErr_Format(PyExc_TypeError, "Code position index must be int,"
                 " not %R", index);
    return NULL;
  }

  long i = PyLong_AsLong(index);
  uint32_t l = KosCode_CODE_LEN(self);

  if (i < 0 || (uint32_t) i >= l) {
    PyErr_Format(PyExc_IndexError, "Code position index out of"
                 " bounds: %ld", i);
    return NULL;
  }

  uint16_t line = 0, col = 0;
  if (KosCode_GetPosition(self, (uint32_t) i, &line, &col)) {
    return NULL;
  }

  PyObject *p = PyTuple_New(2);
  if (! p) {
    return NULL;
  }

  PyTuple_SET_ITEM(p, 0, PyLong_FromLong((long) line));
  PyTuple_SET_ITEM(p, 1, PyLong_FromLong((long) col));

  return p;
}


static PyObject *code_m_expression(PyObject *self, PyObject *parent_ns) {

  if (! parent_ns) {
    PyErr_SetString(PyExc_TypeError, "Code.expression(parent_ns) requires"
                    " a parent namespace");
    return NULL;
  }
  if (! KosNamespace_Check(parent_ns)) {
    PyErr_Format(PyExc_TypeError, "Code.expression parent must be Namespace,"
                 " not %R", parent_ns);
    return NULL;
  }
  return KosExpression_New(self, parent_ns);
}


static PyObject *code_m_function(PyObject *self, PyObject *args) {

  PyObject *formals = NULL;
  PyObject *parent_ns = NULL;

  if (! PyArg_ParseTuple(args, "OO:Code.function", &formals, &parent_ns))
    return NULL;
  if (! KosFormals_Check(formals)) {
    PyErr_Format(PyExc_TypeError, "Code.function formals must be Formals,"
                 " not %R", formals);
    return NULL;
  }
  if (! parent_ns) {
    PyErr_SetString(PyExc_TypeError, "Code.function(parent_ns) requires"
                    " a parent namespace");
    return NULL;
  }
  if (! KosNamespace_Check(parent_ns)) {
    PyErr_Format(PyExc_TypeError, "Code.function parent must be Namespace,"
                 " not %R", parent_ns);
    return NULL;
  }
  return KosFunction_NewFromFormals(self, formals, parent_ns);
}


static PyMethodDef code_methods[] = {

  { "constant", (PyCFunction) code_m_constant, METH_O,
    "C.constant(index) => object" },

  { "oparg", (PyCFunction) code_m_oparg, METH_O,
    "C.oparg(index) => (op, arg)" },

  { "length", (PyCFunction) code_m_length, METH_NOARGS,
    "C.length() => int" },

  { "position", (PyCFunction) code_m_position, METH_O,
    "C.position(index) => (line, col)" },

  { "expression", (PyCFunction) code_m_expression, METH_O,
    "C.expression(parent_ns) => Expression (callable that returns Coroutine)" },

  { "function", (PyCFunction) code_m_function, METH_VARARGS,
    "C.function(formals, parent_ns) => Function (callable that returns Coroutine)" },

  { NULL, },
};


/* === Constant pool helpers === */


static uint32_t collect_const(
    PyObject **cpool,
    uint32_t len,
    PyObject *value) {

  uint32_t i = 0;

  for (; i < len; i++) {
    if (! cpool[i]) {
      Py_INCREF(value);
      cpool[i] = value;
      break;

    } else if (cpool[i] == value) {
      break;
    }
  }

  return i;
}


static uint32_t count_const(
    PyObject **cpool,
    uint32_t maxlen) {

  uint32_t i = 0;

  for (; i < maxlen; i++) {
    if (! cpool[i]) {
      return i;
    }
  }

  return i;
}


/* === Label helpers === */


static int collect_label(
    PyObject *labels,
    PyObject *name,
    uint32_t index) {

  PyObject *io = PyLong_FromLong(index);
  int ret = PyDict_SetItem(labels, name, io);
  Py_DECREF(io);
  return ret;
}


/* === Position unpacking === */


static int unpack_position(
    PyObject *entry,
    uint16_t *line,
    uint16_t *col) {

  PyObject *tmp = NULL;

  entry = PySequence_Tuple(entry);
  if (! entry) {
    return -1;
  }

  Py_ssize_t offset = 0;

  if (PyTuple_GET_SIZE(entry) > 1) {

    tmp = PyTuple_GET_ITEM(entry, offset++);
    if (! PyLong_Check(tmp)) {
      PyErr_Format(PyExc_RuntimeError, "Invalid position entry %R", entry);
      goto failure;
    }
    *line = (uint16_t) PyLong_AsLong(tmp);

    tmp = PyTuple_GET_ITEM(entry, offset++);
    if (! PyLong_Check(tmp)) {
      PyErr_Format(PyExc_RuntimeError, "Invalid position entry %R", entry);
      goto failure;
    }
    *col = (uint16_t) PyLong_AsLong(tmp);

  } else if (PyTuple_GET_SIZE(entry)) {

    tmp = PyTuple_GET_ITEM(entry, offset++);
    if (! PyLong_Check(tmp)) {
      PyErr_Format(PyExc_RuntimeError, "Invalid position entry %R", entry);
      goto failure;
    }
    *line = (uint16_t) PyLong_AsLong(tmp);
  }

  Py_DECREF(entry);
  return 0;

 failure:
  Py_DECREF(entry);
  return -1;
}


/* === Storage allocation === */


/**
   Allocate page-aligned storage for code, positions, and constant pool.

   Layout: [code array][padding][positions array][padding][cpool array]

   All sections are aligned to pointer size for the constant pool.
*/
static int code_alloc_storage(
    uint32_t instr_count,
    void **out_code,
    uint32_t pos_count,
    void **out_pos,
    uint32_t cpool_count,
    void **out_cpool,
    size_t *out_len) {

  size_t code_bytes = sizeof(KosOp) * instr_count;
  size_t pos_bytes = sizeof(KosPosition) * pos_count;
  size_t cpool_bytes = sizeof(PyObject *) * cpool_count;

  /* Align code section to pointer size */
  size_t ptr_size = sizeof(PyObject *);
  size_t code_offset = 0;
  size_t pos_offset = ((code_bytes + ptr_size - 1) / ptr_size) * ptr_size;
  size_t cpool_offset = pos_offset +
    ((pos_bytes + ptr_size - 1) / ptr_size) * ptr_size;
  size_t total = cpool_offset + cpool_bytes;

  /* Round up to page size */
  size_t page_size = sysconf(_SC_PAGE_SIZE);
  total = ((total + page_size - 1) / page_size) * page_size;

  void *data = NULL;
  if (posix_memalign(&data, page_size, total)) {
    PyErr_SetString(PyExc_MemoryError, "Failed to allocate bytecode storage");
    return -1;
  }

  *out_code = data + code_offset;
  *out_pos = data + pos_offset;
  *out_cpool = data + cpool_offset;
  *out_len = total;

  return 0;
}


/* === Instruction processing === */


static int process_instr(
    PyObject *self,
    PyObject *instr,
    uint32_t *instr_l,
    KosOp *code,
    uint32_t *pos_l,
    KosPosition *pos,
    uint32_t *const_l,
    PyObject **cpool) {

  SAYF("in process_instr(%R, %R, %u, %p, %u, %p, %u, %p)",
       self, instr, *instr_l, code, *pos_l, pos, *const_l, cpool);

  PyObject *pair = NULL;
  PyObject *labels = PyDict_New();

  if (! labels) {
    return -1;
  }

  uint16_t line = 0, col = 0;
  uint16_t last_line = 0, last_col = 0;
  uint32_t di = 0;  /* destination instruction index */
  uint32_t pi = 0;  /* position index */

  for (uint32_t i = 0; i < *instr_l; i++) {
    pair = PySequence_Fast_GET_ITEM(instr, i);
    pair = PySequence_Fast(pair, "instruction pairs must be sequences");
    if (! pair) {
      goto failure;
    }

    Py_ssize_t pl = PySequence_Fast_GET_SIZE(pair);
    if (pl < 2) {
      PyErr_SetString(PyExc_RuntimeError, "Code instructions"
                      " and arguments must be pairs of 2");
      goto failure;
    }

    PyObject *a = PySequence_Fast_GET_ITEM(pair, 0);
    PyObject *b = PySequence_Fast_GET_ITEM(pair, 1);

    if (! PyLong_Check(a)) {
      PyErr_Format(PyExc_RuntimeError, "Code instruction undefined"
                   " %R:%R", a, b);
      goto failure;
    }

    long lop = PyLong_AsLong(a);
    if ((lop < (long) KOS_OP_NOOP) || (lop >= (long) KOS_OP_THEEND)) {
      PyErr_Format(PyExc_RuntimeError, "Code instruction undefined"
                   " %R:%R", a, b);
      goto failure;
    }

    code[di].op = (uint16_t) lop;
    code[di].arg = 0;

    KosOpStyle style = opcode_styles[(KosOpCode) lop];

    switch (style) {
    case KOSTYLE_CONST:
      code[di].arg = (uint16_t) collect_const(cpool, *const_l, b);
      break;

    case KOSTYLE_VAR:
      {
        /* Collect variable name into constants pool */
        PyObject *name = PyObject_Str(b);
        if (! name) {
          goto failure;
        }
        code[di].arg = (uint16_t) collect_const(cpool, *const_l, name);
        Py_DECREF(name);
      }
      break;

    case KOSTYLE_LABEL:
      /* Store index for second pass label resolution */
      {
        PyObject *index = PyLong_FromLong(di);
        if (! index) {
          goto failure;
        }
        int res = PyDict_SetItem(labels, index, b);
        Py_DECREF(index);
        if (res) {
          goto failure;
        }
      }
      break;

    case KOSTYLE_COMPILE:
      /* Handled specially below */
      break;

    default:
      /* Everything else should be a number */
      if (! PyLong_Check(b)) {
        PyErr_Format(PyExc_RuntimeError, "Code instruction expects int"
                     " argument, got %R:%R", a, b);
        goto failure;
      }
      code[di].arg = (uint16_t) PyLong_AsLong(b);
    }

    /* Handle special opcodes */
    switch ((KosOpCode) lop) {
    case KOS_OP_LABEL:
      /* Store the label's destination index */
      if (collect_label(labels, b, di)) {
        goto failure;
      }
      code[di].op = KOS_OP_NOOP;
      break;

    case KOS_OP_POSITION:
      /* Update position state, record if changed */
      if (unpack_position(b, &line, &col)) {
        goto failure;
      }
      if (line != last_line || col != last_col) {
        pos[pi].index = (uint16_t) di;
        pos[pi].line = line;
        pos[pi].col = col;
        pi++;
        last_line = line;
        last_col = col;
      }
      code[di].op = KOS_OP_NOOP;
      break;

    case KOS_OP_THEEND:
      code[di].op = KOS_OP_NOOP;
      /* fall through */
    case KOS_OP_NOOP:
      break;

    case KOS_OP_YIELD:
    case KOS_OP_YIELD_FROM:
      KosCode_FLAGS(self) |= KOS_CODE_YIELDS;
      di++;
      break;

    default:
      di++;
      break;
    }

    Py_CLEAR(pair);
  }

  /* Second pass: resolve jump labels */
  for (uint32_t i = di; i--; ) {
    if (opcode_styles[code[i].op] == KOSTYLE_LABEL) {

      PyObject *index = PyLong_FromLong(i);

      /* borrowed */
      PyObject *key = PyDict_GetItem(labels, index);
      Py_CLEAR(index);

      if (! key) {
        PyErr_Format(PyExc_RuntimeError, "opcode jump not seen before at"
                     " index %u", i);
        goto failure;
      }

      /* borrowed */
      PyObject *dest = PyDict_GetItem(labels, key);
      if (! dest) {
        PyErr_Format(PyExc_RuntimeError, "opcode jump label missing %R",
                     key);
        goto failure;
      }

      code[i].arg = (uint16_t) PyLong_AsLong(dest);
    }
  }

  *const_l = count_const(cpool, *const_l);
  *instr_l = di;
  *pos_l = pi;

  Py_CLEAR(labels);
  return PyErr_Occurred() ? -1 : 0;

 failure:
  Py_XDECREF(labels);
  Py_XDECREF(pair);
  return -1;
}


/* === Code initialization === */


static int code_instr_init(PyObject *self, PyObject *instr) {

  SAYF("in code_instr_init(%R, %R)", self, instr);

  KosOp *code = NULL;
  KosPosition *pos = NULL;
  PyObject **cpool = NULL;

  uint32_t instr_l = 0;
  uint32_t pos_count = 0;
  uint32_t const_count = 0;
  size_t alloc_size = 0;

  Py_ssize_t _instr_l = PySequence_Fast_GET_SIZE(instr);
  if (_instr_l < 0) {
    return -1;
  }
  instr_l = (uint32_t) _instr_l;

  if (instr_l == 0) {
    PyErr_SetString(PyExc_ValueError,
                    "Code must have at least one instruction"
                    " (e.g. RETURN to return None)");
    return -1;
  }

  /* First pass: count constants and position changes (worst case) */
  for (uint32_t i = instr_l; i--; ) {
    PyObject *pair = PySequence_Fast_GET_ITEM(instr, i);
    PyObject *a = PySequence_Fast_GET_ITEM(pair, 0);

    if (! PyLong_Check(a)) {
      PyErr_Format(PyExc_RuntimeError, "Code instruction undefined"
                   " %R", a);
      return -1;
    }

    long lop = PyLong_AsLong(a);
    if ((lop < (long) KOS_OP_NOOP) || (lop >= (long) KOS_OP_THEEND)) {
      PyErr_Format(PyExc_RuntimeError, "Code instruction undefined"
                   " %R", a);
      return -1;
    }

    KosOpStyle style = opcode_styles[(KosOpCode) lop];
    if (style == KOSTYLE_CONST || style == KOSTYLE_VAR) {
      const_count++;
    }

    if ((KosOpCode) lop == KOS_OP_POSITION) {
      pos_count++;
    }
  }

  /* Allocate storage */
  if (code_alloc_storage(instr_l, (void **) &code,
                         pos_count, (void **) &pos,
                         const_count, (void **) &cpool,
                         &alloc_size)) {
    return -1;
  }

  SAYF("allocated %zu bytes of storage: %p, %p, %p",
       alloc_size, code, pos, cpool);
  KosCode_ALLOCATION(self) = alloc_size;

  /* Initialize code array */
  for (uint32_t i = instr_l; i--; ) {
    code[i].op = KOS_OP_NOOP;
    code[i].arg = 0;
  }

  /* Initialize positions array */
  for (uint32_t i = pos_count; i--; ) {
    pos[i].index = 0;
    pos[i].line = 0;
    pos[i].col = 0;
  }

  /* Initialize constant pool */
  for (uint32_t i = const_count; i--; ) {
    cpool[i] = NULL;
  }

  /* Process instructions */
  if (process_instr(self, instr, &instr_l, code,
                    &pos_count, pos,
                    &const_count, cpool)) {
    goto failure;
  }

  /* Make storage read-only */
  if (mprotect(code, alloc_size, PROT_READ)) {
    PyErr_SetFromErrno(PyExc_OSError);
    goto failure;
  }

  KosCode_CODE_LEN(self) = instr_l;
  KosCode_CODE(self) = code;
  KosCode_POS_LEN(self) = pos_count;
  KosCode_POS(self) = pos;
  KosCode_CPOOL_LEN(self) = const_count;
  KosCode_CPOOL(self) = cpool;

  return 0;

 failure:
  if (code) {
    free(code);
  }
  return -1;
}


/* === Python type methods === */


static PyObject *code_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  PyObject *nspec = NULL;
  PyObject *instr = NULL;
  PyObject *name = NULL;
  PyObject *filename = NULL;

  if (kwds && PyDict_Size(kwds)) {
    PyErr_SetString(PyExc_TypeError, "Code takes no named arguments");
    return NULL;
  }

  if (! PyArg_ParseTuple(args, "OO|OO:Code",
                         &nspec, &instr, &name, &filename)) {
    return NULL;
  }

  return KosCode_New(nspec, instr, name, filename);
}


static int code_traverse(PyObject *self, visitproc visit, void *arg) {

  Py_VISIT(KosCode_NAME(self));
  Py_VISIT(KosCode_FILENAME(self));
  Py_VISIT(KosCode_NSPEC(self));
  Py_VISIT(KosCode_EXTRAS(self));

  for (uint32_t i = KosCode_CPOOL_LEN(self); i--; ) {
    Py_VISIT(KosCode_GET_CONST(self, i));
  }

  return 0;
}


static int code_clear(PyObject *self) {

  Py_CLEAR(KosCode_NAME(self));
  Py_CLEAR(KosCode_FILENAME(self));
  Py_CLEAR(KosCode_NSPEC(self));
  Py_CLEAR(KosCode_EXTRAS(self));

  for (uint32_t i = KosCode_CPOOL_LEN(self); i--; ) {
    Py_DECREF(KosCode_GET_CONST(self, i));
  }

  void *data = KosCode_CODE(self);
  if (data) {
    mprotect(data, KosCode_ALLOCATION(self), PROT_WRITE);
    free(data);
  }

  KosCode_ALLOCATION(self) = 0;
  KosCode_CODE_LEN(self) = 0;
  KosCode_CODE(self) = NULL;
  KosCode_POS_LEN(self) = 0;
  KosCode_POS(self) = NULL;
  KosCode_CPOOL_LEN(self) = 0;
  KosCode_CPOOL(self) = NULL;

  return 0;
}


static void code_dealloc(PyObject *self) {

  Py_CLEAR(KosCode_NAME(self));
  Py_CLEAR(KosCode_FILENAME(self));
  Py_CLEAR(KosCode_NSPEC(self));
  Py_CLEAR(KosCode_EXTRAS(self));

  for (uint32_t i = KosCode_CPOOL_LEN(self); i--; ) {
    Py_DECREF(KosCode_GET_CONST(self, i));
  }

  void *data = KosCode_CODE(self);
  if (data) {
    mprotect(data, KosCode_ALLOCATION(self), PROT_WRITE);
    free(data);
  }

  KosCode_ALLOCATION(self) = 0;
  KosCode_CODE_LEN(self) = 0;
  KosCode_CODE(self) = NULL;
  KosCode_POS_LEN(self) = 0;
  KosCode_POS(self) = NULL;
  KosCode_CPOOL_LEN(self) = 0;
  KosCode_CPOOL(self) = NULL;

  Py_TYPE(self)->tp_free(self);
}


static PyObject *code_repr(PyObject *self) {

  return PyUnicode_FromFormat("<Code %R at %p>",
                              KosCode_NAME(self), self);
}


/* === Type definition === */


PyTypeObject KosCodeType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "koskari.lib.Code",
  sizeof(KosCode),
  0,

  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_repr = code_repr,
  .tp_getset = code_getset,
  .tp_members = code_members,
  .tp_methods = code_methods,
  .tp_new = code_new,
  .tp_traverse = code_traverse,
  .tp_clear = code_clear,
  .tp_dealloc = code_dealloc,
};


/* === Constructor === */


PyObject *KosCode_New(
    PyObject *nspec,
    PyObject *instr,
    PyObject *name,
    PyObject *filename) {

  if (! filename || filename == Py_None) {
    filename = _str_anon;
  }

  if (! name || name == Py_None) {
    name = _str_anon;
  }

  if (! PyUnicode_Check(filename)) {
    PyErr_Format(PyExc_TypeError, "Code filename must be a str,"
                 " not %R", filename);
    return NULL;
  }

  if (! PyUnicode_Check(name)) {
    PyErr_Format(PyExc_TypeError, "Code name must be a str,"
                 " not %R", name);
    return NULL;
  }

  if (! KosNamespaceSpec_CheckExact(nspec)) {
    PyErr_Format(PyExc_TypeError, "Code namespace spec must be a"
                 " NamespaceSpec, not %R", nspec);
    return NULL;
  }

  instr = PySequence_Fast(instr, "Code instructions must be a sequence");
  if (! instr) {
    return NULL;
  }

  PyObject *self = PyObject_GC_New(PyObject, &KosCodeType);
  if (! self) {
    Py_DECREF(instr);
    return NULL;
  }

  KosCode_FLAGS(self) = 0;

  Py_INCREF(name);
  KosCode_NAME(self) = name;

  Py_INCREF(filename);
  KosCode_FILENAME(self) = filename;

  Py_INCREF(nspec);
  KosCode_NSPEC(self) = nspec;

  KosCode_ALLOCATION(self) = 0;

  KosCode_CODE_LEN(self) = 0;
  KosCode_CODE(self) = NULL;

  KosCode_POS_LEN(self) = 0;
  KosCode_POS(self) = NULL;

  KosCode_CPOOL_LEN(self) = 0;
  KosCode_CPOOL(self) = NULL;

  KosCode_FAUX(self) = NULL;
  KosCode_EXTRAS(self) = PyDict_New();

  if (code_instr_init(self, instr)) {
    Py_CLEAR(self);
    Py_DECREF(instr);
    return NULL;
  }

  PyObject_GC_Track(self);

  Py_DECREF(instr);
  return self;
}


/* === Accessor functions === */


PyObject *KosCode_GetConstant(PyObject *self, uint32_t index) {

  if (index >= KosCode_CPOOL_LEN(self)) {
    PyErr_Format(PyExc_IndexError, "constant index out of bounds, %u",
                 index);
    return NULL;
  }

  /* Result is borrowed */
  return KosCode_GET_CONST(self, index);
}


PyObject *KosCode_GetInstruction(PyObject *self, uint32_t index) {

  if (index >= KosCode_CODE_LEN(self)) {
    PyErr_Format(PyExc_IndexError, "instruction index out of bounds, %u",
                 index);
    return NULL;
  }

  KosOp op = KosCode_CODE(self)[index];

  PyObject *tup = PyTuple_New(2);
  if (! tup) {
    return NULL;
  }

  PyTuple_SET_ITEM(tup, 0, PyLong_FromLong(op.op));
  PyTuple_SET_ITEM(tup, 1, PyLong_FromLong(op.arg));

  return tup;
}


int KosCode_GetPosition(
    PyObject *self,
    uint32_t index,
    uint16_t *out_line,
    uint16_t *out_col) {

  KosPosition *pos = KosCode_POS(self);
  uint32_t pos_len = KosCode_POS_LEN(self);

  if (pos_len == 0) {
    *out_line = 0;
    *out_col = 0;
    return 0;
  }

  /* Binary search for largest pos[i].index <= index */
  uint32_t lo = 0;
  uint32_t hi = pos_len;

  while (lo < hi) {
    uint32_t mid = lo + (hi - lo) / 2;
    if (pos[mid].index <= index) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }

  if (lo == 0) {
    /* Index is before first position entry */
    *out_line = 0;
    *out_col = 0;
  } else {
    *out_line = pos[lo - 1].line;
    *out_col = pos[lo - 1].col;
  }

  return 0;
}


PyCodeObject *KosCode_Faux(PyObject *self) {

  if (! self) {
    return PyCode_NewEmpty("", "", 0);
  }

  PyCodeObject *faux = KosCode_FAUX(self);
  if (faux) {
    return faux;
  }

  uint16_t line = 0, col = 0;
  if (KosCode_CODE_LEN(self)) {
    KosCode_GetPosition(self, 0, &line, &col);
  }

  faux = PyCode_NewEmpty("", "", line);
  if (! faux) {
    return NULL;
  }

  PyObject *tmp;

  tmp = KosCode_FILENAME(self);
  Py_CLEAR(faux->co_filename);
  Py_INCREF(tmp);
  faux->co_filename = tmp;

  tmp = KosCode_NAME(self);
  Py_CLEAR(faux->co_name);
  Py_INCREF(tmp);
  faux->co_name = tmp;

  KosCode_FAUX(self) = faux;
  return faux;
}


/* === Enum creation === */


#define int_enum(enum_dict, ek)                   \
  do {                                            \
    PyObject *_v = PyLong_FromLong(ek);           \
    PyDict_SetItemString(enum_dict, #ek, _v);     \
    Py_DECREF(_v);                                \
  } while(0)


PyObject *KosOpStyleEnum = NULL;


static PyObject *create_opstyle_enum(void) {

  PyObject *enum_module = NULL;
  PyObject *enum_dict = NULL;

  enum_module = PyImport_ImportModule("enum");
  if (! enum_module) {
    return NULL;
  }

  enum_dict = PyDict_New();
  if (! enum_dict) {
    Py_DECREF(enum_module);
    return NULL;
  }

  int_enum(enum_dict, KOSTYLE_COMPILE);
  int_enum(enum_dict, KOSTYLE_LABEL);
  int_enum(enum_dict, KOSTYLE_NOARG);
  int_enum(enum_dict, KOSTYLE_COUNT);
  int_enum(enum_dict, KOSTYLE_PAIR_COUNT);
  int_enum(enum_dict, KOSTYLE_STAR);
  int_enum(enum_dict, KOSTYLE_CONST);
  int_enum(enum_dict, KOSTYLE_VAR);
  int_enum(enum_dict, KOSTYLE_FASTVAR);

  PyObject *res = PyObject_CallMethod(enum_module, "IntEnum", "sO",
                                      "OpStyle", enum_dict);

  Py_DECREF(enum_dict);
  Py_DECREF(enum_module);

  return res;
}


PyObject *KosOpCodeEnum = NULL;


static PyObject *create_opcode_enum(void) {

  PyObject *enum_module = NULL;
  PyObject *enum_dict = NULL;

  enum_module = PyImport_ImportModule("enum");
  if (! enum_module) {
    return NULL;
  }

  enum_dict = PyDict_New();
  if (! enum_dict) {
    Py_DECREF(enum_module);
    return NULL;
  }

#define OP(name, style)                         \
  int_enum(enum_dict, KOS_OP_##name);

#include "opcodes.incl"
#undef OP

  PyObject *res = PyObject_CallMethod(enum_module, "IntEnum", "sO",
                                      "OpCode", enum_dict);

  Py_DECREF(enum_dict);
  Py_DECREF(enum_module);

  if (! res) {
    return NULL;
  }

  /* Attach style attribute to each opcode */
#define OP(name, style)                                           \
  do {                                                            \
    PyObject *_en = PyObject_GetAttrString(res, "KOS_OP_" #name); \
    PyObject *_fl = PyLong_FromLong(KOSTYLE_##style);             \
    PyObject_SetAttrString(_en, "style", _fl);                    \
    Py_DECREF(_fl);                                               \
    Py_DECREF(_en);                                               \
  } while (0);

#include "opcodes.incl"
#undef OP

  return res;
}


/* === Module lifecycle === */


int koskari_bytecode_init(PyObject *mod) {

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict) {
    return -1;
  }

  if (PyType_Ready(&KosCodeType)) {
    return -1;
  }

  if (PyDict_SetItemString(dict, "Code", (PyObject *) &KosCodeType)) {
    return -1;
  }

  PyObject *stuff;

  stuff = create_opcode_enum();
  if (! stuff) {
    return -1;
  }
  if (PyDict_SetItemString(dict, "OpCode", stuff)) {
    Py_DECREF(stuff);
    return -1;
  }
  KosOpCodeEnum = stuff;

  stuff = create_opstyle_enum();
  if (! stuff) {
    return -1;
  }
  if (PyDict_SetItemString(dict, "OpStyle", stuff)) {
    Py_DECREF(stuff);
    return -1;
  }
  KosOpStyleEnum = stuff;

  STR_CONST(_str_anon, "<anon>");

  return 0;
}


int koskari_bytecode_clear(PyObject *mod) {

  Py_CLEAR(KosOpCodeEnum);
  Py_CLEAR(KosOpStyleEnum);
  Py_CLEAR(_str_anon);

  return 0;
}


// The end.
