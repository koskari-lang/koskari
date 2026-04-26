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

#include <string.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <unistd.h>

#include "lib.h"
#include "debug.h"


/* === Static string constants === */


static PyObject *_str_anon = NULL;


/* Max depth for PUSH/POP stacks used in extent derivation and FAST validation */
#define DERIVE_EXTENT_STACK_MAX 64


/* === Opcode styles table === */


typedef struct {
  KosOpStyle style;
  KosExtraSchema extra;
} OpStyleEntry;


#define OP_NARGS(_1, _2, _3, _4, _5, _6, _7, N, ...)  N
#define OP_CONCAT(a, b)  a ## b
#define OP_INNER(N, ...)  OP_CONCAT(OP_, N)(__VA_ARGS__)
#define OP(...)  OP_INNER(OP_NARGS(__VA_ARGS__, 7, 6, 5, 4, 3, 2, 1), __VA_ARGS__)

#define OP_2(name, style)  { KOSTYLE_##style, { \
    KOSTYLE_NOARG, KOSTYLE_NOARG, \
    KOSTYLE_NOARG, KOSTYLE_NOARG } },
#define OP_6(name, EXTRA, f0, f1, f2, f3)  { KOSTYLE_EXTRA, { \
    KOSTYLE_##f0, KOSTYLE_##f1, \
    KOSTYLE_##f2, KOSTYLE_##f3 } },

static const OpStyleEntry opcode_styles[] = {
  #include "opcodes.incl"
};

#undef OP
#undef OP_2
#undef OP_6
#undef OP_INNER
#undef OP_CONCAT
#undef OP_NARGS


KosOpStyle kos_opcode_style(KosOpCode op) {

  if (op <= KOS_OP_THEEND) {
    return opcode_styles[op].style;
  }
  return (KosOpStyle) -1;
}


/* === Opcode names table === */


#define OP(name, ...)  #name ,

static const char *const opcode_names[] = {
  #include "opcodes.incl"
};

#undef OP


const char *kos_opcode_name(KosOpCode op) {

  if (op <= KOS_OP_THEEND) {
    return opcode_names[op];
  }
  return "UNKNOWN_OP";
}


/* Extent helpers (defined after extent_pairs) */
static inline int is_push_op(KosOpCode op);
static inline int is_pop_op(KosOpCode op);
static inline KosOpCode pop_for_push(KosOpCode push_op);
static inline KosExtentKind kind_for_push(KosOpCode push_op);


/* === Extra operand schema === */


KosExtraSchema kos_opcode_extra_schema(KosOpCode op) {

  if (op <= KOS_OP_THEEND) {
    return opcode_styles[op].extra;
  }
  return (KosExtraSchema){ KOSTYLE_NOARG, KOSTYLE_NOARG,
      KOSTYLE_NOARG, KOSTYLE_NOARG };
}


/* === Softening helpers === */

/*
   Softening turns sealed (hardened) bytecode back into a Python list of
   (opcode, arg) pairs suitable for Code(...). The stream is longer than
   code_len because it re-inserts KOS_OP_POSITION and KOS_OP_LABEL pseudo-ops
   that were stripped during hardening. Label *names* are not stored in
   hardened form (only target PCs). ``collect_label_map`` uses a single
   ``PyDict`` and one forward scan; on first discovery of each PC it assigns
   the PC text immediately. Emission order per
   instruction index is LABEL (if target), POSITION (if changed), then opcode.
*/


/**
   Record a jump-target PC in ``label_map`` and assign a label name on first
   discovery. Duplicate PCs are ignored.

   :param label_map: the only dict used for soften label collection
   :param pc: instruction index
   :return: 0 on success, -1 on error
*/
static int soften_label_map_add_pc(
    PyObject *label_map,
    uint32_t pc) {

  PyObject *key = PyLong_FromUnsignedLong((unsigned long) pc);
  if (! key)
    return -1;

  PyObject *existing = PyDict_GetItemWithError(label_map, key);
  if (! existing && PyErr_Occurred()) {
    Py_DECREF(key);
    return -1;
  }

  if (! existing) {
    PyObject *name = PyUnicode_FromFormat("LABEL_%u", (unsigned int) pc);
    if (! name) {
      Py_DECREF(key);
      return -1;
    }

    if (PyDict_SetItem(label_map, key, name) < 0) {
      Py_DECREF(name);
      Py_DECREF(key);
      return -1;
    }
    Py_DECREF(name);
  }

  Py_DECREF(key);
  return 0;
}


/**
   Build the single ``dict`` mapping each jump-target PC (``int`` key) to
   label string (``"L0"``, ``"L1"``, ...).

   Hardened code stores branch destinations as raw PCs in ``KosOp.arg`` for
   KOSTYLE_LABEL opcodes, and likewise in EXTRA row slots marked LABEL in the
   opcode schema. One ``PyDict`` is filled in one scan, and first discovery of
   a target PC assigns the PC text immediately.

   :param self: KosCode object
   :param code_len: instruction count (hardened stream length)

   :return: new reference to ``dict`` (possibly empty); NULL on error
*/
static PyObject *collect_label_map(PyObject *self, uint32_t code_len) {

  PyObject *label_map = PyDict_New();
  if (! label_map) {
    return NULL;
  }

  KosOp *code = KosCode_CODE(self);

  /* Every JUMP*, CHECK*, etc. records the destination PC in op.arg */
  for (uint32_t i = 0; i < code_len; i++) {
    KosOpCode op = (KosOpCode) code[i].op;
    if (op >= KOS_OP_THEEND) {
      Py_DECREF(label_map);
      PyErr_Format(PyExc_RuntimeError, "invalid opcode %u", (unsigned) op);
      return NULL;
    }

    KosOpStyle st = opcode_styles[op].style;

    if (st == KOSTYLE_LABEL) {
      /* op.arg is the destination instruction index */
      if (soften_label_map_add_pc(label_map, code[i].arg)) {
        Py_DECREF(label_map);
        return NULL;
      }

    } else if (st == KOSTYLE_EXTRA) {
      /* insn.arg is row index; each schema LABEL field holds a dest PC */
      uint16_t eidx = code[i].arg;
      if (eidx >= KosCode_EXTRA_LEN(self)) {
        Py_DECREF(label_map);
        PyErr_SetString(PyExc_RuntimeError, "EXTRA index out of bounds");
        return NULL;
      }

      KosExtra row = KosCode_GetExtra(self, eidx);
      KosExtraSchema schema = kos_opcode_extra_schema(op);
      uint16_t raw[4] = { row.a0, row.a1, row.a2, row.a3 };

      for (int slot = 0; slot < 4; slot++) {
        KosOpStyle fk = (slot == 0) ? schema.f0 : (slot == 1) ? schema.f1 :
            (slot == 2) ? schema.f2 : schema.f3;
        if (fk == KOSTYLE_LABEL) {
          if (soften_label_map_add_pc(label_map, raw[slot])) {
            Py_DECREF(label_map);
            return NULL;
          }
        }
      }
    }
  }

  return label_map;
}


/**
  Build a 4-tuple of Python soften arguments for one EXTRA opcode row.

  Hardened EXTRA rows store uint16 indices (cpool, raw ints, or PCs). This
  reverses emit: CONST/VAR become Python values from the pool; LABEL slots
  look up the destination PC in ``label_map``; other slots stay as integers
  (FAST,
  COUNT, STAR, etc.). NOARG schema slots become 0, matching the collect
  phase default.

  :param self: KosCode (for constant pool lookup)
  :param op: opcode (selects EXTRA field schema)
  :param row: raw a0..a3 from hardened extra table
  :param label_map: ``dict`` mapping target PC ``int`` to label ``str``

  :return: new tuple of length 4, or NULL on error
*/
static PyObject *soften_extra_tuple(
    PyObject *self,
    KosOpCode op,
    KosExtra row,
    PyObject *label_map) {

  KosExtraSchema schema = kos_opcode_extra_schema(op);
  uint16_t raw[4] = { row.a0, row.a1, row.a2, row.a3 };
  PyObject *items[4];

  for (int slot = 0; slot < 4; slot++) {
    KosOpStyle fk = (slot == 0) ? schema.f0 : (slot == 1) ? schema.f1 :
        (slot == 2) ? schema.f2 : schema.f3;

    /* Mirror code_collect: skip NOARG (emit uses 0); else decode by kind */
    if (fk == KOSTYLE_NOARG) {
      items[slot] = PyLong_FromLong(0);

    } else if (fk == KOSTYLE_CONST || fk == KOSTYLE_VAR) {
      PyObject *v = KosCode_GetConstant(self, raw[slot]);
      if (! v) {
        for (int j = 0; j < slot; j++)
          Py_DECREF(items[j]);
        return NULL;
      }
      Py_INCREF(v);
      items[slot] = v;

    } else if (fk == KOSTYLE_LABEL) {
      PyObject *key = PyLong_FromUnsignedLong((unsigned long) raw[slot]);
      if (! key) {
        for (int j = 0; j < slot; j++)
          Py_DECREF(items[j]);
        return NULL;
      }
      PyObject *lab = PyDict_GetItem(label_map, key);
      Py_DECREF(key);
      if (! lab) {
        for (int j = 0; j < slot; j++)
          Py_DECREF(items[j]);
        PyErr_Format(PyExc_RuntimeError,
                     "label pc %u missing from label map", raw[slot]);
        return NULL;
      }
      Py_INCREF(lab);
      items[slot] = lab;

    } else {
      items[slot] = PyLong_FromLong((long) raw[slot]);
    }

    if (! items[slot]) {
      for (int j = 0; j < slot; j++)
        Py_DECREF(items[j]);
      return NULL;
    }
  }

  return PyTuple_Pack(4, items[0], items[1], items[2], items[3]);
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

  { "extras", T_OBJECT,
    offsetof(KosCode, extras), READONLY,
    "Mutable dict for compiler/runtime metadata; update in place" },

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


static PyObject *code_m_function_spec(PyObject *self, PyObject *formals_spec) {

  if (! KosFormalsSpec_CheckExact(formals_spec)) {
    PyErr_Format(PyExc_TypeError, "Code.function_spec formals_spec must be"
                 " FormalsSpec, not %R", formals_spec);
    return NULL;
  }
  return KosFunctionSpec_New(NULL, formals_spec, self);
}


static PyObject *code_m_extra_tuple(PyObject *self, PyObject *index) {

  if (! PyLong_Check(index)) {
    PyErr_Format(PyExc_TypeError, "Code extra_tuple index must be int,"
                 " not %R", index);
    return NULL;
  }

  long i = PyLong_AsLong(index);
  uint32_t extra_len = KosCode_EXTRA_LEN(self);

  if (i < 0 || (uint32_t) i >= extra_len) {
    PyErr_Format(PyExc_IndexError, "Code extra index out of bounds: %ld",
                 i);
    return NULL;
  }

  KosExtra row = KosCode_GetExtra(self, (uint32_t) i);
  return Py_BuildValue("(HHHH)", (unsigned short) row.a0,
                       (unsigned short) row.a1,
                       (unsigned short) row.a2,
                       (unsigned short) row.a3);
}


/**
   Python method: ``Code.soften()`` -> list of instruction pairs.

   Reconstructs the softened stream the compiler would pass to ``Code()``:
   per hardened instruction index, optional ``KOS_OP_LABEL`` (name for jump
   target PCs), ``KOS_OP_POSITION`` when (line, col) changes, then the real
   opcode with pool values and string labels instead of raw indices.

   :param self: KosCode instance

   :return: new list of ``(int opcode, arg)`` pairs, or NULL on error
*/
static PyObject *code_m_soften(PyObject *self, PyObject *unused) {

  (void) unused;

  uint32_t code_len = KosCode_CODE_LEN(self);
  PyObject *label_map = collect_label_map(self, code_len);

  if (! label_map) {
    return NULL;
  }

  PyObject *out = PyList_New(0);
  if (! out) {
    Py_DECREF(label_map);
    return NULL;
  }

  int have_prev = 0;
  uint16_t prev_line = 0;
  uint16_t prev_col = 0;

  for (uint32_t i = 0; i < code_len; i++) {
    /* If this instruction index is a jump target, emit a label pseudo-op */
    {
      PyObject *key_i = PyLong_FromUnsignedLong((unsigned long) i);
      if (! key_i) {
        Py_DECREF(out);
        Py_DECREF(label_map);
        return NULL;
      }
      PyObject *lab = PyDict_GetItem(label_map, key_i);
      Py_DECREF(key_i);
      if (lab) {
        Py_INCREF(lab);
        PyObject *t = Py_BuildValue("(iN)", (int) KOS_OP_LABEL, lab);
        if (! t || PyList_Append(out, t) < 0) {
          Py_XDECREF(t);
          Py_DECREF(out);
          Py_DECREF(label_map);
          return NULL;
        }
        Py_DECREF(t);
      }
    }

    uint16_t line = 0, col = 0;

    /* Delta-decode from KosCode position table (same as code_collect) */
    KosCode_GetPosition(self, i, &line, &col);

    if (! have_prev || line != prev_line || col != prev_col) {
      PyObject *t = Py_BuildValue("(i(HH))", (int) KOS_OP_POSITION, line, col);
      if (! t || PyList_Append(out, t) < 0) {
        Py_XDECREF(t);
        Py_DECREF(out);
        Py_DECREF(label_map);
        return NULL;
      }
      Py_DECREF(t);
      have_prev = 1;
      prev_line = line;
      prev_col = col;
    }

    KosOp insn = KosCode_CODE(self)[i];
    KosOpCode op = (KosOpCode) insn.op;

    if (op > KOS_OP_THEEND) {
      Py_DECREF(out);
      Py_DECREF(label_map);
      PyErr_Format(PyExc_RuntimeError, "invalid opcode %u", (unsigned) op);
      return NULL;
    }

    if (op == KOS_OP_LABEL || op == KOS_OP_POSITION) {
      Py_DECREF(out);
      Py_DECREF(label_map);
      PyErr_SetString(PyExc_RuntimeError,
                      "hardened code must not contain LABEL or POSITION");
      return NULL;
    }

    KosOpStyle st = opcode_styles[op].style;
    PyObject *pair = NULL;

    if (st == KOSTYLE_CONST || st == KOSTYLE_VAR) {
      PyObject *val = KosCode_GetConstant(self, insn.arg);
      if (! val) {
        Py_DECREF(out);
        Py_DECREF(label_map);
        return NULL;
      }
      pair = Py_BuildValue("(iO)", (int) op, val);
    } else if (st == KOSTYLE_LABEL) {
      /* insn.arg is destination PC; label string is label_map[insn.arg] */
      PyObject *key_d = PyLong_FromUnsignedLong((unsigned long) insn.arg);
      if (! key_d) {
        Py_DECREF(out);
        Py_DECREF(label_map);
        return NULL;
      }
      PyObject *lab = PyDict_GetItem(label_map, key_d);
      Py_DECREF(key_d);
      if (! lab) {
        Py_DECREF(out);
        Py_DECREF(label_map);
        PyErr_Format(PyExc_RuntimeError,
                     "label pc %u missing from label map", insn.arg);
        return NULL;
      }
      Py_INCREF(lab);
      pair = Py_BuildValue("(iN)", (int) op, lab);
    } else if (st == KOSTYLE_EXTRA) {
      if (insn.arg >= KosCode_EXTRA_LEN(self)) {
        Py_DECREF(out);
        Py_DECREF(label_map);
        PyErr_SetString(PyExc_RuntimeError, "EXTRA index out of bounds");
        return NULL;
      }
      KosExtra row = KosCode_GetExtra(self, insn.arg);
      PyObject *tup = soften_extra_tuple(self, op, row, label_map);
      if (! tup) {
        Py_DECREF(out);
        Py_DECREF(label_map);
        return NULL;
      }
      pair = Py_BuildValue("(iN)", (int) op, tup);
    } else {
      /* COUNT, FASTVAR, NOARG raw fields: pass through as small ints */
      pair = Py_BuildValue("(ii)", (int) op, (int) insn.arg);
    }

    if (! pair || PyList_Append(out, pair) < 0) {
      Py_XDECREF(pair);
      Py_DECREF(out);
      Py_DECREF(label_map);
      return NULL;
    }
    Py_DECREF(pair);
  }

  Py_DECREF(label_map);

  return out;
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

  { "function_spec", (PyCFunction) code_m_function_spec, METH_O,
    "C.function_spec(formals_spec) => FunctionSpec" },

  { "extra_tuple", (PyCFunction) code_m_extra_tuple, METH_O,
    "C.extra_tuple(index) => (a0, a1, a2, a3) for EXTRA opcode operand row" },

  { "soften", (PyCFunction) code_m_soften, METH_NOARGS,
    "C.soften() => list of (opcode, arg) pairs with LABEL/POSITION "
    "pseudo-ops and pool/label values restored" },

  { NULL, },
};


/* === Phase 1: Collect === */
/*
   Transform compiler output into sealed readonly bytecode. Four phases:
   Collect (size everything, build label map + cpool + extra) -> Allocate
   (single block) -> Emit (write into block) -> Finalize (extents, validate,
   mprotect). Sizing is exact: no over-allocation. Cpool uses identity
   dedup (PyList linear scan) so 1 and True stay distinct.
*/


/**
   Collection state for the finalization pipeline. Built during the
   collect phase; used for sizing and by the emit phase. Lifetime:
   from code_collect through code_emit.
*/
typedef struct {
  PyObject *cpool_list;       /* PyList: identity-based const internment (no dict) */
  PyObject *labels;           /* PyDict: label_name -> output_pc for jump resolution */
  KosExtra *extra_rows;       /* growable array of KosExtra; CONST/VAR interned */
  uint32_t extra_len;
  uint32_t extra_cap;
  KosPosition *pos_entries;   /* delta-encoded positions; index where (line,col) changes */
  uint32_t pos_count;
  uint32_t pos_cap;
  uint32_t code_len;          /* instruction count after squashing LABEL/POSITION */
  uint32_t cpool_len;         /* unique constants by identity */
  uint32_t extent_count;      /* PUSH_* count for extent table sizing */
  uint16_t line;
  uint16_t col;
  uint16_t last_line;
  uint16_t last_col;
  /* LABEL slots in EXTRA rows deferred until emit: (extra_idx, slot, label_name) */
  PyObject *pending_labels;
} CodeCollect;


/**
   Intern a constant into the cpool using object identity.

   Must use identity (pointer equality), not hash/equality, because
   Python's 1 == True and 0 == False would incorrectly merge distinct
   constants. Linear scan of cpool_list; append if not found.
*/
static uint32_t intern_const(CodeCollect *coll, PyObject *value) {

  PyObject *list = coll->cpool_list;
  Py_ssize_t n = PyList_GET_SIZE(list);

  /* Linear scan by identity; do not use dict (1==True, 0==False would merge) */
  for (Py_ssize_t i = 0; i < n; i++) {
    if (PyList_GET_ITEM(list, i) == value) {
      return (uint32_t) i;
    }
  }

  if (PyList_Append(list, value) < 0) {
    return (uint32_t) -1;
  }
  Py_INCREF(value);
  coll->cpool_len = (uint32_t) (n + 1);
  return (uint32_t) n;
}


/* === Label helpers === */


/**
   Record label definition: labels[name] = output_pc. Used in pass 1 so
   JUMP-style opcodes can resolve target pc in emit phase.
*/
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


/**
   Parse POSITION instruction arg: (line, col) or (line,) or (line, col, extra).
   Extracts line and col into out params. Returns -1 on invalid format.
*/
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


/**
   Collect phase: walk input instructions, simulate squashing, build
   label map, intern constants (identity-based), build extra rows.
   Fills CodeCollect with exact sizes for allocation.
*/
static int code_collect(PyObject *self, PyObject *instr, CodeCollect *coll) {

  Py_ssize_t instr_l = PySequence_Fast_GET_SIZE(instr);
  if (instr_l < 0) {
    return -1;
  }

  coll->cpool_list = PyList_New(0);
  coll->labels = PyDict_New();
  coll->pending_labels = PyList_New(0);
  if (! coll->cpool_list || ! coll->labels || ! coll->pending_labels) {
    Py_CLEAR(coll->cpool_list);
    Py_CLEAR(coll->labels);
    Py_CLEAR(coll->pending_labels);
    return -1;
  }

  coll->extra_rows = NULL;
  coll->extra_len = 0;
  coll->extra_cap = 0;
  coll->pos_entries = PyMem_New(KosPosition, 64);
  coll->pos_count = 0;
  coll->pos_cap = 64;
  if (! coll->pos_entries) {
    goto failure;
  }

  coll->code_len = 0;
  coll->cpool_len = 0;
  coll->extent_count = 0;
  coll->line = 0;
  coll->col = 0;
  coll->last_line = 0;
  coll->last_col = 0;

  /* Pass 1: Simulate squashing of LABEL/POSITION so we know output sizes.
     di = destination instruction index (count of instructions that survive).
     LABEL and POSITION are dropped from the final stream; their data goes
     into labels dict and pos_entries. */
  uint32_t di = 0;
  for (Py_ssize_t i = 0; i < instr_l; i++) {
    PyObject *pair = PySequence_Fast_GET_ITEM(instr, i);
    if (! PySequence_Check(pair) || PySequence_Fast_GET_SIZE(pair) < 2) {
      PyErr_SetString(PyExc_RuntimeError,
                      "Code instructions must be pairs of 2");
      goto failure;
    }

    PyObject *a = PySequence_Fast_GET_ITEM(pair, 0);
    PyObject *b = PySequence_Fast_GET_ITEM(pair, 1);
    if (! PyLong_Check(a)) {
      PyErr_Format(PyExc_RuntimeError, "Code instruction undefined %R:%R",
                   a, b);
      goto failure;
    }

    long lop = PyLong_AsLong(a);
    if (lop < (long) KOS_OP_NOOP || lop > (long) KOS_OP_THEEND) {
      PyErr_Format(PyExc_RuntimeError, "Code instruction undefined %R:%R",
                   a, b);
      goto failure;
    }

    KosOpCode op = (KosOpCode) lop;

    if (op == KOS_OP_LABEL) {
      /* Record label name -> output pc; LABEL itself is not emitted */
      if (collect_label(coll->labels, b, di)) {
        goto failure;
      }
      continue;   /* do not advance di */
    }

    if (op == KOS_OP_POSITION) {
      uint16_t line = 0, col = 0;
      if (unpack_position(b, &line, &col)) {
        goto failure;
      }
      coll->line = line;
      coll->col = col;
      /* Emit position entry only when line/col changes (delta encoding) */
      if (line != coll->last_line || col != coll->last_col) {
        if (coll->pos_count >= coll->pos_cap) {
          KosPosition *n = PyMem_Resize(coll->pos_entries, KosPosition,
                                        coll->pos_cap * 2);
          if (! n) {
            PyErr_NoMemory();
            goto failure;
          }
          coll->pos_entries = n;
          coll->pos_cap *= 2;
        }
        coll->pos_entries[coll->pos_count].index = (uint16_t) di;
        coll->pos_entries[coll->pos_count].line = line;
        coll->pos_entries[coll->pos_count].col = col;
        coll->pos_count++;
        coll->last_line = line;
        coll->last_col = col;
      }
      continue;   /* POSITION not emitted as instruction */
    }

    /* Count PUSH_* for extent table sizing; each PUSH_* becomes one extent */
    if (is_push_op(op)) {
      coll->extent_count++;
    }

    di++;
  }

  coll->code_len = di;

  /* Pass 2: Intern constants (identity-based dedup) and build extra rows.
     Walk input again; for each surviving instruction, process CONST/VAR/EXTRA
     into cpool_list and extra_rows. LABEL-style opcodes store pc->name in
     labels for later resolution. */
  di = 0;
  uint32_t extra_idx = 0;
  for (Py_ssize_t i = 0; i < instr_l; i++) {
    PyObject *pair = PySequence_Fast_GET_ITEM(instr, i);
    PyObject *a = PySequence_Fast_GET_ITEM(pair, 0);
    PyObject *b = PySequence_Fast_GET_ITEM(pair, 1);
    KosOpCode op = (KosOpCode) PyLong_AsLong(a);
    KosOpStyle style = opcode_styles[op].style;

    if (op == KOS_OP_LABEL || op == KOS_OP_POSITION) {
      continue;   /* skipped in pass 2; already processed in pass 1 */
    }

    /* EXTRA opcodes: arg is 4-tuple (a0,a1,a2,a3). Schema dictates how
       each slot is interpreted: CONST/VAR -> intern into cpool; LABEL ->
       defer until emit; FAST/COUNT -> store raw. */
    if (style == KOSTYLE_EXTRA) {
      KosExtraSchema schema = kos_opcode_extra_schema(op);
      if (! PyTuple_Check(b)) {
        PyErr_Format(PyExc_RuntimeError,
                     "EXTRA opcode expects tuple argument, got %R", b);
        goto failure;
      }
      Py_ssize_t tlen = PyTuple_GET_SIZE(b);
      if (tlen < 4) {
        PyErr_Format(PyExc_RuntimeError,
                     "EXTRA opcode tuple must have 4 elements, got %zd",
                     tlen);
        goto failure;
      }

      KosExtra row = { 0, 0, 0, 0 };
      for (int slot = 0; slot < 4; slot++) {
        KosOpStyle fk = (slot == 0) ? schema.f0 : (slot == 1) ? schema.f1 :
            (slot == 2) ? schema.f2 : schema.f3;
        PyObject *elem = PyTuple_GET_ITEM(b, slot);

        if (fk == KOSTYLE_NOARG) {
          continue;
        }
        /* CONST: intern value, store cpool index in row slot */
        if (fk == KOSTYLE_CONST) {
          uint32_t idx = intern_const(coll, elem);
          if (idx == (uint32_t) -1) {
            goto failure;
          }
          if (slot == 0) {
            row.a0 = (uint16_t) idx;
          } else if (slot == 1) {
            row.a1 = (uint16_t) idx;
          } else if (slot == 2) {
            row.a2 = (uint16_t) idx;
          } else {
            row.a3 = (uint16_t) idx;
          }
          continue;
        }
        /* VAR: stringify, intern, store cpool index (same as KOSTYLE_VAR) */
        if (fk == KOSTYLE_VAR) {
          PyObject *name = PyObject_Str(elem);
          if (! name) {
            goto failure;
          }
          uint32_t idx = intern_const(coll, name);
          Py_DECREF(name);
          if (idx == (uint32_t) -1) {
            goto failure;
          }
          if (slot == 0) {
            row.a0 = (uint16_t) idx;
          } else if (slot == 1) {
            row.a1 = (uint16_t) idx;
          } else if (slot == 2) {
            row.a2 = (uint16_t) idx;
          } else {
            row.a3 = (uint16_t) idx;
          }
          continue;
        }
        /* LABEL: cannot resolve until pass 1 labels are complete; defer */
        if (fk == KOSTYLE_LABEL) {
          PyObject *pending = PyTuple_Pack(3,
              PyLong_FromLong(extra_idx),
              PyLong_FromLong(slot),
              elem);
          if (! pending || PyList_Append(coll->pending_labels, pending) < 0) {
            Py_XDECREF(pending);
            goto failure;
          }
          Py_DECREF(pending);
          continue;
        }
        /* FASTVAR, COUNT, STAR: store raw integer (slot, argc, bitflags) */
        if (fk == KOSTYLE_COUNT || fk == KOSTYLE_FASTVAR || fk == KOSTYLE_STAR) {
          if (! PyLong_Check(elem)) {
            PyErr_Format(PyExc_RuntimeError,
                         "EXTRA field expects int, got %R", elem);
            goto failure;
          }
          long val = PyLong_AsLong(elem);
          if (val < 0 || val > 65535) {
            PyErr_Format(PyExc_RuntimeError,
                         "EXTRA field value out of range: %ld", val);
            goto failure;
          }
          uint16_t v = (uint16_t) val;
          if (slot == 0) {
            row.a0 = v;
          } else if (slot == 1) {
            row.a1 = v;
          } else if (slot == 2) {
            row.a2 = v;
          } else {
            row.a3 = v;
          }
        }
      }

      /* Grow extra_rows if needed; append this row */
      if (coll->extra_len >= coll->extra_cap) {
        uint32_t new_cap = coll->extra_cap ? coll->extra_cap * 2 : 16;
        KosExtra *n = PyMem_Resize(coll->extra_rows, KosExtra, new_cap);
        if (! n) {
          PyErr_NoMemory();
          goto failure;
        }
        coll->extra_rows = n;
        coll->extra_cap = new_cap;
      }
      coll->extra_rows[coll->extra_len] = row;
      coll->extra_len++;
      extra_idx++;
      di++;
      continue;
    }

    /* CONST: intern arg into cpool; emit phase will look up index */
    if (style == KOSTYLE_CONST) {
      uint32_t idx = intern_const(coll, b);
      if (idx == (uint32_t) -1) {
        goto failure;
      }
      di++;
      continue;
    }

    /* VAR: stringify name, intern into cpool (identity-based) */
    if (style == KOSTYLE_VAR) {
      PyObject *name = PyObject_Str(b);
      if (! name) {
        goto failure;
      }
      uint32_t idx = intern_const(coll, name);
      Py_DECREF(name);
      if (idx == (uint32_t) -1) {
        goto failure;
      }
      di++;
      continue;
    }

    /* LABEL-style (JUMP etc): record instruction index -> label name so emit
       can resolve target pc from labels dict (name -> pc) */
    if (style == KOSTYLE_LABEL) {
      PyObject *key = PyLong_FromLong(di);
      if (! key || PyDict_SetItem(coll->labels, key, b) < 0) {
        Py_XDECREF(key);
        goto failure;
      }
      Py_DECREF(key);
      di++;
      continue;
    }

    di++;
  }

  coll->cpool_len = (uint32_t) PyList_GET_SIZE(coll->cpool_list);
  return 0;

 failure:
  PyMem_Del(coll->pos_entries);
  coll->pos_entries = NULL;
  PyMem_Del(coll->extra_rows);
  coll->extra_rows = NULL;
  Py_CLEAR(coll->cpool_list);
  Py_CLEAR(coll->labels);
  Py_CLEAR(coll->pending_labels);
  return -1;
}


/**
   Release collect-phase resources. Call after code_emit or on failure.
   Does not free the CodeCollect struct itself.
*/
static void code_collect_clear(CodeCollect *coll) {

  Py_CLEAR(coll->cpool_list);
  Py_CLEAR(coll->labels);
  Py_CLEAR(coll->pending_labels);
  PyMem_Del(coll->pos_entries);
  coll->pos_entries = NULL;
  PyMem_Del(coll->extra_rows);
  coll->extra_rows = NULL;
}


/* === Phase 2: Allocate === */


/**
   Allocate a single page-aligned block for the entire bytecode object.
   Sizes come from CodeCollect; no over-allocation. Layout (with alignment
   padding between regions):

     [code][pad][positions][pad][cpool][pad][extra][pad][extents][top_extent_id]

   All offsets use natural alignment: pointer size for cpool, 4-byte for
   KosExtra. Total is rounded up to page size for mprotect.
*/
static int code_alloc_unified(
    CodeCollect *coll,
    void **out_base,
    size_t *out_len,
    KosOp **out_code,
    KosPosition **out_pos,
    PyObject ***out_cpool,
    KosExtra **out_extra,
    KosExtentDesc **out_extents,
    uint16_t **out_top_extent_id) {

  /* Compute region sizes from collect-phase counts (all exact, no oversizing) */
  size_t ptr_size = sizeof(PyObject *);
  size_t code_bytes = sizeof(KosOp) * coll->code_len;
  size_t pos_bytes = sizeof(KosPosition) * coll->pos_count;
  size_t cpool_bytes = sizeof(PyObject *) * coll->cpool_len;
  size_t extra_bytes = sizeof(KosExtra) * coll->extra_len;
  size_t extent_bytes = sizeof(KosExtentDesc) * coll->extent_count;
  size_t top_bytes = sizeof(uint16_t) * coll->code_len;

  /* Compute aligned offsets; each region starts at aligned boundary */
  size_t code_offset = 0;
  size_t pos_offset = ((code_bytes + ptr_size - 1) / ptr_size) * ptr_size;
  size_t cpool_offset = pos_offset +
    ((pos_bytes + ptr_size - 1) / ptr_size) * ptr_size;
  size_t extra_offset = cpool_offset +
    ((cpool_bytes + ptr_size - 1) / ptr_size) * ptr_size;
  size_t extent_offset = extra_offset +
    ((extra_bytes + sizeof(uint32_t) - 1) / sizeof(uint32_t)) * sizeof(uint32_t);
  size_t top_offset = extent_offset + extent_bytes;

  size_t total = top_offset + top_bytes;
  size_t page_size = (size_t) sysconf(_SC_PAGE_SIZE);
  total = ((total + page_size - 1) / page_size) * page_size;

  /* Single allocation for entire block; later mprotect(PROT_READ) once */
  void *base = NULL;
  if (posix_memalign(&base, page_size, total)) {
    PyErr_SetString(PyExc_MemoryError,
                    "Failed to allocate bytecode storage");
    return -1;
  }

  memset(base, 0, total);

  *out_base = base;
  *out_len = total;
  *out_code = (KosOp *) ((char *) base + code_offset);
  *out_pos = (KosPosition *) ((char *) base + pos_offset);
  *out_cpool = (PyObject **) ((char *) base + cpool_offset);
  *out_extra = coll->extra_len ? (KosExtra *) ((char *) base + extra_offset)
      : NULL;
  *out_extents = coll->extent_count
      ? (KosExtentDesc *) ((char *) base + extent_offset) : NULL;
  *out_top_extent_id = coll->extent_count
      ? (uint16_t *) ((char *) base + top_offset) : NULL;

  return 0;
}


/* === Phase 3: Emit === */


/**
   Emit instructions and tables into pre-allocated regions. Walk input
   again; for each instruction, write op/arg to code[]. LABEL and POSITION
   are skipped. Resolve label targets for KOSTYLE_LABEL and pending_labels.
   Copy cpool_list -> cpool, extra_rows -> extra, pos_entries -> pos.
*/
static int code_emit(
    PyObject *self,
    PyObject *instr,
    CodeCollect *coll,
    KosOp *code,
    KosPosition *pos,
    PyObject **cpool,
    KosExtra *extra) {

  Py_ssize_t instr_l = PySequence_Fast_GET_SIZE(instr);
  uint32_t di = 0;
  uint32_t pi = 0;
  uint32_t extra_idx = 0;
  uint16_t last_line = 0, last_col = 0;

  /* Walk input; di = output instruction index, pi = position entry index */
  for (Py_ssize_t i = 0; i < instr_l; i++) {
    PyObject *pair = PySequence_Fast_GET_ITEM(instr, i);
    PyObject *a = PySequence_Fast_GET_ITEM(pair, 0);
    PyObject *b = PySequence_Fast_GET_ITEM(pair, 1);
    KosOpCode op = (KosOpCode) PyLong_AsLong(a);
    KosOpStyle style = opcode_styles[op].style;

    /* LABEL/POSITION were squashed in collect; skip them here */
    if (op == KOS_OP_LABEL || op == KOS_OP_POSITION) {
      if (op == KOS_OP_POSITION) {
        uint16_t l = 0, c = 0;
        unpack_position(b, &l, &c);
        if (l != last_line || c != last_col) {
          last_line = l;
          last_col = c;
        }
      }
      continue;
    }

    code[di].op = (uint16_t) op;
    code[di].arg = 0;

    /* Resolve arg from collect-phase data: cpool index, label pc, or raw */
    if (style == KOSTYLE_CONST) {
      Py_ssize_t n = PyList_GET_SIZE(coll->cpool_list);
      for (Py_ssize_t j = 0; j < n; j++) {
        if (PyList_GET_ITEM(coll->cpool_list, j) == b) {
          code[di].arg = (uint16_t) j;
          break;
        }
      }
    } else if (style == KOSTYLE_VAR) {
      PyObject *name = PyObject_Str(b);
      if (! name) {
        return -1;
      }
      Py_ssize_t n = PyList_GET_SIZE(coll->cpool_list);
      for (Py_ssize_t j = 0; j < n; j++) {
        if (PyList_GET_ITEM(coll->cpool_list, j) == name) {
          code[di].arg = (uint16_t) j;
          break;
        }
      }
      Py_DECREF(name);
    } else if (style == KOSTYLE_LABEL) {
      /* Look up target pc from labels dict (label_name -> pc) */
      PyObject *dest = PyDict_GetItem(coll->labels, b);
      if (! dest) {
        PyErr_Format(PyExc_RuntimeError, "opcode jump label missing %R", b);
        return -1;
      }
      code[di].arg = (uint16_t) PyLong_AsLong(dest);
    } else if (style == KOSTYLE_EXTRA) {
      /* Arg is index into extra table; copy row from collect */
      code[di].arg = (uint16_t) extra_idx;
      if (extra) {
        memcpy(&extra[extra_idx], &coll->extra_rows[extra_idx],
               sizeof(KosExtra));
      }
      extra_idx++;
    } else {
      /* COUNT, NOARG, etc: use raw integer from b */
      if (PyLong_Check(b)) {
        code[di].arg = (uint16_t) PyLong_AsLong(b);
      }
    }

    /* Record YIELDS flag for generators */
    if (op == KOS_OP_YIELD || op == KOS_OP_YIELD_FROM) {
      KosCode_FLAGS(self) |= KOS_CODE_YIELDS;
    }

    /* Emit position entry when this instruction has a position change */
    if (pi < coll->pos_count && coll->pos_entries[pi].index == di) {
      pos[pi] = coll->pos_entries[pi];
      pi++;
    }

    di++;
  }

  /* Transfer cpool: INCREF each item, store in pre-allocated array */
  Py_ssize_t cpool_n = PyList_GET_SIZE(coll->cpool_list);
  for (Py_ssize_t j = 0; j < cpool_n; j++) {
    PyObject *val = PyList_GET_ITEM(coll->cpool_list, j);
    Py_INCREF(val);
    cpool[j] = val;
  }

  /* Resolve LABEL placeholders in extra rows: replace name with target pc */
  if (extra) {
    Py_ssize_t n_pending = PyList_GET_SIZE(coll->pending_labels);
    for (Py_ssize_t k = 0; k < n_pending; k++) {
      PyObject *pending = PyList_GET_ITEM(coll->pending_labels, k);
      long eidx = PyLong_AsLong(PyTuple_GET_ITEM(pending, 0));
      long slot = PyLong_AsLong(PyTuple_GET_ITEM(pending, 1));
      PyObject *label_name = PyTuple_GET_ITEM(pending, 2);
      PyObject *dest = PyDict_GetItem(coll->labels, label_name);
      if (! dest) {
        PyErr_Format(PyExc_RuntimeError, "opcode jump label missing %R",
                     label_name);
        return -1;
      }
      uint16_t pc = (uint16_t) PyLong_AsLong(dest);
      KosExtra *row = &extra[eidx];
      if (slot == 0) {
        row->a0 = pc;
      } else if (slot == 1) {
        row->a1 = pc;
      } else if (slot == 2) {
        row->a2 = pc;
      } else {
        row->a3 = pc;
      }
    }
  }

  return 0;
}




/* === Extent derivation === */

/*
   Extent table: maps PUSH/POP block structure to a flat table.
   Each PUSH_* opens an extent; matching POP_* closes it. top_extent_id[pc]
   gives the innermost extent id at each instruction for jump locality checks.
*/


typedef struct {
  KosOpCode push_op;
  KosOpCode pop_op;
  KosExtentKind kind;
} ExtentOpPair;


/* PUSH_* -> POP_* and extent kind; source of truth for extent op classification */
static const ExtentOpPair extent_pairs[] = {
  { KOS_OP_PUSH_TRY,      KOS_OP_POP_TRY,      KOS_EXT_TRY },
  { KOS_OP_PUSH_LOOP,     KOS_OP_POP_LOOP,     KOS_EXT_LOOP },
  { KOS_OP_PUSH_FINALLY,  KOS_OP_POP_FINALLY,  KOS_EXT_FINALLY },
  { KOS_OP_PUSH_PROMPT,   KOS_OP_POP_PROMPT,   KOS_EXT_PROMPT },
  { KOS_OP_PUSH_SHIFT,    KOS_OP_POP_SHIFT,    KOS_EXT_SHIFT },
  { KOS_OP_PUSH_CC,       KOS_OP_POP_CC,       KOS_EXT_CC },
  { KOS_OP_PUSH_WITH,     KOS_OP_POP_WITH,     KOS_EXT_WITH },
  { KOS_OP_PUSH_DYN_WITH, KOS_OP_POP_DYN_WITH, KOS_EXT_DYN_WITH },
  { KOS_OP_PUSH_NS,       KOS_OP_POP_NS,       KOS_EXT_NS },
  { KOS_OP_PUSH_COLLECT,  KOS_OP_POP_COLLECT,  KOS_EXT_COLLECT },
  { KOS_OP_PUSH_WIND,     KOS_OP_POP_WIND,     KOS_EXT_WIND },
  { KOS_OP_PUSH_FOREACH,  KOS_OP_POP_FOREACH,  KOS_EXT_FOREACH },
  { KOS_OP_PUSH_PARAM,    KOS_OP_POP_PARAM,    KOS_EXT_PARAM },
};


#define N_EXTENT_PAIRS \
  (sizeof extent_pairs / sizeof extent_pairs[0])


static inline int is_push_op(KosOpCode op) {

  for (size_t i = 0; i < N_EXTENT_PAIRS; i++) {
    if (extent_pairs[i].push_op == op) {
      return 1;
    }
  }
  return 0;
}


static inline int is_pop_op(KosOpCode op) {

  for (size_t i = 0; i < N_EXTENT_PAIRS; i++) {
    if (extent_pairs[i].pop_op == op) {
      return 1;
    }
  }
  return 0;
}


static inline KosOpCode pop_for_push(KosOpCode push_op) {

  for (size_t i = 0; i < N_EXTENT_PAIRS; i++) {
    if (extent_pairs[i].push_op == push_op) {
      return extent_pairs[i].pop_op;
    }
  }
  return KOS_OP_NOOP;
}


static inline KosExtentKind kind_for_push(KosOpCode push_op) {

  for (size_t i = 0; i < N_EXTENT_PAIRS; i++) {
    if (extent_pairs[i].push_op == push_op) {
      return extent_pairs[i].kind;
    }
  }
  return 0;
}


/* Stack entry for simulating PUSH/POP during extent derivation */
typedef struct {
  uint32_t push_pc;
  uint32_t extent_id;
  KosOpCode push_op;
} ExtentStackEntry;


/**
   Validate extent structure (push/pop matching) without allocating.
   Catches POP with no PUSH, extent mismatch, unclosed extent.
   Call when extent_count == 0 to reject code that has POP_* but no PUSH_*.
*/
static int validate_extents(KosOp *code, uint32_t code_len) {

  ExtentStackEntry stack[DERIVE_EXTENT_STACK_MAX];
  uint32_t stack_top = 0;

  /* Simulate PUSH/POP stack; error on POP with empty stack or mismatch */
  for (uint32_t pc = 0; pc < code_len; pc++) {
    KosOpCode op = (KosOpCode) code[pc].op;

    if (is_push_op(op)) {
      /* PUSH: push onto stack */
      if (stack_top >= DERIVE_EXTENT_STACK_MAX) {
        PyErr_SetString(PyExc_RuntimeError, "extent nesting too deep");
        return -1;
      }
      stack[stack_top].push_pc = pc;
      stack[stack_top].push_op = op;
      stack_top++;
    }
    else if (is_pop_op(op)) {
      /* POP: must match top of stack */
      if (stack_top == 0) {
        PyErr_Format(PyExc_RuntimeError,
                     "POP at pc %u with no matching PUSH", pc);
        return -1;
      }
      KosOpCode expected_pop = pop_for_push(stack[stack_top - 1].push_op);
      if (op != expected_pop) {
        PyErr_Format(PyExc_RuntimeError,
                     "extent mismatch at pc %u: expected %d, got %d",
                     pc, (int) expected_pop, (int) op);
        return -1;
      }
      stack_top--;
    }
  }

  if (stack_top != 0) {
    PyErr_Format(PyExc_RuntimeError, "unclosed extent at pc %u",
                 stack[stack_top - 1].push_pc);
    return -1;
  }

  return 0;
}


/**
   Fill extent table and top_extent_id from instruction stream. Walk code;
   on PUSH_* allocate extent descriptor, push onto stack; on POP_* close
   extent, propagate top_extent_id to all pcs inside the block.
   Call when extent_count > 0; buffers are pre-allocated in unified block.
*/
static int derive_extents_fill(
    KosOp *code,
    uint32_t code_len,
    KosExtentDesc *extents_out,
    uint32_t extent_count,
    uint16_t *top_extent_id_out) {

  ExtentStackEntry stack[DERIVE_EXTENT_STACK_MAX];
  uint32_t stack_top = 0;
  uint32_t extent_idx = 0;

  /* Initialize all pcs to "no extent" */
  for (uint32_t i = 0; i < code_len; i++) {
    top_extent_id_out[i] = KOS_EXTENT_NONE;
  }

  for (uint32_t pc = 0; pc < code_len; pc++) {
    KosOpCode op = (KosOpCode) code[pc].op;

    if (is_push_op(op)) {
      if (stack_top >= DERIVE_EXTENT_STACK_MAX) {
        PyErr_SetString(PyExc_RuntimeError,
                        "extent nesting too deep");
        return -1;
      }
      if (extent_idx >= extent_count) {
        PyErr_SetString(PyExc_RuntimeError, "extent count mismatch");
        return -1;
      }

      /* Build extent descriptor; parent is current stack top */
      uint32_t parent_id = KOS_EXTENT_NONE;
      if (stack_top > 0) {
        parent_id = stack[stack_top - 1].extent_id;
      }

      KosExtentDesc *e = &extents_out[extent_idx];
      e->kind = kind_for_push(op);
      e->push_pc = pc;
      e->pop_pc = 0;
      e->parent_extent_id = parent_id;
      e->flags = 0;

      stack[stack_top].push_pc = pc;
      stack[stack_top].extent_id = extent_idx;
      stack[stack_top].push_op = op;
      top_extent_id_out[pc] = (uint16_t) extent_idx;  /* PUSH pc is inside */
      stack_top++;
      extent_idx++;
    }
    else if (is_pop_op(op)) {
      if (stack_top == 0) {
        PyErr_Format(PyExc_RuntimeError,
                     "POP at pc %u with no matching PUSH", pc);
        return -1;
      }

      KosOpCode expected_pop = pop_for_push(stack[stack_top - 1].push_op);
      if (op != expected_pop) {
        PyErr_Format(PyExc_RuntimeError,
                     "extent mismatch at pc %u: expected %d, got %d",
                     pc, (int) expected_pop, (int) op);
        return -1;
      }

      uint32_t eid = stack[stack_top - 1].extent_id;
      extents_out[eid].pop_pc = pc;

      /* Sanity: push must precede pop */
      if (extents_out[eid].push_pc >= extents_out[eid].pop_pc) {
        PyErr_Format(PyExc_RuntimeError,
                     "invalid extent range: push_pc %u >= pop_pc %u",
                     extents_out[eid].push_pc, extents_out[eid].pop_pc);
        return -1;
      }

      /* Propagate extent id to all pcs inside [push_pc+1, pop_pc) */
      uint32_t enter_pc = extents_out[eid].push_pc + 1;
      for (uint32_t j = enter_pc; j < pc; j++) {
        if (top_extent_id_out[j] == KOS_EXTENT_NONE) {
          top_extent_id_out[j] = (uint16_t) eid;
        }
      }

      stack_top--;
    }
  }

  if (stack_top != 0) {
    PyErr_Format(PyExc_RuntimeError,
                 "unclosed extent at pc %u",
                 stack[stack_top - 1].push_pc);
    return -1;
  }

  return 0;
}


/**
   Opcodes that perform control-flow jumps (must stay local).
   Excludes PUSH_* whose args are handler targets that may legitimately
   point outside the current block.
*/
static int is_jump_op(KosOpCode op) {

  return (kos_opcode_style(op) == KOSTYLE_LABEL
          && !is_push_op(op));
}


/**
   Validate JUMP locality: all jump targets must be inside the source block
   or at the block's pop_pc (leaving). Called after derive_extents.
   Only applies to control-flow jump opcodes, not PUSH_*.
*/
static int validate_jump_locality(
    KosOp *code,
    uint32_t code_len,
    KosExtentDesc *extents,
    uint32_t extent_len,
    uint16_t *top_extent_id) {

  if (! top_extent_id || extent_len == 0) {
    return 0;
  }

  /* For each control-flow jump, target must be inside or at boundary of
     source extent. PUSH_* handler args may jump out; JUMP/JUMP_TRUE etc
     must stay local. */
  for (uint32_t src_pc = 0; src_pc < code_len; src_pc++) {
    if (! is_jump_op((KosOpCode) code[src_pc].op)) {
      continue;
    }

    uint32_t t = code[src_pc].arg;
    if (t >= code_len) {
      PyErr_Format(PyExc_RuntimeError,
                   "JUMP at pc %u targets out-of-range pc %u",
                   src_pc, t);
      return -1;
    }

    uint32_t eid = top_extent_id[src_pc];
    if (eid == KOS_EXTENT_NONE) {
      continue;
    }

    if (eid >= extent_len) {
      continue;
    }

    KosExtentDesc *e = &extents[eid];
    uint32_t enter_pc = e->push_pc + 1;
    uint32_t pop_pc = e->pop_pc;

    if (enter_pc <= t && t < pop_pc) {
      continue;
    }
    if (t == pop_pc) {
      continue;
    }

    PyErr_Format(PyExc_RuntimeError,
                 "non-local JUMP at pc %u to pc %u: target outside block"
                 " [%u, %u)",
                 src_pc, t, enter_pc, pop_pc);
    return -1;
  }

  return 0;
}


/**
   Validate ITER_NEXT is only used inside a PUSH_FOREACH block extent.
   Called after derive_extents.
*/
static int validate_iter_next_in_foreach(
    KosOp *code,
    uint32_t code_len,
    KosExtentDesc *extents,
    uint32_t extent_len,
    uint16_t *top_extent_id) {

  if (! top_extent_id) {
    return 0;
  }

  /* ITER_NEXT must be inside a PUSH_FOREACH; walk parent chain to find */
  for (uint32_t pc = 0; pc < code_len; pc++) {
    if ((KosOpCode) code[pc].op != KOS_OP_ITER_NEXT) {
      continue;
    }
    uint32_t eid = top_extent_id[pc];
    int found = 0;
    while (eid != KOS_EXTENT_NONE && eid < extent_len) {
      if (extents[eid].kind == KOS_EXT_FOREACH) {
        found = 1;
        break;
      }
      eid = extents[eid].parent_extent_id;
    }
    if (! found) {
      PyErr_Format(PyExc_RuntimeError,
                   "ITER_NEXT at pc %u must be inside a PUSH_FOREACH block",
                   pc);
      return -1;
    }
  }

  return 0;
}


/**
   Reject streams that mix yield with tail-call opcodes. Generators must
   not use tail-call frame reuse; the compiler demotes TAIL_* to CALL_* when
   yields are present.

   :param instr_fast: result of PySequence_Fast on the instruction list

   :return: 0 if ok, -1 with ValueError set if both yields and tail calls
*/
static int validate_no_tail_with_yield(PyObject *instr_fast) {

  int saw_yield = 0;
  int saw_tail = 0;
  Py_ssize_t instr_l = PySequence_Fast_GET_SIZE(instr_fast);

  for (Py_ssize_t i = 0; i < instr_l; i++) {
    PyObject *pair = PySequence_Fast_GET_ITEM(instr_fast, i);
    if (! PySequence_Check(pair) || PySequence_Fast_GET_SIZE(pair) < 2) {
      continue;
    }

    PyObject *a = PySequence_Fast_GET_ITEM(pair, 0);
    if (! PyLong_Check(a)) {
      continue;
    }

    long lop = PyLong_AsLong(a);
    if (lop == KOS_OP_YIELD || lop == KOS_OP_YIELD_FROM) {
      saw_yield = 1;
    } else if (lop == KOS_OP_TAIL_CALL || lop == KOS_OP_TAIL_CALL_NAMES
               || lop == KOS_OP_TAIL_CALL_STAR) {
      saw_tail = 1;
    }

    if (saw_yield && saw_tail) {
      PyErr_SetString(PyExc_ValueError,
                      "code containing yield or yield-from cannot use "
                      "TAIL_CALL, TAIL_CALL_NAMES, or TAIL_CALL_STAR");
      return -1;
    }
  }

  return 0;
}


/**
   FAST slot count for a NamespaceSpec.

   Matches ``convert_fast`` / compiler ordering: locals, then closures, then
   globals. Indices 0 .. limit-1 are the only valid FASTVAR operands.

   :param nspec: NamespaceSpec instance
   :param out_limit: set to ``len(local)+len(closure)+len(global)``

   :return: 0 on success, -1 on error
*/
static int namespace_spec_fast_slot_limit(
    PyObject *nspec,
    uint32_t *out_limit) {

  PyObject *loc = KosNamespaceSpec_LOCAL_SYMS(nspec);
  PyObject *clo = KosNamespaceSpec_CLOSURE_SYMS(nspec);
  PyObject *glo = KosNamespaceSpec_GLOBAL_SYMS(nspec);

  if (! loc || ! clo || ! glo) {
    return -1;
  }

  Py_ssize_t nloc = PySequence_Size(loc);
  Py_ssize_t nclo = PySequence_Size(clo);
  Py_ssize_t nglo = PySequence_Size(glo);

  if (nloc < 0 || nclo < 0 || nglo < 0) {
    return -1;
  }

  Py_ssize_t total = nloc + nclo + nglo;

  if (total > (Py_ssize_t) 0xFFFFFFFFu) {
    PyErr_SetString(PyExc_ValueError, "namespace symbol count too large");
    return -1;
  }

  *out_limit = (uint32_t) total;
  return 0;
}


/**
   Reject FASTVAR operands outside the active NamespaceSpec slot range.

   Called after emit when ``code`` and ``extra`` are fully materialized so we
   can validate both scalar FAST opcodes and FAST slots embedded in EXTRA rows
   (CALL_FAST-style operands). Mirrors ``Compiler.convert_fast``: walk the
   instruction stream in order, track pending ``NamespaceSpec`` from ``CONST``
   (for ``PUSH_COLLECT`` fallback), read ``PUSH_NS`` nspec from the EXTRA row,
   push/pop slot limits on ``PUSH_NS`` / ``PUSH_COLLECT`` / ``POP_NS`` /
   ``POP_COLLECT``. The active limit is the root ``Code`` NamespaceSpec when
   the namespace stack is empty, else the innermost pushed extent.

   :param self: partially built KosCode (needs ``namespace_spec`` set)
   :param code: emitted instruction array, length ``code_len``
   :param extra: emitted EXTRA table (may be NULL if ``extra_len==0``)
   :param extra_len: row count of ``extra``
   :param cpool: constant pool (``NamespaceSpec`` lookup for namespace ops)
   :param cpool_len: number of entries in ``cpool``

   :return: 0 if all FAST indices satisfy ``0 <= idx < active_limit``, else -1
      with ValueError or RuntimeError set
*/
static int validate_fast_slots(
    PyObject *self,
    KosOp *code,
    uint32_t code_len,
    KosExtra *extra,
    uint32_t extra_len,
    PyObject **cpool,
    uint32_t cpool_len) {

  uint32_t root_limit = 0;

  if (namespace_spec_fast_slot_limit(KosCode_NSPEC(self), &root_limit)) {
    return -1;
  }

  uint32_t ns_stack[DERIVE_EXTENT_STACK_MAX];
  int depth = 0;
  /* Borrowed ref: last CONST NamespaceSpec (for PUSH_COLLECT fallback) */
  PyObject *pending = NULL;

  for (uint32_t i = 0; i < code_len; i++) {
    KosOpCode op = (KosOpCode) code[i].op;

    if (op > KOS_OP_THEEND) {
      PyErr_Format(PyExc_RuntimeError, "invalid opcode %u", (unsigned) op);
      return -1;
    }

    KosOpStyle st = opcode_styles[op].style;

    uint32_t active = (depth == 0) ? root_limit : (uint32_t) ns_stack[depth - 1];

    if (st == KOSTYLE_FASTVAR) {
      /* op.arg is the fast slot index */
      if (active == 0 || code[i].arg >= active) {
        PyErr_Format(PyExc_ValueError,
                     "FAST index %u out of bounds (limit %u)",
                     (unsigned) code[i].arg, (unsigned) active);
        return -1;
      }
    } else if (st == KOSTYLE_EXTRA) {
      /* Walk schema: only slots tagged FASTVAR are checked */
      uint16_t eidx = code[i].arg;
      if (eidx >= extra_len) {
        PyErr_SetString(PyExc_RuntimeError, "EXTRA index out of bounds");
        return -1;
      }

      KosExtra row = extra[eidx];
      KosExtraSchema schema = kos_opcode_extra_schema(op);
      uint16_t raw[4] = { row.a0, row.a1, row.a2, row.a3 };

      for (int slot = 0; slot < 4; slot++) {
        KosOpStyle fk = (slot == 0) ? schema.f0 : (slot == 1) ? schema.f1 :
            (slot == 2) ? schema.f2 : schema.f3;

        if (fk != KOSTYLE_FASTVAR) {
          continue;
        }

        if (active == 0 || raw[slot] >= active) {
          PyErr_Format(PyExc_ValueError,
                       "FAST index %u out of bounds (limit %u)",
                       (unsigned) raw[slot], (unsigned) active);
          return -1;
        }
      }
    }

    /* Advance namespace stack (mirror Compiler.convert_fast) */
    if (op == KOS_OP_CONST && st == KOSTYLE_CONST) {
      if (code[i].arg >= cpool_len) {
        PyErr_SetString(PyExc_RuntimeError,
                        "CONST operand index out of bounds");
        return -1;
      }
      PyObject *val = cpool[code[i].arg];
      if (KosNamespaceSpec_CheckExact(val)) {
        pending = val;
      }
    } else if (op == KOS_OP_PUSH_NS) {
      /* Nspec cpool index is extra row a0 (schema CONST, CONST, NOARG, NOARG). */
      uint16_t eidx = code[i].arg;
      if (eidx >= extra_len) {
        PyErr_SetString(PyExc_RuntimeError, "EXTRA index out of bounds");
        return -1;
      }
      KosExtra row = extra[eidx];
      uint32_t ci = row.a0;
      if (ci >= cpool_len) {
        PyErr_SetString(PyExc_RuntimeError,
                        "CONST operand index out of bounds");
        return -1;
      }
      PyObject *ns = cpool[ci];
      if (ns && KosNamespaceSpec_CheckExact(ns)) {
        uint32_t lim = 0;
        if (namespace_spec_fast_slot_limit(ns, &lim)) {
          return -1;
        }
        if (depth >= DERIVE_EXTENT_STACK_MAX) {
          PyErr_SetString(PyExc_ValueError, "namespace stack overflow");
          return -1;
        }
        ns_stack[depth++] = lim;
      }
      pending = NULL;
    } else if (op == KOS_OP_PUSH_COLLECT) {
      /* Operand is cpool index of NamespaceSpec, or fall back to pending */
      PyObject *ns = NULL;
      if (code[i].arg < cpool_len) {
        PyObject *v = cpool[code[i].arg];
        if (KosNamespaceSpec_CheckExact(v)) {
          ns = v;
        }
      }
      if (! ns) {
        ns = pending;
      }
      if (ns && KosNamespaceSpec_CheckExact(ns)) {
        uint32_t lim = 0;
        if (namespace_spec_fast_slot_limit(ns, &lim)) {
          return -1;
        }
        if (depth >= DERIVE_EXTENT_STACK_MAX) {
          PyErr_SetString(PyExc_ValueError, "namespace stack overflow");
          return -1;
        }
        ns_stack[depth++] = lim;
      }
      pending = NULL;
    } else if (op == KOS_OP_POP_NS || op == KOS_OP_POP_COLLECT) {
      /* Do not error on underflow; extent validation reports POP without PUSH */
      if (depth > 0) {
        depth--;
      }
    }
  }

  return 0;
}


/* === Code initialization === */


/**
   Initialize KosCode from instruction stream. Pipeline:
   1. Collect: simulate squashing, build label map, intern constants,
      build extra rows. Yields exact sizes (code_len, pos_count, cpool_len,
      extra_len, extent_count).
   2. Allocate: single posix_memalign block for all regions.
   3. Emit: write instructions, positions, cpool, extra into block.
   4. Finalize: derive_extents_fill or validate_extents; validate jumps;
      mprotect(PROT_READ); set KosCode fields.

   Invariant: instruction streams that contain yield or yield-from must not
   contain TAIL_CALL, TAIL_CALL_NAMES, or TAIL_CALL_STAR (ValueError).
*/
static int code_instr_init(PyObject *self, PyObject *instr) {

  SAYF("in code_instr_init(%R, %R)", self, instr);

  Py_ssize_t _instr_l = PySequence_Fast_GET_SIZE(instr);
  if (_instr_l < 0) {
    return -1;
  }
  if (_instr_l == 0) {
    PyErr_SetString(PyExc_ValueError,
                    "Code must have at least one instruction"
                    " (e.g. RETURN to return None)");
    return -1;
  }

  PyObject *instr_fast = PySequence_Fast(instr, "instructions must be sequence");
  if (! instr_fast) {
    return -1;
  }

  if (validate_no_tail_with_yield(instr_fast)) {
    Py_DECREF(instr_fast);
    return -1;
  }

  /* Phase 1: Collect — derive exact sizes, build label map, cpool, extra rows */
  CodeCollect coll = { 0 };
  if (code_collect(self, instr_fast, &coll)) {
    Py_DECREF(instr_fast);
    return -1;
  }

  uint32_t code_len = coll.code_len;
  uint32_t pos_count = coll.pos_count;
  uint32_t cpool_len = coll.cpool_len;
  uint32_t extra_len = coll.extra_len;
  uint32_t extent_count = coll.extent_count;

  /* Phase 2: Allocate — single block for all regions */
  void *base = NULL;
  size_t alloc_size = 0;
  KosOp *code = NULL;
  KosPosition *pos = NULL;
  PyObject **cpool = NULL;
  KosExtra *extra = NULL;
  KosExtentDesc *extents = NULL;
  uint16_t *top_extent_id = NULL;

  if (code_alloc_unified(&coll, &base, &alloc_size,
                         &code, &pos, &cpool, &extra,
                         &extents, &top_extent_id)) {
    code_collect_clear(&coll);
    Py_DECREF(instr_fast);
    return -1;
  }

  /* Phase 3: Emit — write instructions and tables into block */
  if (code_emit(self, instr_fast, &coll, code, pos, cpool, extra)) {
    code_collect_clear(&coll);
    mprotect(base, alloc_size, PROT_WRITE);
    free(base);
    Py_DECREF(instr_fast);
    return -1;
  }

  /* Before sealing: ensure FAST indices match active NamespaceSpec per extent */
  if (validate_fast_slots(self, code, code_len, extra, extra_len,
                          cpool, cpool_len)) {
    code_collect_clear(&coll);
    mprotect(base, alloc_size, PROT_WRITE);
    free(base);
    Py_DECREF(instr_fast);
    return -1;
  }

  code_collect_clear(&coll);
  Py_DECREF(instr_fast);

  /* Phase 4: Finalize — fill extent table, validate, mprotect */
  if (extent_count > 0) {
    if (derive_extents_fill(code, code_len, extents, extent_count,
                            top_extent_id)) {
      mprotect(base, alloc_size, PROT_WRITE);
      free(base);
      return -1;
    }
  }
  else if (validate_extents(code, code_len)) {
    /* No PUSH_* but has POP_*: reject malformed bytecode */
    mprotect(base, alloc_size, PROT_WRITE);
    free(base);
    return -1;
  }

  if (extents && top_extent_id &&
      validate_jump_locality(code, code_len, extents, extent_count,
                             top_extent_id)) {
    mprotect(base, alloc_size, PROT_WRITE);
    free(base);
    return -1;
  }

  if (extents && top_extent_id &&
      validate_iter_next_in_foreach(code, code_len, extents,
                                    extent_count, top_extent_id)) {
    mprotect(base, alloc_size, PROT_WRITE);
    free(base);
    return -1;
  }

  /* Seal block read-only; single mprotect for entire allocation */
  if (mprotect(base, alloc_size, PROT_READ)) {
    mprotect(base, alloc_size, PROT_WRITE);
    free(base);
    PyErr_SetFromErrno(PyExc_OSError);
    return -1;
  }

  /* Attach block sub-regions to KosCode */
  KosCode_ALLOCATION(self) = alloc_size;
  KosCode_CODE_LEN(self) = code_len;
  KosCode_CODE(self) = code;
  KosCode_POS_LEN(self) = pos_count;
  KosCode_POS(self) = pos;
  KosCode_CPOOL_LEN(self) = cpool_len;
  KosCode_CPOOL(self) = cpool;
  KosCode_EXTRA_LEN(self) = extra_len;
  KosCode_EXTRA(self) = extra;
  KosCode_EXTENT_LEN(self) = extent_count;
  KosCode_EXTENTS(self) = extents;
  KosCode_TOP_EXTENT_ID(self) = top_extent_id;

  return 0;
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
  Py_VISIT(KosCode_FAUX(self));
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
  Py_CLEAR(KosCode_FAUX(self));
  Py_CLEAR(KosCode_EXTRAS(self));

  for (uint32_t i = KosCode_CPOOL_LEN(self); i--; ) {
    Py_DECREF(KosCode_GET_CONST(self, i));
  }

  void *base = KosCode_CODE(self);
  if (base) {
    mprotect(base, KosCode_ALLOCATION(self), PROT_WRITE);
    free(base);
  }

  KosCode_ALLOCATION(self) = 0;
  KosCode_CODE_LEN(self) = 0;
  KosCode_CODE(self) = NULL;
  KosCode_POS_LEN(self) = 0;
  KosCode_POS(self) = NULL;
  KosCode_CPOOL_LEN(self) = 0;
  KosCode_CPOOL(self) = NULL;
  KosCode_EXTRA_LEN(self) = 0;
  KosCode_EXTRA(self) = NULL;
  KosCode_EXTENT_LEN(self) = 0;
  KosCode_EXTENTS(self) = NULL;
  KosCode_TOP_EXTENT_ID(self) = NULL;

  return 0;
}


static void code_dealloc(PyObject *self) {

  Py_CLEAR(KosCode_NAME(self));
  Py_CLEAR(KosCode_FILENAME(self));
  Py_CLEAR(KosCode_NSPEC(self));
  Py_CLEAR(KosCode_FAUX(self));
  Py_CLEAR(KosCode_EXTRAS(self));

  for (uint32_t i = KosCode_CPOOL_LEN(self); i--; ) {
    Py_DECREF(KosCode_GET_CONST(self, i));
  }

  void *base = KosCode_CODE(self);
  if (base) {
    mprotect(base, KosCode_ALLOCATION(self), PROT_WRITE);
    free(base);
  }

  KosCode_ALLOCATION(self) = 0;
  KosCode_CODE_LEN(self) = 0;
  KosCode_CODE(self) = NULL;
  KosCode_POS_LEN(self) = 0;
  KosCode_POS(self) = NULL;
  KosCode_CPOOL_LEN(self) = 0;
  KosCode_CPOOL(self) = NULL;
  KosCode_EXTRA_LEN(self) = 0;
  KosCode_EXTRA(self) = NULL;
  KosCode_EXTENT_LEN(self) = 0;
  KosCode_EXTENTS(self) = NULL;
  KosCode_TOP_EXTENT_ID(self) = NULL;

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

  KosCode_EXTENT_LEN(self) = 0;
  KosCode_EXTENTS(self) = NULL;
  KosCode_TOP_EXTENT_ID(self) = NULL;

  KosCode_EXTRA_LEN(self) = 0;
  KosCode_EXTRA(self) = NULL;

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


int kos_loc_lookup(PyObject *code, uint32_t pc, uint16_t *line, uint16_t *col) {

  if (! code || ! KosCode_Check(code) || ! line || ! col) {
    if (line) {
      *line = 0;
    }
    if (col) {
      *col = 0;
    }
    return -1;
  }

  return KosCode_GetPosition(code, pc, line, col);
}


uint32_t kos_extent_top(PyObject *code, uint32_t pc) {

  if (! code || ! KosCode_Check(code)) {
    return KOS_EXTENT_NONE;
  }

  uint16_t *top = KosCode_TOP_EXTENT_ID(code);
  if (! top || pc >= KosCode_CODE_LEN(code)) {
    return KOS_EXTENT_NONE;
  }

  uint32_t eid = top[pc];
  if (eid == KOS_EXTENT_NONE) {
    return KOS_EXTENT_NONE;
  }
  return eid;
}


uint32_t kos_extent_find(PyObject *code, uint32_t pc, KosExtentKind kind) {

  uint32_t eid = kos_extent_top(code, pc);
  KosExtentDesc *extents = KosCode_EXTENTS(code);
  uint32_t extent_len = KosCode_EXTENT_LEN(code);

  while (eid != KOS_EXTENT_NONE && eid < extent_len) {
    if (extents[eid].kind == kind) {
      return eid;
    }
    eid = extents[eid].parent_extent_id;
  }

  return KOS_EXTENT_NONE;
}


uint32_t kos_extent_find_by_pop_pc(PyObject *code, uint32_t pop_pc,
    KosExtentKind kind) {

  if (! code || ! KosCode_Check(code)) {
    return KOS_EXTENT_NONE;
  }

  KosExtentDesc *extents = KosCode_EXTENTS(code);
  uint32_t extent_len = KosCode_EXTENT_LEN(code);

  for (uint32_t eid = 0; eid < extent_len; eid++) {
    if (extents[eid].kind == kind && extents[eid].pop_pc == pop_pc) {
      return eid;
    }
  }
  return KOS_EXTENT_NONE;
}


KosExtentDesc *kos_extent_desc(PyObject *code, uint32_t extent_id) {

  if (! code || ! KosCode_Check(code)) {
    return NULL;
  }

  if (extent_id >= KosCode_EXTENT_LEN(code)) {
    return NULL;
  }

  return &KosCode_EXTENTS(code)[extent_id];
}


int kos_try_handler_for(PyObject *code, uint32_t pc, KosTryHandler *out) {

  if (! code || ! KosCode_Check(code) || ! out) {
    SAY("[extent] kos_try_handler_for bad args\n");
    return -1;
  }

  uint32_t eid_try = kos_extent_find(code, pc, KOS_EXT_TRY);
  uint32_t eid = eid_try;
  if (eid == KOS_EXTENT_NONE) {
    eid = kos_extent_find(code, pc, KOS_EXT_FINALLY);
  }

  if (eid == KOS_EXTENT_NONE) {
    SAYF("[extent] kos_try_handler_for pc=%u eid_try=%u no extent\n",
         (unsigned) pc, (unsigned) eid_try);
    return -1;
  }

  KosExtentDesc *e = kos_extent_desc(code, eid);
  if (! e) {
    SAYF("[extent] kos_try_handler_for pc=%u eid=%u desc NULL\n",
         (unsigned) pc, (unsigned) eid);
    return -1;
  }

  SAYF("[extent] kos_try_handler_for pc=%u eid_try=%u eid=%u kind=%d\n",
       (unsigned) pc, (unsigned) eid_try, (unsigned) eid, (int) e->kind);

  out->extent_id = eid;
  out->exit_pc = KOS_EXTENT_NONE;

  if (e->push_pc >= KosCode_CODE_LEN(code)) {
    out->except_pc = KOS_EXTENT_NONE;
    out->finally_pc = KOS_EXTENT_NONE;
    SAYF("[extent] kos_try_handler_for pc=%u push_pc out of range\n",
         (unsigned) pc);
    return 0;
  }

  uint32_t handler_pc = (uint32_t) KosCode_CODE(code)[e->push_pc].arg;
  if (e->kind == KOS_EXT_TRY) {
    out->except_pc = handler_pc;
    out->finally_pc = KOS_EXTENT_NONE;
  }
  else if (e->kind == KOS_EXT_FINALLY) {
    out->except_pc = KOS_EXTENT_NONE;
    out->finally_pc = handler_pc;
  }
  else {
    out->except_pc = KOS_EXTENT_NONE;
    out->finally_pc = KOS_EXTENT_NONE;
  }

  SAYF("[extent] kos_try_handler_for pc=%u except_pc=%u finally_pc=%u\n",
       (unsigned) pc,
       (unsigned) out->except_pc,
       (unsigned) out->finally_pc);

  return 0;
}


uint32_t kos_shift_ic_for(PyObject *code, uint32_t pc) {

  uint32_t eid = kos_extent_find(code, pc, KOS_EXT_SHIFT);
  if (eid == KOS_EXTENT_NONE) {
    return KOS_EXTENT_NONE;
  }

  KosExtentDesc *e = kos_extent_desc(code, eid);
  if (! e) {
    return KOS_EXTENT_NONE;
  }

  return e->pop_pc + 1;
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
  if (tmp)
    Py_INCREF(tmp);
  else
    tmp = PyUnicode_FromString("<unknown>");
  faux->co_filename = tmp;

  tmp = KosCode_NAME(self);
  Py_CLEAR(faux->co_name);
  if (tmp)
    Py_INCREF(tmp);
  else
    tmp = PyUnicode_FromString("<lambda>");
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


/**
   Create an IntEnum from the given name and pre-filled dict.
   Consumes enum_dict (caller must not DECREF it). Returns new ref or NULL.
*/
static PyObject *create_int_enum(const char *name, PyObject *enum_dict) {

  PyObject *enum_module = PyImport_ImportModule("enum");
  if (! enum_module) {
    Py_DECREF(enum_dict);
    return NULL;
  }

  PyObject *res = PyObject_CallMethod(enum_module, "IntEnum", "sO",
                                      name, enum_dict);

  Py_DECREF(enum_dict);
  Py_DECREF(enum_module);

  return res;
}


PyObject *KosOpStyleEnum = NULL;


static PyObject *create_opstyle_enum(void) {

  PyObject *enum_dict = PyDict_New();
  if (! enum_dict)
    return NULL;

  int_enum(enum_dict, KOSTYLE_COMPILE);
  int_enum(enum_dict, KOSTYLE_LABEL);
  int_enum(enum_dict, KOSTYLE_NOARG);
  int_enum(enum_dict, KOSTYLE_COUNT);
  int_enum(enum_dict, KOSTYLE_PAIR_COUNT);
  int_enum(enum_dict, KOSTYLE_STAR);
  int_enum(enum_dict, KOSTYLE_CONST);
  int_enum(enum_dict, KOSTYLE_VAR);
  int_enum(enum_dict, KOSTYLE_FASTVAR);
  int_enum(enum_dict, KOSTYLE_EXTRA);

  return create_int_enum("OpStyle", enum_dict);
}


PyObject *KosOpCodeEnum = NULL;


static PyObject *create_opcode_enum(void) {

  PyObject *enum_dict = PyDict_New();
  if (! enum_dict)
    return NULL;

#define OP(name, ...)  int_enum(enum_dict, KOS_OP_##name);
#include "opcodes.incl"
#undef OP

  PyObject *res = create_int_enum("OpCode", enum_dict);
  if (! res)
    return NULL;

  /* Attach style and optional extra schema to each opcode */
  for (KosOpCode op = 0; op <= KOS_OP_THEEND; op++) {
    char buf[64];
    int n = PyOS_snprintf(buf, sizeof buf, "KOS_OP_%s", opcode_names[op]);
    if (n < 0 || (size_t) n >= sizeof buf)
      continue;
    PyObject *_en = PyObject_GetAttrString(res, buf);
    if (! _en)
      continue;
    KosOpStyle style = opcode_styles[op].style;
    PyObject *_fl = PyLong_FromLong((long) style);
    PyObject_SetAttrString(_en, "style", _fl);
    Py_DECREF(_fl);
    if (style == KOSTYLE_EXTRA) {
      KosExtraSchema ex = opcode_styles[op].extra;
      PyObject *_ex = Py_BuildValue("(iiii)", ex.f0, ex.f1, ex.f2, ex.f3);
      if (_ex) {
        PyObject_SetAttrString(_en, "extra", _ex);
        Py_DECREF(_ex);
      }
    }
    Py_DECREF(_en);
  }

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
