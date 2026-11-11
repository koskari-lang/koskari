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
*/


/* Python.h sets the feature test macros, so it comes before any
   system header */
#include "lib.h"
#include "debug.h"

#include <string.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <unistd.h>


/* === Static string constants === */


static PyObject *_str_anon = NULL;


/* Max depth for PUSH/POP stacks used in extent derivation and FAST validation */
#define DERIVE_EXTENT_STACK_MAX 512


/* === Opcode styles table === */


typedef struct {
  KosOpStyle style;
  uint8_t cache;          /* count of per-site cache slots this opcode wants */
  KosExtraSchema extra;
} OpStyleEntry;


#define OP_NARGS(_1, _2, _3, _4, _5, _6, _7, N, ...)  N
#define OP_CONCAT(a, b)  a ## b
#define OP_INNER(N, ...)  OP_CONCAT(OP_, N)(__VA_ARGS__)
#define OP(...)  OP_INNER(OP_NARGS(__VA_ARGS__, 7, 6, 5, 4, 3, 2, 1), __VA_ARGS__)

#define OP_3(name, cache, style)  { KOSTYLE_##style, (cache), { \
    KOSTYLE_NOARG, KOSTYLE_NOARG, \
    KOSTYLE_NOARG, KOSTYLE_NOARG } },
#define OP_7(name, cache, EXTRA, f0, f1, f2, f3)  { KOSTYLE_EXTRA, (cache), { \
    KOSTYLE_##f0, KOSTYLE_##f1, \
    KOSTYLE_##f2, KOSTYLE_##f3 } },

static const OpStyleEntry opcode_styles[] = {
  #include "opcodes.incl"
};

#undef OP
#undef OP_3
#undef OP_7
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
static inline KosOpCode push_for_pop(KosOpCode pop_op);
static inline KosExtentKind kind_for_push(KosOpCode push_op);
static inline int discardable_for_push(KosOpCode push_op);


/* === Extra operand schema === */


KosExtraSchema kos_opcode_extra_schema(KosOpCode op) {

  if (op <= KOS_OP_THEEND) {
    return opcode_styles[op].extra;
  }
  return (KosExtraSchema){ KOSTYLE_NOARG, KOSTYLE_NOARG,
      KOSTYLE_NOARG, KOSTYLE_NOARG };
}

/* The kind of one field of a schema. */
static inline KosOpStyle schema_field(KosExtraSchema schema, int slot) {
  switch (slot) {
    case 0: return schema.f0;
    case 1: return schema.f1;
    case 2: return schema.f2;
    default: return schema.f3;
  }
}


/**
   The effective schema of one EXTRA row: the opcode's schema with the
   CMPSRC override applied, where the source bits in field 0 mark the
   operand fields that hold constants.
*/
static KosExtraSchema extra_row_schema(KosOpCode op, uint32_t src) {
  KosExtraSchema schema = kos_opcode_extra_schema(op);

  if (schema.f0 == KOSTYLE_CMPSRC) {
    if (KosCmpSrc_X_CONST(src))
      schema.f1 = KOSTYLE_CONST;
    if (KosCmpSrc_Y_CONST(src))
      schema.f2 = KOSTYLE_CONST;
  }
  return schema;
}


uint8_t kos_opcode_cache_count(KosOpCode op) {

  if (op <= KOS_OP_THEEND) {
    return opcode_styles[op].cache;
  }
  return 0;
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

  PyObject *const existing = PyDict_GetItemWithError(label_map, key);
  if (! existing && PyErr_Occurred()) {
    Py_DECREF(key);
    return -1;
  }

  if (! existing) {
    PyObject *name = PyUnicode_FromFormat(KosCode_SOFTEN_LABEL_PREFIX "%u",
                                            (unsigned int) pc);
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
   label string (``"LABEL_<pc>"``, see KosCode_SOFTEN_LABEL_PREFIX).

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

      const KosExtra *row = KosCode_GetExtra(self, eidx);
      KosExtraSchema schema = kos_opcode_extra_schema(op);

      for (int slot = 0; slot < 4; slot++) {
        KosOpStyle fk = schema_field(schema, slot);
        if (fk == KOSTYLE_LABEL) {
          if (soften_label_map_add_pc(label_map,
                                      KosExtra_TARGET(row, slot)->pc)) {
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

  A resolved row holds pool objects, immediates, and label pcs. This
  reverses emit: CONST/VAR are the pool objects themselves; LABEL slots
  look up the destination PC in ``label_map``; other slots stay as integers
  (FAST,
  COUNT, STAR, etc.). NOARG schema slots become 0, matching the collect
  phase default.

  :param self: KosCode (for constant pool lookup)
  :param op: opcode (selects EXTRA field schema)
  :param row: the resolved row
  :param label_map: ``dict`` mapping target PC ``int`` to label ``str``

  :return: new tuple of length 4, or NULL on error
*/
static PyObject *soften_extra_tuple(
    PyObject *self,
    KosOpCode op,
    const KosExtra *row,
    PyObject *label_map) {

  KosExtraSchema schema = extra_row_schema(op, KosExtra_IMM(row, 0));
  PyObject *items[4];

  for (int slot = 0; slot < 4; slot++) {
    KosOpStyle fk = schema_field(schema, slot);

    /* Mirror code_collect: skip NOARG (emit uses 0); else decode by kind */
    if (fk == KOSTYLE_NOARG) {
      items[slot] = PyLong_FromLong(0);

    } else if (fk == KOSTYLE_CONST || fk == KOSTYLE_ATTR
               || fk == KOSTYLE_VAR) {
      PyObject *v = KosExtra_OBJ(row, slot);
      Py_INCREF(v);
      items[slot] = v;

    } else if (fk == KOSTYLE_LABEL) {
      uint32_t dest_pc = KosExtra_TARGET(row, slot)->pc;
      PyObject *key = PyLong_FromUnsignedLong((unsigned long) dest_pc);
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
                     "label pc %u missing from label map", (unsigned) dest_pc);
        return NULL;
      }
      Py_INCREF(lab);
      items[slot] = lab;

    } else {
      items[slot] = PyLong_FromLong((long) KosExtra_IMM(row, slot));
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


typedef struct {
  size_t allocation_bytes;
  size_t payload_bytes;
  size_t padding_bytes;
  size_t page_rounding_bytes;
  size_t code_bytes;
  size_t position_bytes;
  size_t cpool_bytes;
  size_t extra_bytes;
  size_t extent_bytes;
  size_t top_extent_bytes;
} CodeAllocStats;


/* Alignment of the instruction array and the extras table: a 32-byte
   record never straddles a 64-byte line. The arena reserves every
   member on the same unit, which is what keeps that alignment inside
   a chunk. */
#define KOS_BLOCK_ROW_ALIGN KOS_ARENA_UNIT


static size_t align_size(size_t value, size_t unit) {

  if (! unit) {
    return value;
  }
  return ((value + unit - 1) / unit) * unit;
}


/**
   Region offsets and sizes for a unified bytecode block, computed from
   the five region lengths. The single source of truth for the block
   layout: allocation and the stats getsets all derive
   offsets from here. Region order (with alignment padding between):

     [code][pad][positions][pad][cpool][pad][extra][pad][extents][top]

   All offsets use natural alignment: pointer size for cpool, 4-byte
   for KosExtra. code region starts at offset 0. total_unrounded is
   the exact end of the top region, before any page rounding.
*/
typedef struct KosBlockLayout {
  size_t code_bytes;
  size_t pos_bytes;
  size_t cpool_bytes;
  size_t extra_bytes;
  size_t extent_bytes;
  size_t top_bytes;

  size_t pos_offset;
  size_t cpool_offset;
  size_t extra_offset;
  size_t extent_offset;
  size_t top_offset;

  size_t total_unrounded;
} KosBlockLayout;


static void code_block_layout(
    uint32_t code_len,
    uint32_t pos_count,
    uint32_t cpool_len,
    uint32_t extra_len,
    uint32_t extent_count,
    KosBlockLayout *out) {

  size_t ptr_size = sizeof(PyObject *);

  out->code_bytes = sizeof(KosOp) * (size_t) code_len;
  out->pos_bytes = sizeof(KosPosition) * (size_t) pos_count;
  out->cpool_bytes = sizeof(PyObject *) * (size_t) cpool_len;
  out->extra_bytes = sizeof(KosExtra) * (size_t) extra_len;
  out->extent_bytes = sizeof(KosExtentDesc) * (size_t) extent_count;
  /* a block with no extents has no top_extent_id table either */
  out->top_bytes = extent_count
      ? sizeof(uint16_t) * (size_t) code_len : 0;

  out->pos_offset = align_size(out->code_bytes, ptr_size);
  out->cpool_offset = out->pos_offset
      + align_size(out->pos_bytes, ptr_size);
  /* Rows are two to a cache line; keep the table line-aligned. */
  out->extra_offset = align_size(out->cpool_offset + out->cpool_bytes,
                                 KOS_BLOCK_ROW_ALIGN);
  out->extent_offset = out->extra_offset
      + align_size(out->extra_bytes, sizeof(uint32_t));
  out->top_offset = out->extent_offset + out->extent_bytes;

  out->total_unrounded = out->top_offset + out->top_bytes;
}


static void code_self_layout(PyObject *self, KosBlockLayout *out) {

  code_block_layout(
      KosCode_CODE_LEN(self), KosCode_POS_LEN(self),
      KosCode_CPOOL_LEN(self), KosCode_EXTRA_LEN(self),
      KosCode_EXTENT_LEN(self), out);
}


static void code_calc_alloc_stats(PyObject *self, CodeAllocStats *out) {

  KosBlockLayout lay;
  size_t allocation = KosCode_ALLOCATION(self);

  code_self_layout(self, &lay);

  out->allocation_bytes = allocation;
  out->code_bytes = lay.code_bytes;
  out->position_bytes = lay.pos_bytes;
  out->cpool_bytes = lay.cpool_bytes;
  out->extra_bytes = lay.extra_bytes;
  out->extent_bytes = lay.extent_bytes;
  out->top_extent_bytes = lay.top_bytes;
  out->payload_bytes = lay.code_bytes + lay.pos_bytes + lay.cpool_bytes
      + lay.extra_bytes + lay.extent_bytes + lay.top_bytes;

  out->padding_bytes = lay.total_unrounded - out->payload_bytes;

  /* the allocation is the exact block (heap, arena) or the block
     rounded up to a page (protected), never less than the block */
  out->page_rounding_bytes = allocation - lay.total_unrounded;
}


static PyObject *code_get_allocation_bytes(PyObject *self, void *_unused) {

  CodeAllocStats stats;
  (void) _unused;
  code_calc_alloc_stats(self, &stats);
  return PyLong_FromSize_t(stats.allocation_bytes);
}


static PyObject *code_get_arena(PyObject *self, void *_unused) {

  (void) _unused;

  PyObject *arena = KosCode_ARENA(self);
  if (! arena) {
    Py_RETURN_NONE;
  }
  Py_INCREF(arena);
  return arena;
}


static PyObject *code_get_payload_bytes(PyObject *self, void *_unused) {

  CodeAllocStats stats;
  (void) _unused;
  code_calc_alloc_stats(self, &stats);
  return PyLong_FromSize_t(stats.payload_bytes);
}


static PyObject *code_get_padding_bytes(PyObject *self, void *_unused) {

  CodeAllocStats stats;
  (void) _unused;
  code_calc_alloc_stats(self, &stats);
  return PyLong_FromSize_t(stats.padding_bytes);
}


static PyObject *code_get_page_rounding_bytes(PyObject *self, void *_unused) {

  CodeAllocStats stats;
  (void) _unused;
  code_calc_alloc_stats(self, &stats);
  return PyLong_FromSize_t(stats.page_rounding_bytes);
}


static PyObject *code_get_code_bytes(PyObject *self, void *_unused) {

  CodeAllocStats stats;
  (void) _unused;
  code_calc_alloc_stats(self, &stats);
  return PyLong_FromSize_t(stats.code_bytes);
}


static PyObject *code_get_position_bytes(PyObject *self, void *_unused) {

  CodeAllocStats stats;
  (void) _unused;
  code_calc_alloc_stats(self, &stats);
  return PyLong_FromSize_t(stats.position_bytes);
}


static PyObject *code_get_cpool_bytes(PyObject *self, void *_unused) {

  CodeAllocStats stats;
  (void) _unused;
  code_calc_alloc_stats(self, &stats);
  return PyLong_FromSize_t(stats.cpool_bytes);
}


static PyObject *code_get_extra_bytes(PyObject *self, void *_unused) {

  CodeAllocStats stats;
  (void) _unused;
  code_calc_alloc_stats(self, &stats);
  return PyLong_FromSize_t(stats.extra_bytes);
}


static PyObject *code_get_extent_bytes(PyObject *self, void *_unused) {

  CodeAllocStats stats;
  (void) _unused;
  code_calc_alloc_stats(self, &stats);
  return PyLong_FromSize_t(stats.extent_bytes);
}


static PyObject *code_get_top_extent_bytes(PyObject *self, void *_unused) {

  CodeAllocStats stats;
  (void) _unused;
  code_calc_alloc_stats(self, &stats);
  return PyLong_FromSize_t(stats.top_extent_bytes);
}


static PyObject *code_get_yields(PyObject *self, void *_unused) {

  return PyBool_FromLong(KosCode_YIELDS(self));
}


static PyObject *code_get_self_slot(PyObject *self, void *_unused) {

  return PyLong_FromLong(KosCode_SELF_SLOT(self));
}


static PyObject *code_get_uses_fast(PyObject *self, void *_unused) {

  return PyBool_FromLong(KosCode_USES_FAST(self));
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


static PyObject *code_get_caches(PyObject *self, void *_unused) {

  return PyBool_FromLong(KosCode_CACHES(self) != 0);
}


static PyObject *code_get_protected(PyObject *self, void *_unused) {

  return PyBool_FromLong(KosCode_PROTECTED(self) != 0);
}


static PyObject *code_get_cache_slots(PyObject *self, void *_unused) {

  return PyLong_FromUnsignedLong(KosCode_CACHE_LEN(self));
}


static PyGetSetDef code_getset[] = {

  { "arena", code_get_arena, NULL,
    "owning Arena when the block lives in one, else None", NULL },

  { "allocation_bytes", code_get_allocation_bytes, NULL,
    "int; total allocated bytes for sealed bytecode storage",
    NULL },

  { "payload_bytes", code_get_payload_bytes, NULL,
    "int; sum of logical region bytes before padding",
    NULL },

  { "padding_bytes", code_get_padding_bytes, NULL,
    "int; alignment padding bytes between storage regions",
    NULL },

  { "page_rounding_bytes", code_get_page_rounding_bytes, NULL,
    "int; tail padding bytes added by page-size rounding",
    NULL },

  { "code_bytes", code_get_code_bytes, NULL,
    "int; bytes used by hardened instruction array",
    NULL },

  { "position_bytes", code_get_position_bytes, NULL,
    "int; bytes used by source position table",
    NULL },

  { "cpool_bytes", code_get_cpool_bytes, NULL,
    "int; bytes reserved for constant pool pointer array",
    NULL },

  { "extra_bytes", code_get_extra_bytes, NULL,
    "int; bytes used by extra operand rows",
    NULL },

  { "extent_bytes", code_get_extent_bytes, NULL,
    "int; bytes used by static extent descriptor table",
    NULL },

  { "top_extent_bytes", code_get_top_extent_bytes, NULL,
    "int; bytes reserved for top-extent lookup table",
    NULL },

  { "yields", code_get_yields, NULL,
    "bool; whether this code will yield like a generator",
    NULL },

  { "self_slot", code_get_self_slot, NULL,
    "int; the local slot this code's function binds itself in, or -1",
    NULL },

  { "uses_fast", code_get_uses_fast, NULL,
    "bool; whether this code uses fast-slot opcodes at root namespace scope",
    NULL },

  { "protected", code_get_protected, NULL,
    "bool; whether the unified block is sealed read-only with mprotect",
    NULL },

  { "caches", code_get_caches, NULL,
    "bool; whether per-site opcode caches are enabled for this code (gate)",
    NULL },

  { "cache_slots", code_get_cache_slots, NULL,
    "int; number of per-site cache slots allocated on this code",
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


/**
   Read a Python-side index for the accessors below: a Py_ssize_t, so
   the bound check and the access share one width. -1 with IndexError
   when the int is negative or does not fit, TypeError when it is not
   an int.
*/
static Py_ssize_t code_accessor_index(PyObject *index, const char *what) {

  if (! PyLong_Check(index)) {
    PyErr_Format(PyExc_TypeError, "Code %s index must be int, not %R",
                 what, index);
    return -1;
  }

  Py_ssize_t i = PyLong_AsSsize_t(index);
  if (i < 0) {
    if (i == -1 && PyErr_Occurred())
      PyErr_Clear();
    PyErr_Format(PyExc_IndexError, "Code %s index out of bounds: %R",
                 what, index);
    return -1;
  }

  return i;
}


static PyObject *code_m_constant(PyObject *self, PyObject *index) {

  Py_ssize_t i = code_accessor_index(index, "constants pool");
  if (i < 0)
    return NULL;

  if ((size_t) i >= KosCode_CPOOL_LEN(self)) {
    PyErr_Format(PyExc_IndexError, "Code constants pool index out"
                 " of bounds: %zd", i);
    return NULL;
  }

  PyObject *res = KosCode_GET_CONST(self, (uint32_t) i);
  Py_XINCREF(res);
  return res;
}


/* The Python-visible instruction count: the real length, hiding the appended
   THEEND end-of-code barrier. Every Python-facing accessor (length, oparg,
   position) bounds against this so the barrier is invisible; internal callers
   use the KosCode_CODE_LEN macro (full length, barrier included). */
static uint32_t code_visible_len(PyObject *self) {
  uint32_t len = KosCode_CODE_LEN(self);
  if (len && KosCode_GET_OP(self, len - 1) == KOS_OP_THEEND) {
    len--;
  }
  return len;
}


/**
   The (opcode, arg) pair of one instruction as a tuple of two ints.
   New reference, or NULL with an exception set.
*/
static PyObject *op_as_tuple(KosOp op) {

  PyObject *opv = PyLong_FromLong((long) op.op);
  PyObject *argv = PyLong_FromLong((long) op.arg);
  PyObject *p = NULL;

  if (opv && argv)
    p = PyTuple_New(2);
  if (! p) {
    Py_XDECREF(opv);
    Py_XDECREF(argv);
    return NULL;
  }
  PyTuple_SET_ITEM(p, 0, opv);
  PyTuple_SET_ITEM(p, 1, argv);
  return p;
}


static PyObject *code_m_oparg(PyObject *self, PyObject *index) {

  Py_ssize_t i = code_accessor_index(index, "oparg");
  if (i < 0)
    return NULL;

  if ((size_t) i >= code_visible_len(self)) {
    PyErr_Format(PyExc_IndexError, "Code oparg index out of"
                 " bounds: %zd", i);
    return NULL;
  }

  return op_as_tuple(KosCode_CODE(self)[i]);
}


static PyObject *code_m_length(PyObject *self) {

  return PyLong_FromLong(code_visible_len(self));
}


static PyObject *code_m_position(PyObject *self, PyObject *index) {

  Py_ssize_t i = code_accessor_index(index, "position");
  if (i < 0)
    return NULL;

  if ((size_t) i >= code_visible_len(self)) {
    PyErr_Format(PyExc_IndexError, "Code position index out of"
                 " bounds: %zd", i);
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
  if (KosFunction_CheckFormalsLayout(formals, self) < 0)
    return NULL;
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


/**
   The index view of a resolved row, for introspection and soften's
   callers: a CONST or VAR field reports its pool index, recovered by
   identity against the identity-deduplicated pool; every other field
   reports its immediate. The owning instruction supplies the schema;
   rows are one per instruction.
*/
static int extra_row_indices(PyObject *self, uint32_t eidx, uint16_t out[4]) {

  KosOp *code = KosCode_CODE(self);
  uint32_t code_len = KosCode_CODE_LEN(self);
  const KosExtra *row = KosCode_GetExtra(self, eidx);
  KosOpCode op = KOS_OP_THEEND;

  for (uint32_t pc = 0; pc < code_len; pc++) {
    if (opcode_styles[code[pc].op].style == KOSTYLE_EXTRA
        && code[pc].arg == eidx) {
      op = (KosOpCode) code[pc].op;
      break;
    }
  }
  if (op == KOS_OP_THEEND) {
    PyErr_Format(PyExc_RuntimeError, "extra row %u has no owning"
                 " instruction", (unsigned) eidx);
    return -1;
  }

  KosExtraSchema schema = extra_row_schema(op, KosExtra_IMM(row, 0));

  for (int slot = 0; slot < 4; slot++) {
    KosOpStyle fk = schema_field(schema, slot);

    if (fk == KOSTYLE_CONST || fk == KOSTYLE_ATTR || fk == KOSTYLE_VAR) {
      PyObject *obj = KosExtra_OBJ(row, slot);
      uint32_t cpool_len = KosCode_CPOOL_LEN(self);
      uint32_t ci = 0;
      while (ci < cpool_len && KosCode_GET_CONST(self, ci) != obj)
        ci++;
      if (ci == cpool_len) {
        PyErr_Format(PyExc_RuntimeError, "extra row %u field %d is not"
                     " a pool constant", (unsigned) eidx, slot);
        return -1;
      }
      out[slot] = (uint16_t) ci;
    } else if (fk == KOSTYLE_LABEL) {
      out[slot] = KosExtra_TARGET(row, slot)->pc;
    } else {
      out[slot] = (uint16_t) KosExtra_IMM(row, slot);
    }
  }
  return 0;
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

  uint16_t view[4];
  if (extra_row_indices(self, (uint32_t) i, view))
    return NULL;
  return Py_BuildValue("(HHHH)", (unsigned short) view[0],
                       (unsigned short) view[1],
                       (unsigned short) view[2],
                       (unsigned short) view[3]);
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

  /* Strip the trailing THEEND barrier: it is appended fresh at assembly, so it
     must not appear in the softened stream (KosCode_New re-adds it, and it is
     rejected as user input by code_collect). collect_label_map also rejects
     THEEND, so bound its scan to the body too. */
  uint32_t body_len = code_len;
  if (body_len && KosCode_GET_OP(self, body_len - 1) == KOS_OP_THEEND) {
    body_len--;
  }

  PyObject *label_map = collect_label_map(self, body_len);

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

  for (uint32_t i = 0; i < body_len; i++) {
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
      const KosExtra *row = KosCode_GetExtra(self, insn.arg);
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
   The index form of an extended operand row, as collect builds it and
   soften emits it: four 16-bit fields, each a cpool index, a label pc,
   or an immediate by schema. Emit resolves it into the block's KosExtra.
*/
typedef struct KosExtraRaw {
  uint16_t a0;
  uint16_t a1;
  uint16_t a2;
  uint16_t a3;
} KosExtraRaw;


/**
   Collection state for the finalization pipeline.
 Built during the
   collect phase; used for sizing and by the emit phase. Lifetime:
   from code_collect through code_emit.
*/
typedef struct {
  PyObject *cpool_list;       /* PyList: identity-based const internment (no dict) */
  PyObject *labels;           /* PyDict: label_name -> output_pc for jump resolution */
  KosExtraRaw *extra_rows;    /* growable array of index-form rows; CONST/VAR interned */
  uint32_t extra_len;
  uint32_t extra_cap;
  uint16_t *args;             /* per output instruction: the cpool index of a
                                 CONST or VAR operand, resolved here so emit
                                 neither converts nor interns a second time */
  KosPosition *pos_entries;   /* delta-encoded positions; index where (line,col) changes */
  uint32_t pos_count;
  uint32_t pos_cap;
  uint32_t code_len;          /* instruction count after squashing LABEL/POSITION */
  uint32_t cpool_len;         /* unique constants by identity */
  uint32_t extent_count;      /* PUSH_* count for extent table sizing */
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

   :return: the 16-bit pool index, so every value 0..65535 is a valid
     result, or -1 with an exception set
*/
static int intern_const(CodeCollect *coll, PyObject *value) {

  if (! value) {
    PyErr_SetString(PyExc_RuntimeError, "NULL value passed to intern_const");
    return -1;
  }

  Py_INCREF(value);
  if (PyUnicode_CheckExact(value))
    PyUnicode_InternInPlace(&value);

  PyObject *list = coll->cpool_list;
  Py_ssize_t n = PyList_GET_SIZE(list);

  /* Linear scan by identity; do not use dict, hash, or equality
     (1==True, 0==False would merge those values and many others) */
  for (Py_ssize_t i = 0; i < n; i++) {
    if (PyList_GET_ITEM(list, i) == value) {
      Py_DECREF(value);
      return (int) i;
    }
  }

  if (n > UINT16_MAX) {
    PyErr_Format(PyExc_OverflowError,
                 "constant pool exceeds 16-bit limit (%zd entries)", n);
    Py_DECREF(value);
    return -1;
  }

  if (PyList_Append(list, value) < 0) {
    Py_DECREF(value);
    return -1;
  }

  Py_DECREF(value);
  coll->cpool_len = (uint32_t) (n + 1);
  return (int) n;
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
  PyMem_Del(coll->args);
  coll->args = NULL;
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
  if (! io)
    return -1;
  int ret = PyDict_SetItem(labels, name, io);
  Py_DECREF(io);
  return ret;
}


/**
   A label is an exact str or int: hashing and comparing one runs no
   Python, so resolving labels while a block is being written cannot
   run code.
*/
static int check_label(PyObject *label, Py_ssize_t i) {
  if (PyUnicode_CheckExact(label) || PyLong_CheckExact(label))
    return 0;
  PyErr_Format(PyExc_TypeError, "Code instruction %zd label must be a str"
               " or int, not %.100s", i, Py_TYPE(label)->tp_name);
  return -1;
}


/* === Position unpacking === */


/**
   One line or column of a POSITION entry: an int in the 16-bit range
   the record stores, refused rather than truncated. Returns -1 with
   the error set.
*/
static int position_field(PyObject *tmp, PyObject *entry, uint16_t *out) {

  if (! PyLong_Check(tmp)) {
    PyErr_Format(PyExc_RuntimeError, "Invalid position entry %R", entry);
    return -1;
  }

  long val = PyLong_AsLong(tmp);
  if ((val == -1 && PyErr_Occurred()) || val < 0 || val > UINT16_MAX) {
    PyErr_Clear();
    PyErr_Format(PyExc_ValueError, "position entry %R is outside 0..%u",
                 entry, (unsigned) UINT16_MAX);
    return -1;
  }

  *out = (uint16_t) val;
  return 0;
}


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
    if (position_field(tmp, entry, line))
      goto failure;

    tmp = PyTuple_GET_ITEM(entry, offset++);
    if (position_field(tmp, entry, col))
      goto failure;

  } else if (PyTuple_GET_SIZE(entry)) {

    tmp = PyTuple_GET_ITEM(entry, offset++);
    if (position_field(tmp, entry, line))
      goto failure;
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
  coll->args = PyMem_New(uint16_t, (size_t) instr_l + 1);
  coll->pos_entries = PyMem_New(KosPosition, 64);
  coll->pos_count = 0;
  coll->pos_cap = 64;
  if (! coll->args || ! coll->pos_entries) {
    goto failure;
  }

  coll->code_len = 0;
  coll->cpool_len = 0;
  coll->extent_count = 0;
  coll->last_line = 0;
  coll->last_col = 0;

  /* Pass 1: Simulate squashing of LABEL/POSITION so we know output sizes.
     di = destination instruction index (count of instructions that survive).
     LABEL and POSITION are dropped from the final stream; their data goes
     into labels dict and pos_entries. */
  uint32_t di = 0;
  for (Py_ssize_t i = 0; i < instr_l; i++) {
    /* shape and opcode range were checked by validate_instructions
       before anything read the items */
    PyObject *pair = PySequence_Fast_GET_ITEM(instr, i);
    PyObject *b = PySequence_Fast_GET_ITEM(pair, 1);
    KosOpCode op = (KosOpCode) PyLong_AsLong(PySequence_Fast_GET_ITEM(pair, 0));

    if (op == KOS_OP_LABEL) {
      /* Record label name -> output pc; LABEL itself is not emitted */
      if (check_label(b, i) || collect_label(coll->labels, b, di)) {
        goto failure;
      }
      continue;   /* do not advance di */
    }

    if (op == KOS_OP_POSITION) {
      uint16_t line = 0, col = 0;
      if (unpack_position(b, &line, &col)) {
        goto failure;
      }
      /* Emit position entry only when line/col changes (delta encoding) */
      if (line != coll->last_line || col != coll->last_col) {
        /* Coalesce consecutive POSITION ops that map to the same output
           instruction index: only the last one before the instruction
           takes effect, so overwrite the pending entry rather than append
           an orphan.  code_emit places at most one entry per instruction
           index, so an appended same-index entry would be dropped, and the
           unwritten slot it leaves in the table corrupts the binary
           search in KosCode_GetPosition. */
        if (coll->pos_count > 0
            && coll->pos_entries[coll->pos_count - 1].index == (uint16_t) di) {
          coll->pos_entries[coll->pos_count - 1].line = line;
          coll->pos_entries[coll->pos_count - 1].col = col;
        } else {
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
        }
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

  /* Reserve one extra slot for the THEEND end-of-code barrier, appended by
     code_emit. Every code object carries it as its final instruction. A
     record carries its own 16-bit pc, and jump targets are 16-bit, so the
     count is bounded here. */
  if (di >= UINT16_MAX) {
    PyErr_Format(PyExc_OverflowError,
                 "code exceeds 16-bit instruction limit (%u instructions)",
                 (unsigned) di);
    goto failure;
  }
  coll->code_len = di + 1;

  /* A POSITION after the last instruction names nothing: its entry
     would sit at the THEEND slot, which code_emit never visits, and
     the unwritten slot it left in the table would break the binary
     search for the final run. */
  if (coll->pos_count > 0
      && coll->pos_entries[coll->pos_count - 1].index == (uint16_t) di) {
    coll->pos_count--;
  }

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
                     "Code instruction %zd EXTRA opcode expects tuple"
                     " argument, got %R", i, b);
        goto failure;
      }
      Py_ssize_t tlen = PyTuple_GET_SIZE(b);
      if (tlen < 4) {
        PyErr_Format(PyExc_RuntimeError,
                     "Code instruction %zd EXTRA opcode tuple must have 4"
                     " elements, got %zd", i, tlen);
        goto failure;
      }

      /* CMPSRC rows carry per-operand source bits in slot a0: a set bit
         overrides that operand slot's declared kind to CONST. Resolve
         the effective schema before interpreting the slots. */
      if (schema.f0 == KOSTYLE_CMPSRC) {
        PyObject *srcv = PyTuple_GET_ITEM(b, 0);
        if (! PyLong_Check(srcv)) {
          PyErr_Format(PyExc_RuntimeError,
                       "Code instruction %zd CMPSRC field expects int,"
                       " got %R", i, srcv);
          goto failure;
        }
        schema = extra_row_schema(op, (uint32_t) PyLong_AsLong(srcv));
      }

      KosExtraRaw row = { 0, 0, 0, 0 };
      for (int slot = 0; slot < 4; slot++) {
        KosOpStyle fk = schema_field(schema, slot);
        PyObject *elem = PyTuple_GET_ITEM(b, slot);

        if (fk == KOSTYLE_NOARG) {
          continue;
        }
        /* CONST, ATTR: intern value, store cpool index in row slot.
           An ATTR field's constant is handed to the method cache and
           the type lookup as a str, so it is one here. */
        if (fk == KOSTYLE_CONST || fk == KOSTYLE_ATTR) {
          if (fk == KOSTYLE_ATTR && ! PyUnicode_CheckExact(elem)) {
            PyErr_Format(PyExc_TypeError, "Code instruction %zd attribute"
                         " name must be str, not %.100s",
                         i, Py_TYPE(elem)->tp_name);
            goto failure;
          }
          int idx = intern_const(coll, elem);
          if (idx < 0) {
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
          int idx = intern_const(coll, name);
          Py_DECREF(name);
          if (idx < 0) {
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
          if (check_label(elem, i))
            goto failure;
          PyObject *idx_obj = PyLong_FromLong(extra_idx);
          PyObject *slot_obj = PyLong_FromLong(slot);
          if (! idx_obj || ! slot_obj) {
            Py_XDECREF(idx_obj);
            Py_XDECREF(slot_obj);
            goto failure;
          }
          /* PyTuple_Pack increfs each element; release our own refs after */
          PyObject *pending = PyTuple_Pack(3, idx_obj, slot_obj, elem);
          Py_DECREF(idx_obj);
          Py_DECREF(slot_obj);
          if (! pending || PyList_Append(coll->pending_labels, pending) < 0) {
            Py_XDECREF(pending);
            goto failure;
          }
          Py_DECREF(pending);
          continue;
        }
        /* FASTVAR, COUNT, STAR, OPCOUNT, CMP, CMPCOUNT, CMPSRC,
           OPERAND, RAW: store raw integer (slot, argc, bitflags, packed
           op-type+count, comparison selector, packed
           selector+count/source-bits, operator selector, an opcode's
           own immediate) */
        if (fk == KOSTYLE_COUNT || fk == KOSTYLE_FASTVAR
            || fk == KOSTYLE_STAR || fk == KOSTYLE_OPCOUNT
            || fk == KOSTYLE_CMP || fk == KOSTYLE_CMPCOUNT
            || fk == KOSTYLE_CMPSRC || fk == KOSTYLE_OPERAND
            || fk == KOSTYLE_RAW) {
          if (! PyLong_Check(elem)) {
            PyErr_Format(PyExc_RuntimeError,
                         "Code instruction %zd EXTRA field %d expects int,"
                         " got %R", i, slot, elem);
            goto failure;
          }
          long val = PyLong_AsLong(elem);
          if (val < 0 || val > 65535) {
            PyErr_Format(PyExc_RuntimeError,
                         "Code instruction %zd EXTRA field %d value out of"
                         " range: %ld", i, slot, val);
            goto failure;
          }

          /* A selector names an operator or comparison; one past the
             table is refused here rather than at dispatch. */
          if ((fk == KOSTYLE_OPCOUNT
               && KosOpCount_TYPE((uint16_t) val) >= KOS_OPERAND_COUNT)
              || (fk == KOSTYLE_OPERAND && val >= KOS_OPERAND_COUNT)) {
            PyErr_Format(PyExc_ValueError,
                         "Code instruction %zd EXTRA field %d %R names no"
                         " operator", i, slot, elem);
            goto failure;
          }
          if ((fk == KOSTYLE_CMP && val >= KOS_COMPARE_COUNT)
              || (fk == KOSTYLE_CMPSRC
                  && KosCmpSrc_SEL((uint16_t) val) >= KOS_COMPARE_COUNT)) {
            PyErr_Format(PyExc_ValueError,
                         "Code instruction %zd EXTRA field %d %R names no"
                         " comparison", i, slot, elem);
            goto failure;
          }

          /* An operand count no segment can hold is refused here, as
             for the scalar COUNT; a flag-valued COUNT never nears it. */
          if (fk == KOSTYLE_COUNT && val > KOS_SEGMENT_MAX_SLOTS) {
            PyErr_Format(
                PyExc_ValueError,
                "Code instruction %zd EXTRA field %d %R names more"
                " operands than a segment holds (%u)",
                i, slot, elem, (unsigned) KOS_SEGMENT_MAX_SLOTS);
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
        KosExtraRaw *n = PyMem_Resize(coll->extra_rows, KosExtraRaw, new_cap);
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

    /* CONST: intern arg into cpool; emit reads the index back */
    if (style == KOSTYLE_CONST) {
      int idx = intern_const(coll, b);
      if (idx < 0) {
        goto failure;
      }
      coll->args[di] = (uint16_t) idx;
      di++;
      continue;
    }

    /* VAR: stringify name once, intern into cpool (identity-based);
       emit reads the index back */
    if (style == KOSTYLE_VAR) {
      PyObject *name = PyObject_Str(b);
      if (! name) {
        goto failure;
      }
      int idx = intern_const(coll, name);
      Py_DECREF(name);
      if (idx < 0) {
        goto failure;
      }
      coll->args[di] = (uint16_t) idx;
      di++;
      continue;
    }

    /* LABEL-style (JUMP etc): emit resolves the operand itself from
       the name -> pc dict pass 1 filled; nothing to record here */
    if (style == KOSTYLE_LABEL) {
      if (check_label(b, i))
        goto failure;
      di++;
      continue;
    }

    /* Count-like styles: the arg is the instruction's 16-bit field.
       Refuse anything emit could not store as given; NOARG ignores its
       arg. */
    if (style == KOSTYLE_COUNT || style == KOSTYLE_PAIR_COUNT
        || style == KOSTYLE_STAR || style == KOSTYLE_FASTVAR
        || style == KOSTYLE_OPCOUNT || style == KOSTYLE_CMPCOUNT
        || style == KOSTYLE_RAW) {
      if (! PyLong_Check(b)) {
        PyErr_Format(PyExc_TypeError, "Code instruction %zd argument must be"
                     " an int, not %.100s", i, Py_TYPE(b)->tp_name);
        goto failure;
      }
      long val = PyLong_AsLong(b);
      if ((val == -1 && PyErr_Occurred()) || val < 0 || val > UINT16_MAX) {
        PyErr_Clear();
        PyErr_Format(PyExc_ValueError, "Code instruction %zd argument %R is"
                     " outside 0..%u", i, b, (unsigned) UINT16_MAX);
        goto failure;
      }

      /* A packed selector names an operator or comparison; one past
         the table is refused here rather than at dispatch. */
      if (style == KOSTYLE_OPCOUNT
          && KosOpCount_TYPE((uint16_t) val) >= KOS_OPERAND_COUNT) {
        PyErr_Format(PyExc_ValueError, "Code instruction %zd argument %R"
                     " names no operator", i, b);
        goto failure;
      }
      if (style == KOSTYLE_CMPCOUNT
          && KosCmpCount_SEL((uint16_t) val) >= KOS_COMPARE_COUNT) {
        PyErr_Format(PyExc_ValueError, "Code instruction %zd argument %R"
                     " names no comparison", i, b);
        goto failure;
      }

      /* An operand count no segment can hold is refused here rather
         than at the push that would overflow it: a COUNT names that
         many operands, a PAIR_COUNT twice as many. */
      if (style == KOSTYLE_COUNT || style == KOSTYLE_PAIR_COUNT) {
        long slots = (style == KOSTYLE_PAIR_COUNT) ? val * 2 : val;
        if (slots > KOS_SEGMENT_MAX_SLOTS) {
          PyErr_Format(PyExc_ValueError, "Code instruction %zd argument %R"
                       " names more operands than a segment holds (%u)",
                       i, b, (unsigned) KOS_SEGMENT_MAX_SLOTS);
          goto failure;
        }
      }

      /* A prompt instruction's count is the number of operands naming
         its tag: none for the default tag, or the one tag. */
      if ((op == KOS_OP_PUSH_PROMPT || op == KOS_OP_PUSH_SHIFT)
          && val > 1) {
        PyErr_Format(PyExc_ValueError, "Code instruction %zd argument %R"
                     " is not 0 or 1", i, b);
        goto failure;
      }
    }

    di++;
  }

  Py_ssize_t cpool_len = PyList_GET_SIZE(coll->cpool_list);
  if (cpool_len > UINT16_MAX) {
    PyErr_Format(
      PyExc_RuntimeError,
      "Constant pool length %zd exceeds 16-bit limit",
      cpool_len);
    goto failure;
  }

  coll->cpool_len = (uint16_t) cpool_len;
  return 0;

 failure:
  code_collect_clear(coll);
  return -1;
}


/* === Phase 2: Allocate === */


/**
   Allocate a single block for the entire bytecode object. Sizes come
   from CodeCollect; no over-allocation. Layout (with alignment padding
   between regions):

     [code][pad][positions][pad][cpool][pad][extra][pad][extents]
     [top_extent_id]

   All offsets use natural alignment: pointer size for cpool,
   4-byte for KosExtra. With an allocator, the exact unrounded total is
   reserved from one of its arena chunks, which out_chunk receives as
   an owned reference, so the chunk outlives whatever Python runs while
   the block is written, and protect is ignored. Otherwise,
   with protect, the block is page-aligned and the total is rounded up
   to page size for the later mprotect seal; without, it is a plain
   writable malloc of the exact unrounded total.
*/
static int code_alloc_unified(
    CodeCollect *coll,
    int protect,
    PyObject *allocator,
    PyObject **out_chunk,
    void **out_base,
    size_t *out_len,
    KosOp **out_code,
    KosPosition **out_pos,
    PyObject ***out_cpool,
    KosExtra **out_extra,
    KosExtentDesc **out_extents,
    uint16_t **out_top_extent_id) {

  /* Compute region sizes and offsets from collect-phase counts (all
     exact, no oversizing) */
  KosBlockLayout lay;
  code_block_layout(coll->code_len, coll->pos_count, coll->cpool_len,
                    coll->extra_len, coll->extent_count, &lay);

  size_t total = lay.total_unrounded;
  void *base = NULL;

  *out_chunk = NULL;

  if (allocator) {
    if (KosCodeAllocator_Reserve(allocator, total, &base, out_chunk))
      return -1;
    Py_INCREF(*out_chunk);

  } else if (protect) {
    /* Page-aligned; later mprotect(PROT_READ) once for entire block */
    size_t page_size = KosArena_PageSize();
    total = align_size(total, page_size);
    if (posix_memalign(&base, page_size, total)) {
      base = NULL;
    }
  } else {
    /* Plain heap, line-aligned for the instruction array */
    if (posix_memalign(&base, KOS_BLOCK_ROW_ALIGN, total)) {
      base = NULL;
    }
  }

  if (! base) {
    PyErr_SetString(PyExc_MemoryError,
                    "Failed to allocate bytecode storage");
    return -1;
  }

  *out_base = base;
  *out_len = total;
  *out_code = (KosOp *) base;
  *out_pos = (KosPosition *) ((char *) base + lay.pos_offset);
  *out_cpool = (PyObject **) ((char *) base + lay.cpool_offset);
  *out_extra = coll->extra_len
      ? (KosExtra *) ((char *) base + lay.extra_offset) : NULL;
  *out_extents = coll->extent_count
      ? (KosExtentDesc *) ((char *) base + lay.extent_offset) : NULL;
  *out_top_extent_id = coll->extent_count
      ? (uint16_t *) ((char *) base + lay.top_offset) : NULL;

  return 0;
}


/**
   Release a unified block that code_alloc_unified produced and no
   KosCode has taken: back to its arena chunk, dropping the reference
   the reservation took, or to the heap. The block is still writable.
*/
static void code_free_unified(void *base, size_t size, PyObject *chunk) {

  if (chunk) {
    KosArena_Complete(chunk);
    KosArena_Release(chunk, base, size);
    Py_DECREF(chunk);
  } else {
    free(base);
  }
}


/**
   Resolve the derived half of every instruction: the handler from its
   opcode, and the in-block operand pointers (a LABEL's target, an
   EXTRA's row) from the serialized index. Runs after emit and
   validation in the block's writable window. A row's LABEL fields are
   set by emit's pending-label pass, and the out-of-block pointers (a
   pool object, a cache slot) are set once by emit.
*/
static void code_fill_derived(
    KosOp *code,
    uint32_t code_len,
    KosExtra *extra) {

  for (uint32_t pc = 0; pc < code_len; pc++) {
    KosOpCode op = (KosOpCode) code[pc].op;
    KosOpStyle st = opcode_styles[op].style;

    code[pc].fn = kos_op_handlers[op];
    if (st == KOSTYLE_LABEL)
      code[pc].u.target = &code[code[pc].arg];
    else if (st == KOSTYLE_EXTRA)
      code[pc].u.row = &extra[code[pc].arg];
  }
}


/* === Phase 3: Emit === */


/**
   Emit instructions and tables into pre-allocated regions. Walk input
   again; for each instruction, write op/arg to code[]. LABEL and POSITION
   are skipped. CONST and VAR args are the indices collect resolved;
   nothing here converts or interns, so the pool cannot grow past the
   count it was allocated at. Resolve label targets for KOSTYLE_LABEL
   and pending_labels. Copy cpool_list -> cpool, extra_rows -> extra,
   pos_entries -> pos.
*/
/**
   Resolve one row from collect's index form: CONST and VAR fields become
   the pool object (borrowed; the cpool array holds the owned reference),
   every other field its immediate. A LABEL field keeps its placeholder
   for the pending-label pass. A CMPSRC row's source bits mark the
   operand fields that hold constants.
*/
static void resolve_extra_row(
    CodeCollect *coll,
    KosOpCode op,
    const KosExtraRaw *raw,
    KosExtra *out) {

  KosExtraSchema schema = extra_row_schema(op, raw->a0);
  const uint16_t idx[4] = { raw->a0, raw->a1, raw->a2, raw->a3 };

  for (int slot = 0; slot < 4; slot++) {
    KosOpStyle fk = schema_field(schema, slot);

    if (fk == KOSTYLE_CONST || fk == KOSTYLE_ATTR || fk == KOSTYLE_VAR)
      out->f[slot].obj = PyList_GET_ITEM(coll->cpool_list, idx[slot]);
    else
      out->f[slot].imm = idx[slot];
  }
}


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
  uint16_t next_slot = 0;
  int cache_on = (KosCode_FLAGS(self) & KOS_CODE_CACHE) != 0;

  /* Walk input; di = output instruction index, pi = position entry index */
  for (Py_ssize_t i = 0; i < instr_l; i++) {
    PyObject *pair = PySequence_Fast_GET_ITEM(instr, i);
    PyObject *a = PySequence_Fast_GET_ITEM(pair, 0);
    PyObject *b = PySequence_Fast_GET_ITEM(pair, 1);
    KosOpCode op = (KosOpCode) PyLong_AsLong(a);
    KosOpStyle style = opcode_styles[op].style;

    /* LABEL/POSITION were squashed in collect; skip them here */
    if (op == KOS_OP_LABEL || op == KOS_OP_POSITION)
      continue;

    code[di].op = (uint16_t) op;
    code[di].arg = 0;
    code[di].pc = (uint16_t) di;

    /* Reserve this site's cache slot run. A cache-consuming opcode gets the
       next free index; the count_cache_slots pre-pass sized the trailing
       array to match. When the cache is gated off (cache_on == 0) no site
       gets a slot and every index stays UNSET. */
    uint8_t ncache = cache_on ? kos_opcode_cache_count(op) : 0;
    if (ncache) {
      code[di].cache = next_slot;
      code[di].slot = &KosCode_CACHE(self)[next_slot];
      next_slot += ncache;
    } else {
      code[di].cache = KOS_CACHE_UNSET;
      code[di].slot = NULL;
    }

    /* Resolve arg from collect-phase data: cpool index, label pc, or raw */
    if (style == KOSTYLE_CONST || style == KOSTYLE_VAR) {
      uint16_t idx = coll->args[di];
      code[di].arg = idx;
      code[di].u.obj = PyList_GET_ITEM(coll->cpool_list, idx);

    } else if (style == KOSTYLE_LABEL) {
      /* Look up target pc from labels dict (label_name -> pc) */
      PyObject *dest = PyDict_GetItem(coll->labels, b);
      if (! dest) {
        PyErr_Format(PyExc_RuntimeError, "opcode jump label missing %R", b);
        return -1;
      }
      code[di].arg = (uint16_t) PyLong_AsLong(dest);
      code[di].u.target = &code[code[di].arg];
    } else if (style == KOSTYLE_EXTRA) {
      /* Arg is the row index; the row resolves from collect's index
         form, and the record points at it. */
      code[di].arg = (uint16_t) extra_idx;
      if (extra) {
        resolve_extra_row(coll, op, &coll->extra_rows[extra_idx],
                          &extra[extra_idx]);
        code[di].u.row = &extra[extra_idx];
      }
      extra_idx++;
    } else if (style == KOSTYLE_NOARG) {
      code[di].arg = 0;
    } else {
      /* COUNT, PAIR_COUNT, STAR, FASTVAR, OPCOUNT, CMPCOUNT: collect
         checked the range */
      code[di].arg = (uint16_t) PyLong_AsLong(b);
    }

    /* Record YIELDS flag for generators */
    if (kos_op_yields(op))
      KosCode_FLAGS(self) |= KOS_CODE_YIELDS;

    /* Emit position entry when this instruction has a position change */
    if (pi < coll->pos_count && coll->pos_entries[pi].index == di) {
      pos[pi] = coll->pos_entries[pi];
      pi++;
    }

    di++;
  }

  /* Append the THEEND end-of-code barrier into the slot code_collect reserved
     (code_len = di + 1). Every code object ends here, so a program counter that
     walks off the end faults at THEEND instead of overrunning the array. It is
     ours alone: users cannot supply THEEND (rejected in code_collect), soften
     strips it, and it requests no cache slot. */
  code[di].op = (uint16_t) KOS_OP_THEEND;
  code[di].arg = 0;
  code[di].cache = KOS_CACHE_UNSET;
  code[di].pc = (uint16_t) di;
  code[di].slot = NULL;
  di++;

  /* The assigned slot run must exactly fill the trailing array that
     count_cache_slots sized at allocation. A mismatch is an assembler bug
     that would let a consumer index out of bounds. */
  if (next_slot != KosCode_CACHE_LEN(self)) {
    PyErr_Format(PyExc_RuntimeError, "cache slot mismatch: assigned %u,"
                 " allocated %u", (unsigned) next_slot,
                 (unsigned) KosCode_CACHE_LEN(self));
    return -1;
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
      extra[eidx].f[slot].target = &code[PyLong_AsLong(dest)];
    }
  }

  /* Transfer cpool last, once nothing in this pass can fail: INCREF
     each item into the pre-allocated array. From here the entries are
     owned references; a rejection after emit must release them
     (code_release_cpool) and success hands them to the Code, whose
     dealloc releases them. */
  for (uint32_t j = 0; j < coll->cpool_len; j++) {
    PyObject *val = PyList_GET_ITEM(coll->cpool_list, j);
    Py_INCREF(val);
    cpool[j] = val;
  }

  return 0;
}


/* Release the owned pool entries a successful code_emit filled, for a
   finalize that rejects the code afterwards. */
static void code_release_cpool(PyObject **cpool, uint32_t cpool_len) {
  for (uint32_t j = cpool_len; j--; )
    Py_DECREF(cpool[j]);
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
  int discardable;   /* a tail call may pass through the extent: its
                        uninstall is a pure release, so frame handoff
                        drops it without running its POP_* row */
} ExtentOpPair;


/* PUSH_* -> POP_*, extent kind and discardability; the source of truth
   for extent op classification. The OpCode enum carries these facts to
   the compiler (decorate_opcode), so the compiler's tables derive from
   this one. The runtime teardown (kos_dyn_teardown_extents_to_frame)
   holds the discardable set once more, keyed by node kind. */
static const ExtentOpPair extent_pairs[] = {
  { KOS_OP_PUSH_TRY,      KOS_OP_POP_TRY,      KOS_EXT_TRY,      0 },
  { KOS_OP_PUSH_LOOP,     KOS_OP_POP_LOOP,     KOS_EXT_LOOP,     1 },
  { KOS_OP_PUSH_FINALLY,  KOS_OP_POP_FINALLY,  KOS_EXT_FINALLY,  0 },
  { KOS_OP_PUSH_PROMPT,   KOS_OP_POP_PROMPT,   KOS_EXT_PROMPT,   0 },
  { KOS_OP_PUSH_SHIFT,    KOS_OP_POP_SHIFT,    KOS_EXT_SHIFT,    0 },
  { KOS_OP_PUSH_CC,       KOS_OP_POP_CC,       KOS_EXT_CC,       0 },
  { KOS_OP_PUSH_WITH,     KOS_OP_POP_WITH,     KOS_EXT_WITH,     0 },
  { KOS_OP_PUSH_DYN_WITH, KOS_OP_POP_DYN_WITH, KOS_EXT_DYN_WITH, 0 },
  { KOS_OP_PUSH_NS,       KOS_OP_POP_NS,       KOS_EXT_NS,       1 },
  { KOS_OP_PUSH_COLLECT,  KOS_OP_POP_COLLECT,  KOS_EXT_COLLECT,  0 },
  { KOS_OP_PUSH_WIND,     KOS_OP_POP_WIND,     KOS_EXT_WIND,     0 },
  { KOS_OP_PUSH_FOREACH,  KOS_OP_POP_FOREACH,  KOS_EXT_FOREACH,  1 },
  { KOS_OP_PUSH_PARAM,    KOS_OP_POP_PARAM,    KOS_EXT_PARAM,    0 },
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


static inline KosOpCode push_for_pop(KosOpCode pop_op) {

  for (size_t i = 0; i < N_EXTENT_PAIRS; i++) {
    if (extent_pairs[i].pop_op == pop_op) {
      return extent_pairs[i].push_op;
    }
  }
  return KOS_OP_NOOP;
}


static inline int discardable_for_push(KosOpCode push_op) {

  for (size_t i = 0; i < N_EXTENT_PAIRS; i++) {
    if (extent_pairs[i].push_op == push_op) {
      return extent_pairs[i].discardable;
    }
  }
  return 0;
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
   Refuse a TAIL_CALL family row nested in an extent frame handoff
   cannot discard. The tail call tears down the extents open above the
   frame; the pairs table marks the kinds that uninstall by pure
   release, every other kind has uninstall behavior that must run, so
   the compiler's promoter never puts a tail call inside one and a
   hand-assembled stream that does is refused here, the same set the
   runtime teardown holds.

   :return: 0 when pc is not a tail call or sits only in discardable
     extents, -1 with ValueError set otherwise
*/
static int validate_tail_call_extents(
    KosOpCode op, uint32_t pc,
    const ExtentStackEntry *stack, uint32_t stack_top) {

  if (op < KOS_OP_TAIL_CALL || op > KOS_OP_TAIL_CALL_FAST_ATTR_STAR)
    return 0;

  for (uint32_t i = 0; i < stack_top; i++) {
    KosOpCode push = stack[i].push_op;
    if (discardable_for_push(push))
      continue;
    PyErr_Format(PyExc_ValueError,
                 "TAIL_CALL at pc %u inside a non-discardable %s extent"
                 " opened at pc %u",
                 pc, kos_opcode_name(push), stack[i].push_pc);
    return -1;
  }
  return 0;
}


/**
   Walk the instruction stream matching each PUSH_* with its POP_*, and
   fill the extent table and top_extent_id as it goes: on PUSH_* a
   descriptor is allocated and pushed, on POP_* the extent closes and
   its id is propagated to every pc inside the block. A tail call
   inside an extent frame handoff cannot discard, a POP_* with no
   PUSH_*, a mismatched pair, nesting past the stack limit and an
   extent left open at the end are refused.

   With NULL outputs the walk validates alone, so a stream with no
   PUSH_* (extent_count 0, no buffers allocated) still has its POP_*
   rows refused. The buffers, when given, sit pre-allocated in the
   unified block.
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
  if (top_extent_id_out) {
    for (uint32_t i = 0; i < code_len; i++) {
      top_extent_id_out[i] = KOS_EXTENT_NONE;
    }
  }

  for (uint32_t pc = 0; pc < code_len; pc++) {
    KosOpCode op = (KosOpCode) code[pc].op;

    if (validate_tail_call_extents(op, pc, stack, stack_top))
      return -1;

    if (is_push_op(op)) {
      if (stack_top >= DERIVE_EXTENT_STACK_MAX) {
        PyErr_Format(PyExc_ValueError, "extent nesting too deep (limit %u)",
                     (unsigned) DERIVE_EXTENT_STACK_MAX);
        return -1;
      }
      if (extent_idx >= extent_count) {
        PyErr_SetString(PyExc_RuntimeError, "extent count mismatch");
        return -1;
      }

      if (extents_out) {
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
        top_extent_id_out[pc] = (uint16_t) extent_idx;  /* PUSH pc is inside */
      }

      stack[stack_top].push_pc = pc;
      stack[stack_top].extent_id = extent_idx;
      stack[stack_top].push_op = op;
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

      if (extents_out) {
        uint32_t eid = stack[stack_top - 1].extent_id;
        extents_out[eid].pop_pc = pc;

        /* Propagate extent id to all pcs inside [push_pc+1, pop_pc) */
        uint32_t enter_pc = extents_out[eid].push_pc + 1;
        for (uint32_t j = enter_pc; j < pc; j++) {
          if (top_extent_id_out[j] == KOS_EXTENT_NONE) {
            top_extent_id_out[j] = (uint16_t) eid;
          }
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
   Collect the jump destinations one instruction carries, as pcs.

   A KOSTYLE_LABEL opcode keeps its destination in the instruction arg.
   An EXTRA one keeps the arg as a row index and the destination in a
   LABEL field of that row, resolved to a KosOp during emit; the same
   scan collect_label_map makes. A PUSH_TRY or PUSH_FINALLY handler
   label is a destination too: the raise walk unwinds to the push's
   region and jumps there, so it is held to the push's own extent like
   any jump from inside it.

   :param out: receives up to 4 destination pcs
   :return: how many destinations were written
*/
static int jump_targets(
    const KosOp *code,
    uint32_t src_pc,
    const KosExtra *extra,
    uint32_t extra_len,
    uint32_t *out) {

  KosOpCode op = (KosOpCode) code[src_pc].op;
  KosOpStyle st = kos_opcode_style(op);
  int n = 0;

  if (st == KOSTYLE_LABEL) {
    out[0] = code[src_pc].arg;
    return 1;
  }

  if (st != KOSTYLE_EXTRA || ! extra)
    return 0;

  {
    uint32_t eidx = code[src_pc].arg;
    const KosExtra *row;
    KosExtraSchema schema = kos_opcode_extra_schema(op);

    /* A row index past the table is another validator's report. */
    if (eidx >= extra_len)
      return 0;

    row = &extra[eidx];
    for (int slot = 0; slot < 4; slot++) {
      if (schema_field(schema, slot) == KOSTYLE_LABEL)
        out[n++] = KosExtra_TARGET(row, slot)->pc;
    }
  }

  return n;
}


/**
   Validate JUMP locality: all jump targets must be inside the source block
   or at the block's pop_pc (leaving). Called after derive_extents.
   Applies to control-flow jumps and to PUSH_TRY/PUSH_FINALLY handler
   labels, whose source block is the push's own extent.
*/
/* Whether pc is the POP of some extent */
static int jump_target_is_pop(
    const KosExtentDesc *extents,
    uint32_t extent_len,
    uint32_t pc) {

  for (uint32_t i = 0; i < extent_len; i++) {
    if (extents[i].pop_pc == pc)
      return 1;
  }
  return 0;
}


static int validate_jump_locality(
    const KosOp *code,
    uint32_t code_len,
    const KosExtra *extra,
    uint32_t extra_len,
    const KosExtentDesc *extents,
    uint32_t extent_len,
    const uint16_t *top_extent_id) {

  if (! top_extent_id || extent_len == 0) {
    return 0;
  }

  /* For each jump, the target must be inside or at the boundary of the
     source extent. A PUSH pc carries its own extent's id, so a handler
     label is held to the handler's own block. */
  for (uint32_t src_pc = 0; src_pc < code_len; src_pc++) {
    uint32_t targets[4];
    int n_targets = jump_targets(code, src_pc, extra, extra_len, targets);

    if (! n_targets) {
      continue;
    }

    uint32_t eid = top_extent_id[src_pc];
    if (eid != KOS_EXTENT_NONE && eid >= extent_len) {
      continue;
    }

    for (int k = 0; k < n_targets; k++) {
      uint32_t t = targets[k];

      if (t >= code_len) {
        PyErr_Format(PyExc_RuntimeError,
                     "JUMP at pc %u targets out-of-range pc %u",
                     src_pc, t);
        return -1;
      }

      /* The extent whose body holds the target: a PUSH pc carries its
         own extent's id but runs the push, so it belongs to the
         enclosing one. A target in a block the source is not in would
         run the body with its push skipped, and the FAST slots of a
         PUSH_NS body were validated against a namespace that never
         got pushed. */
      uint32_t holder = top_extent_id[t];
      if (holder != KOS_EXTENT_NONE && t == extents[holder].push_pc)
        holder = extents[holder].parent_extent_id;

      /* A POP pc carries the parent's id, so a nested block's POP
         would read as the parent's body; it pops the source's own
         block only when it is the source's own POP. */
      if (eid != KOS_EXTENT_NONE && t == extents[eid].pop_pc)
        continue;
      if (jump_target_is_pop(extents, extent_len, t)) {
        PyErr_Format(PyExc_RuntimeError,
                     "JUMP at pc %u to pc %u: target is another block's"
                     " POP", src_pc, t);
        return -1;
      }

      if (holder == eid)
        continue;

      if (eid == KOS_EXTENT_NONE) {
        PyErr_Format(PyExc_RuntimeError,
                     "JUMP at pc %u to pc %u enters block [%u, %u) past"
                     " its push",
                     src_pc, t, extents[holder].push_pc + 1,
                     extents[holder].pop_pc);
      } else {
        PyErr_Format(PyExc_RuntimeError,
                     "non-local JUMP at pc %u to pc %u: target outside block"
                     " [%u, %u)",
                     src_pc, t, extents[eid].push_pc + 1,
                     extents[eid].pop_pc);
      }
      return -1;
    }
  }

  return 0;
}


/**
   Validate ITER_NEXT and its STORE twins are only used inside a
   PUSH_FOREACH block extent. Called after derive_extents.
*/
static int validate_iter_next_in_foreach(
    const KosOp *code,
    uint32_t code_len,
    const KosExtentDesc *extents,
    uint32_t extent_len,
    const uint16_t *top_extent_id) {

  if (! top_extent_id) {
    return 0;
  }

  /* the step must be inside a PUSH_FOREACH; walk parent chain to find */
  for (uint32_t pc = 0; pc < code_len; pc++) {
    KosOpCode op = (KosOpCode) code[pc].op;
    if (op != KOS_OP_ITER_NEXT && op != KOS_OP_ITER_NEXT_STORE_VAR
        && op != KOS_OP_ITER_NEXT_STORE_FAST) {
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
    if (kos_op_yields(lop)) {
      saw_yield = 1;
    } else if (lop >= KOS_OP_TAIL_CALL
               && lop <= KOS_OP_TAIL_CALL_FAST_ATTR_STAR) {
      /* the TAIL_CALL family is one contiguous run in opcodes.incl;
         additions belong inside the run so this range stays total */
      saw_tail = 1;
    }

    if (saw_yield && saw_tail) {
      PyErr_SetString(PyExc_ValueError,
                      "code containing yield or yield-from cannot use "
                      "TAIL_CALL family opcodes");
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

  if ((size_t) total > 0xFFFFFFFFu) {
    PyErr_SetString(PyExc_ValueError, "namespace symbol count too large");
    return -1;
  }

  *out_limit = (uint32_t) total;
  return 0;
}


/**
   The tuple constants of the match instructions, which their bodies
   index without a check: the names of MATCH_STORE, MATCH_CAPTURES,
   MATCH_PASS and MATCH_COLUMNS, and the slot indices of MATCH_CLEAR
   and MATCH_COLUMNS, each an exact tuple; MATCH_STORE's index within
   its names.

   :return: 0, or -1 with ValueError set
*/
static int validate_match_tuples(KosOpCode op, const KosExtra *row) {
  int fields[2] = {-1, -1};

  switch (op) {
  case KOS_OP_MATCH_STORE:
    fields[0] = 3;
    break;
  case KOS_OP_MATCH_CLEAR:
    fields[0] = 1;
    break;
  case KOS_OP_MATCH_CAPTURES:
  case KOS_OP_MATCH_PASS:
    fields[0] = 2;
    break;
  case KOS_OP_MATCH_COLUMNS:
    fields[0] = 2;
    fields[1] = 3;
    break;
  default:
    return 0;
  }

  for (int j = 0; j < 2 && fields[j] >= 0; j++) {
    PyObject *t = KosExtra_OBJ(row, fields[j]);
    if (! t || ! PyTuple_CheckExact(t)) {
      PyErr_Format(PyExc_ValueError,
                   "%s field %d must be a tuple",
                   kos_opcode_name(op), fields[j]);
      return -1;
    }
  }

  if (op == KOS_OP_MATCH_STORE
      && (Py_ssize_t) KosExtra_IMM(row, 1)
      >= PyTuple_GET_SIZE(KosExtra_OBJ(row, 3))) {
    PyErr_Format(PyExc_ValueError,
                 "MATCH_STORE slot %u outside its %zd names",
                 (unsigned) KosExtra_IMM(row, 1),
                 PyTuple_GET_SIZE(KosExtra_OBJ(row, 3)));
    return -1;
  }
  return 0;
}


/**
   Reject FASTVAR operands outside the active NamespaceSpec slot range.

   Called after emit when ``code`` and ``extra`` are fully materialized so we
   can validate both scalar FAST opcodes and FAST slots embedded in EXTRA rows
   (CALL_FAST-style operands). Mirrors ``Compiler.convert_fast``: walk the
   instruction stream in order, read the ``PUSH_NS`` nspec from the EXTRA row
   and the ``PUSH_COLLECT`` nspec from its operand, push/pop slot limits on
   ``PUSH_NS`` / ``PUSH_COLLECT`` / ``POP_NS`` / ``POP_COLLECT``. The active
   limit is the root ``Code`` NamespaceSpec when the namespace stack is
   empty, else the innermost pushed extent.

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
    const KosExtra *extra,
    uint32_t extra_len,
    PyObject **cpool,
    uint32_t cpool_len) {

  uint32_t root_limit = 0;

  if (namespace_spec_fast_slot_limit(KosCode_NSPEC(self), &root_limit)) {
    return -1;
  }

  uint32_t ns_stack[DERIVE_EXTENT_STACK_MAX];
  int depth = 0;

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
      if (depth == 0) {
        KosCode_FLAGS(self) |= KOS_CODE_USES_FAST;
        if ((uint32_t) code[i].arg + 1 > KosCode_FAST_NEED(self))
          KosCode_FAST_NEED(self) = (uint32_t) code[i].arg + 1;
      }
    } else if (st == KOSTYLE_EXTRA) {
      /* Walk schema: only slots tagged FASTVAR are checked */
      uint16_t eidx = code[i].arg;
      if (eidx >= extra_len) {
        PyErr_SetString(PyExc_RuntimeError, "EXTRA index out of bounds");
        return -1;
      }

      const KosExtra *row = &extra[eidx];
      /* A field the CMPSRC bits mark as CONST holds a pool object, not a
         fast slot, so it must not be range-checked against the namespace
         slot limit. */
      KosExtraSchema schema = extra_row_schema(op, KosExtra_IMM(row, 0));

      for (int slot = 0; slot < 4; slot++) {
        KosOpStyle fk = schema_field(schema, slot);

        if (fk != KOSTYLE_FASTVAR) {
          continue;
        }

        if (active == 0 || KosExtra_IMM(row, slot) >= active) {
          PyErr_Format(PyExc_ValueError,
                       "FAST index %u out of bounds (limit %u)",
                       (unsigned) KosExtra_IMM(row, slot), (unsigned) active);
          return -1;
        }

        if (depth == 0) {
          KosCode_FLAGS(self) |= KOS_CODE_USES_FAST;
          if ((uint32_t) KosExtra_IMM(row, slot) + 1 > KosCode_FAST_NEED(self))
            KosCode_FAST_NEED(self) = (uint32_t) KosExtra_IMM(row, slot) + 1;
        }
      }

      if (validate_match_tuples(op, row))
        return -1;
    }

    /* Advance namespace stack (mirror Compiler.convert_fast) */
    if (op == KOS_OP_PUSH_NS) {
      /* Nspec cpool index is extra row a0 (schema CONST, CONST, NOARG, NOARG). */
      uint16_t eidx = code[i].arg;
      if (eidx >= extra_len) {
        PyErr_SetString(PyExc_RuntimeError, "EXTRA index out of bounds");
        return -1;
      }
      PyObject *ns = KosExtra_OBJ(&extra[eidx], 0);
      if (! KosNamespaceSpec_CheckExact(ns)) {
        PyErr_Format(
            PyExc_TypeError,
            "PUSH_NS at pc %u spec must be a NamespaceSpec, not %.100s",
            i, Py_TYPE(ns)->tp_name);
        return -1;
      }
      uint32_t lim = 0;
      if (namespace_spec_fast_slot_limit(ns, &lim)) {
        return -1;
      }
      if (depth >= DERIVE_EXTENT_STACK_MAX) {
        PyErr_Format(PyExc_ValueError, "namespace stack overflow (limit %u)",
                     (unsigned) DERIVE_EXTENT_STACK_MAX);
        return -1;
      }
      ns_stack[depth++] = lim;
    } else if (op == KOS_OP_PUSH_COLLECT) {
      PyObject *ns = cpool[code[i].arg];
      if (! KosNamespaceSpec_CheckExact(ns)) {
        PyErr_Format(
            PyExc_TypeError,
            "PUSH_COLLECT at pc %u spec must be a NamespaceSpec, not %.100s",
            i, Py_TYPE(ns)->tp_name);
        return -1;
      }
      uint32_t lim = 0;
      if (namespace_spec_fast_slot_limit(ns, &lim)) {
        return -1;
      }
      if (depth >= DERIVE_EXTENT_STACK_MAX) {
        PyErr_Format(PyExc_ValueError, "namespace stack overflow (limit %u)",
                     (unsigned) DERIVE_EXTENT_STACK_MAX);
        return -1;
      }
      ns_stack[depth++] = lim;
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
   2. Allocate: single block for all regions (an arena chunk reservation
      with an allocator; page-aligned when protect).
   3. Emit: write instructions, positions, cpool, extra into block.
   4. Finalize: derive_extents_fill; validate jumps;
      with protect and no allocator, mprotect(PROT_READ) and set
      KOS_CODE_PROTECTED; with an allocator, take the chunk as the
      owning arena; set KosCode fields.

   The block stays writable until the final seal, so error paths release
   it without an unseal.

   Invariant: instruction streams that contain yield or yield-from must not
   contain TAIL_CALL, TAIL_CALL_NAMES, or TAIL_CALL_STAR (ValueError).
*/
static int code_instr_init(
    PyObject *self,
    PyObject *instr,
    int protect,
    PyObject *allocator) {

  SAYF("in code_instr_init(%R, %R)", self, instr);

  /* placement and protection both need a page size; where the system
     reports none or the build has no read-only code protection, the
     block is heap and neither applies, whatever a kio record asked */
  if (! KosArena_PageSize()) {
    allocator = NULL;
    protect = 0;
  }

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
  PyObject *chunk = NULL;
  KosOp *code = NULL;
  KosPosition *pos = NULL;
  PyObject **cpool = NULL;
  KosExtra *extra = NULL;
  KosExtentDesc *extents = NULL;
  uint16_t *top_extent_id = NULL;

  if (code_alloc_unified(&coll, protect, allocator, &chunk,
                         &base, &alloc_size,
                         &code, &pos, &cpool, &extra,
                         &extents, &top_extent_id)) {
    code_collect_clear(&coll);
    Py_DECREF(instr_fast);
    return -1;
  }

  /* Phase 3: Emit — write instructions and tables into block */
  if (code_emit(self, instr_fast, &coll, code, pos, cpool, extra)) {
    code_collect_clear(&coll);
    code_free_unified(base, alloc_size, chunk);
    Py_DECREF(instr_fast);
    return -1;
  }

  /* Before sealing: ensure FAST indices match active NamespaceSpec per extent */
  if (validate_fast_slots(self, code, code_len, extra, extra_len,
                          cpool, cpool_len)) {
    code_collect_clear(&coll);
    Py_DECREF(instr_fast);
    goto reject;
  }

  code_collect_clear(&coll);
  Py_DECREF(instr_fast);

  /* Phase 4: Finalize — fill extent table, validate, seal. The
     buffers are NULL when no PUSH_* was counted, and the walk then
     validates alone, refusing a stray POP_*. */
  if (derive_extents_fill(code, code_len, extents, extent_count,
                          top_extent_id))
    goto reject;

  if (extents && top_extent_id &&
      validate_jump_locality(code, code_len, extra, extra_len, extents,
                             extent_count, top_extent_id))
    goto reject;

  if (extents && top_extent_id &&
      validate_iter_next_in_foreach(code, code_len, extents,
                                    extent_count, top_extent_id))
    goto reject;

  code_fill_derived(code, code_len, extra);

  /* Seal block read-only; single mprotect for entire allocation. A
     chunk member is written, is sealed with its chunk, and keeps the
     reference the reservation took. */
  if (chunk) {
    KosArena_Complete(chunk);
    KosCode_ARENA(self) = chunk;

  } else if (protect) {
    if (mprotect(base, alloc_size, PROT_READ)) {
      PyErr_SetFromErrno(PyExc_OSError);
      goto reject;
    }
    KosCode_FLAGS(self) |= KOS_CODE_PROTECTED;
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

 reject:
  /* a rejection after emit: the pool entries are owned and the block
     is still writable */
  code_release_cpool(cpool, cpool_len);
  code_free_unified(base, alloc_size, chunk);
  return -1;
}


/* === Python type methods === */


/**
   Record the local slot the code's function binds itself in.

   :param code: the new KosCode
   :param self_slot: an index among the namespace spec's locals, or -1
     for none

   :return: 0 on success, -1 with ValueError set when self_slot names
     no local
*/
static int code_set_self_slot(PyObject *code, int self_slot) {

  if (self_slot == -1)
    return 0;

  PyObject *loc = KosNamespaceSpec_LOCAL_SYMS(KosCode_NSPEC(code));
  Py_ssize_t nloc = (loc && loc != Py_None) ? PySequence_Size(loc) : 0;
  if (nloc < 0)
    return -1;

  if (self_slot < 0 || self_slot >= nloc) {
    PyErr_Format(PyExc_ValueError, "Code self_slot %d names none of its"
                 " %zd locals", self_slot, nloc);
    return -1;
  }

  KosCode_SELF_SLOT(code) = self_slot;
  return 0;
}


static PyObject *code_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {

  PyObject *nspec = NULL;
  PyObject *instr = NULL;
  PyObject *name = NULL;
  PyObject *filename = NULL;
  int protect = 1;
  int cache = 1;
  PyObject *allocator = NULL;
  int self_slot = -1;

  static char *keywords[] = {
    "nspec", "instr", "name", "filename", "protect", "cache", "allocator",
    "self_slot", NULL,
  };

  if (! PyArg_ParseTupleAndKeywords(args, kwds, "OO|OO$ppOi:Code", keywords,
                                    &nspec, &instr, &name, &filename,
                                    &protect, &cache, &allocator,
                                    &self_slot)) {
    return NULL;
  }

  if (allocator == Py_None) {
    allocator = NULL;
  }

  PyObject *code = KosCode_NewWithAllocator(nspec, instr, name, filename,
                                            protect, cache, allocator);
  if (code && code_set_self_slot(code, self_slot)) {
    Py_CLEAR(code);
  }

  return code;
}


/*
   KosCode is GC-tracked for its trailing per-site cache slots alone.
   Everything else a Code reaches was created before the Code and is
   immutable from Python (cpool consts and FunctionSpecs, the resolved
   operands inlined into instruction rows, READONLY nspec name tuples,
   the synthesized faux), so a reference cycle cannot route through it
   and tp_traverse does not walk it. A populated cache slot is
   different: a method slot owns the receiver type and the resolved
   function, so a class whose methods call through a cached site forms
   type -> function -> code -> slot -> type. tp_traverse visits the
   owned references of every populated slot and tp_clear releases them,
   which is all the collector needs to break that cycle.
*/


/**
   Release the owned references held by one cache slot, dispatching on
   its kind, and return it to KOS_CACHE_EMPTY. An empty slot (the
   framework state) owns nothing.
*/
static void cache_slot_release(KosCacheSlot *slot) {

  switch (slot->kind) {
  case KOS_CACHE_EMPTY:
    break;
  case KOS_CACHE_METHOD:
    Py_CLEAR(slot->u.method.type);
    Py_CLEAR(slot->u.method.resolved);
    break;
  case KOS_CACHE_FIELD:
    /* ref-free: a borrowed guard type and an integer index. */
    break;
  case KOS_CACHE_ELECTOR:
  case KOS_CACHE_ITEM:
    /* ref-free: a static function and two counters. */
    break;
  case KOS_CACHE_TARGET:
    /* ref-free: one record pointer. */
    break;
  case KOS_CACHE_MATCH_ARGS:
    /* ref-free: a borrowed class and tuple under a version guard. */
    break;
  }
  memset(slot, 0, sizeof(*slot));
}


/**
   Visit the owned references held by one cache slot, the traverse
   counterpart of cache_slot_release.
*/
static int cache_slot_visit(KosCacheSlot *slot, visitproc visit, void *arg) {

  switch (slot->kind) {
  case KOS_CACHE_EMPTY:
  case KOS_CACHE_FIELD:
  case KOS_CACHE_ELECTOR:
  case KOS_CACHE_ITEM:
  case KOS_CACHE_TARGET:
  case KOS_CACHE_MATCH_ARGS:
    break;
  case KOS_CACHE_METHOD:
    Py_VISIT(slot->u.method.type);
    Py_VISIT(slot->u.method.resolved);
    break;
  }
  return 0;
}


static int code_traverse(PyObject *self, visitproc visit, void *arg) {
  for (uint16_t i = KosCode_CACHE_LEN(self); i--; ) {
    int res = cache_slot_visit(&KosCode_CACHE(self)[i], visit, arg);
    if (res)
      return res;
  }
  return 0;
}


static int code_clear(PyObject *self) {
  for (uint16_t i = KosCode_CACHE_LEN(self); i--; ) {
    cache_slot_release(&KosCode_CACHE(self)[i]);
  }
  return 0;
}


static void code_dealloc(PyObject *self) {

  PyObject_GC_UnTrack(self);

  if (((KosCode *) self)->weakreflist) {
    PyObject_ClearWeakRefs(self);
  }

  Py_CLEAR(KosCode_NAME(self));
  Py_CLEAR(KosCode_FILENAME(self));
  Py_CLEAR(KosCode_NSPEC(self));
  Py_CLEAR(KosCode_FAUX(self));
  Py_CLEAR(KosCode_EXTRAS(self));

  for (uint32_t i = KosCode_CPOOL_LEN(self); i--; ) {
    Py_DECREF(KosCode_GET_CONST(self, i));
  }

  void *base = KosCode_CODE(self);
  if (KosCode_ARENA(self)) {
    /* the arena owns the block memory: hand the block back, which an
       open chunk reuses when it is the newest, and drop our reference */
    if (base)
      KosArena_Release(KosCode_ARENA(self), base, KosCode_ALLOCATION(self));
    Py_CLEAR(KosCode_ARENA(self));
  } else if (base) {
    if (KosCode_PROTECTED(self)) {
      mprotect(base, KosCode_ALLOCATION(self), PROT_WRITE);
    }
    free(base);
  }

  KosCode_FLAGS(self) &= ~KOS_CODE_PROTECTED;
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

  /* Drop owned references held by cache slots, per-kind. The slots
     themselves are part of the object allocation and are released by
     tp_free; only the owned refs inside a live layout need dropping. */
  code_clear(self);

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
  sizeof(KosCacheSlot),   /* tp_itemsize: trailing per-site cache slots */

  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
  .tp_traverse = code_traverse,
  .tp_clear = code_clear,
  .tp_repr = code_repr,
  .tp_getset = code_getset,
  .tp_members = code_members,
  .tp_methods = code_methods,
  .tp_new = code_new,
  .tp_weaklistoffset = offsetof(KosCode, weakreflist),
  .tp_dealloc = code_dealloc,
};


/* === Constructor === */


/* The one shape check for user-supplied instructions, run before any
   reader touches the items: every item is a list or tuple (the only
   types the PySequence_Fast item macros are defined for) of at least
   two elements whose first is an int in the opcode range. THEEND is
   the end-of-code barrier appended by finalize and is not accepted. */
static int validate_instructions(PyObject *instr) {
  Py_ssize_t n = PySequence_Fast_GET_SIZE(instr);

  for (Py_ssize_t i = 0; i < n; i++) {
    PyObject *pair = PySequence_Fast_GET_ITEM(instr, i);

    if (! (PyList_Check(pair) || PyTuple_Check(pair))) {
      PyErr_Format(PyExc_TypeError, "Code instruction %zd must be a list or"
                   " tuple, not %.100s", i, Py_TYPE(pair)->tp_name);
      return -1;
    }
    if (PySequence_Fast_GET_SIZE(pair) < 2) {
      PyErr_Format(PyExc_ValueError, "Code instruction %zd must have an"
                   " opcode and an argument", i);
      return -1;
    }

    PyObject *a = PySequence_Fast_GET_ITEM(pair, 0);
    if (! PyLong_Check(a)) {
      PyErr_Format(PyExc_TypeError, "Code instruction %zd opcode must be an"
                   " int, not %.100s", i, Py_TYPE(a)->tp_name);
      return -1;
    }

    long lop = PyLong_AsLong(a);
    if (lop == (long) KOS_OP_THEEND) {
      /* the end-of-code barrier is appended by finalize; it is not an
         instruction a caller may supply */
      PyErr_Format(PyExc_ValueError, "Code instruction %zd is THEEND, the"
                   " end-of-code barrier the assembler appends itself", i);
      return -1;
    }
    if ((lop == -1 && PyErr_Occurred())
        || lop < (long) KOS_OP_NOOP || lop > (long) KOS_OP_THEEND) {
      PyErr_Clear();
      PyErr_Format(PyExc_ValueError, "Code instruction %zd opcode %R is"
                   " undefined", i, a);
      return -1;
    }
  }

  return 0;
}


/**
   Sum the per-site cache slots requested across a softened instruction
   sequence. Each instruction's opcode declares how many slots it wants via
   kos_opcode_cache_count; the total sizes the KosCode's trailing slot array.
   Pseudo-ops (LABEL/POSITION) request zero, so counting the whole sequence
   matches the assignment pass in code_emit.

   :param instr: fast-sequence of (op, arg) pairs
   :param out: receives the slot total on success
   :return: 0 on success; -1 (with exception) if the total would exceed the
     index range (KOS_CACHE_UNSET is reserved as the unset sentinel)
*/
static int count_cache_slots(PyObject *instr, uint16_t *out) {

  Py_ssize_t n = PySequence_Fast_GET_SIZE(instr);
  size_t total = 0;

  for (Py_ssize_t i = 0; i < n; i++) {
    PyObject *pair = PySequence_Fast_GET_ITEM(instr, i);
    PyObject *a = PySequence_Fast_GET_ITEM(pair, 0);
    KosOpCode op = (KosOpCode) PyLong_AsLong(a);
    total += kos_opcode_cache_count(op);
  }

  if (total >= KOS_CACHE_UNSET) {
    PyErr_Format(PyExc_OverflowError, "code requests %zu cache slots, over"
                 " the per-object limit of %u", total, KOS_CACHE_UNSET - 1);
    return -1;
  }

  *out = (uint16_t) total;
  return 0;
}


PyObject *KosCode_New(
    PyObject *nspec,
    PyObject *instr,
    PyObject *name,
    PyObject *filename,
    int protect,
    int cache) {

  return KosCode_NewWithAllocator(nspec, instr, name, filename,
                                  protect, cache, NULL);
}


PyObject *KosCode_NewWithAllocator(
    PyObject *nspec,
    PyObject *instr,
    PyObject *name,
    PyObject *filename,
    int protect,
    int cache,
    PyObject *allocator) {

  if (allocator && ! KosCodeAllocator_Check(allocator)) {
    PyErr_Format(PyExc_TypeError, "Code allocator must be a"
                 " CodeAllocator, not %R", Py_TYPE(allocator));
    return NULL;
  }

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

  if (validate_instructions(instr)) {
    Py_DECREF(instr);
    return NULL;
  }

  /* Pragma gate: when caching is off for this code object, allocate no slots
     and leave every instruction's index unset. Otherwise size the trailing
     array to the summed per-opcode counts. */
  uint16_t cache_slots = 0;
  if (cache && count_cache_slots(instr, &cache_slots)) {
    Py_DECREF(instr);
    return NULL;
  }

  /* Var-length: the object allocation includes cache_slots trailing
     KosCacheSlot entries, zeroed by tp_alloc. Zero = UNCACHED, so the table
     needs no init pass. ob_size holds the count (KosCode_CACHE_LEN). The
     type is GC-tracked, so tp_alloc also tracks the object; traverse
     reads only the (already zeroed) slots, which is safe from here on. */
  PyObject *self = PyType_GenericAlloc(&KosCodeType, (Py_ssize_t) cache_slots);
  if (! self) {
    Py_DECREF(instr);
    return NULL;
  }

  ((KosCode *) self)->weakreflist = NULL;
  KosCode_ARENA(self) = NULL;
  KosCode_FAST_NEED(self) = 0;
  KosCode_SELF_SLOT(self) = -1;

  /* Record the cache gate so code_emit assigns slots only when enabled. */
  KosCode_FLAGS(self) = cache ? KOS_CODE_CACHE : 0;

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
  if (! KosCode_EXTRAS(self)) {
    Py_CLEAR(self);
    Py_DECREF(instr);
    return NULL;
  }

  if (code_instr_init(self, instr, protect, allocator)) {
    Py_CLEAR(self);
    Py_DECREF(instr);
    return NULL;
  }

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

  return op_as_tuple(KosCode_CODE(self)[index]);
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

  uint16_t *const top = KosCode_TOP_EXTENT_ID(code);
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
  const KosExtentDesc *const extents = KosCode_EXTENTS(code);
  uint32_t extent_len = KosCode_EXTENT_LEN(code);

  while (eid != KOS_EXTENT_NONE && eid < extent_len) {
    if (extents[eid].kind == kind) {
      return eid;
    }
    eid = extents[eid].parent_extent_id;
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


KosOp *kos_shift_ic_for(PyObject *code, uint32_t pc) {

  uint32_t eid = kos_extent_find(code, pc, KOS_EXT_SHIFT);
  if (eid == KOS_EXTENT_NONE) {
    return NULL;
  }

  const KosExtentDesc *const e = kos_extent_desc(code, eid);
  if (! e) {
    return NULL;
  }

  return &KosCode_CODE(code)[e->pop_pc + 1];
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


/**
   Add one member to an enum dict. Returns 0, or -1 with an exception
   set when the int or the store fails.
*/
static int enum_dict_add(PyObject *enum_dict, const char *name, long value) {

  PyObject *v = PyLong_FromLong(value);
  if (! v)
    return -1;
  int ret = PyDict_SetItemString(enum_dict, name, v);
  Py_DECREF(v);
  return ret;
}


/* Add one member to an enum dict, keyed by the bare suffix of its C
   constant, or leave the function at its fail label. The prefix exists
   so the constant stands alone in C; from the object model the enum
   class already supplies that context, so OpCode.CONST rather than
   OpCode.KOS_OP_CONST. */
#define int_enum(enum_dict, prefix, suffix)                        \
  do {                                                             \
    if (enum_dict_add(enum_dict, #suffix, prefix ## suffix) < 0)   \
      goto fail;                                                   \
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

  int_enum(enum_dict, KOSTYLE_, COMPILE);
  int_enum(enum_dict, KOSTYLE_, LABEL);
  int_enum(enum_dict, KOSTYLE_, NOARG);
  int_enum(enum_dict, KOSTYLE_, COUNT);
  int_enum(enum_dict, KOSTYLE_, PAIR_COUNT);
  int_enum(enum_dict, KOSTYLE_, STAR);
  int_enum(enum_dict, KOSTYLE_, CONST);
  int_enum(enum_dict, KOSTYLE_, ATTR);
  int_enum(enum_dict, KOSTYLE_, VAR);
  int_enum(enum_dict, KOSTYLE_, FASTVAR);
  int_enum(enum_dict, KOSTYLE_, OPCOUNT);
  int_enum(enum_dict, KOSTYLE_, EXTRA);
  int_enum(enum_dict, KOSTYLE_, CMP);
  int_enum(enum_dict, KOSTYLE_, CMPCOUNT);
  int_enum(enum_dict, KOSTYLE_, CMPSRC);
  int_enum(enum_dict, KOSTYLE_, OPERAND);
  int_enum(enum_dict, KOSTYLE_, RAW);

  return create_int_enum("OpStyle", enum_dict);

fail:
  Py_DECREF(enum_dict);
  return NULL;
}


PyObject *KosOpCodeEnum = NULL;


/**
   Fetch the OpStyle enum member for a raw style ordinal.

   :param styles: the OpStyle IntEnum
   :return: new reference, or NULL with an exception set
*/
static PyObject *opstyle_member(PyObject *styles, KosOpStyle style) {

  return PyObject_CallFunction(styles, "i", (int) style);
}


/**
   Build an EXTRA opcode's field schema as a 4-tuple of OpStyle members.

   :param styles: the OpStyle IntEnum
   :return: new reference, or NULL with an exception set
*/
static PyObject *opstyle_schema(PyObject *styles, KosExtraSchema ex) {

  const KosOpStyle fields[4] = { ex.f0, ex.f1, ex.f2, ex.f3 };

  PyObject *res = PyTuple_New(4);
  if (! res)
    return NULL;

  for (int i = 0; i < 4; i++) {
    PyObject *member = opstyle_member(styles, fields[i]);
    if (! member) {
      Py_DECREF(res);
      return NULL;
    }
    PyTuple_SET_ITEM(res, i, member);
  }

  return res;
}


/**
   The OpCode member for an opcode, or None for KOS_OP_NOOP, which the
   extent pair lookups return for an opcode outside the table.

   :param opcodes: the OpCode IntEnum
   :return: new reference, or NULL with an exception set
*/
static PyObject *opcode_member_or_none(PyObject *opcodes, KosOpCode op) {

  if (op == KOS_OP_NOOP)
    Py_RETURN_NONE;
  return PyObject_CallFunction(opcodes, "i", (int) op);
}


/**
   Set one attribute on an opcode member, consuming value.

   :return: 0 on success, -1 with an exception set
*/
static int set_opcode_attr(PyObject *member, const char *name,
    PyObject *value) {

  int rc;

  if (! value)
    return -1;
  rc = PyObject_SetAttrString(member, name, value);
  Py_DECREF(value);
  return rc ? -1 : 0;
}


/**
   Attach the attributes every opcode member carries, so consumers can
   read each without a presence check: ``style`` is an OpStyle member;
   ``extra`` is the field schema of an EXTRA opcode, or an empty tuple
   for every other style; ``yields`` says whether the row makes a code
   object a generator; ``pop`` is the paired POP_* member of a PUSH_*
   row and ``push`` the paired PUSH_* member of a POP_* row, None
   otherwise; ``discardable`` says whether a tail call may pass through
   the extent a PUSH_* row opens, False for every other row. The extent
   facts come from extent_pairs, so the compiler's tables derive from
   the one classification the validator and extent derivation use.

   :param opcodes: the OpCode IntEnum
   :param styles: the OpStyle IntEnum
   :return: 0 on success, -1 with an exception set
*/
static int decorate_opcode(PyObject *member, PyObject *opcodes,
    PyObject *styles, KosOpCode op) {

  KosOpStyle style = opcode_styles[op].style;
  PyObject *extra;

  if (set_opcode_attr(member, "style", opstyle_member(styles, style)))
    return -1;

  if (style == KOSTYLE_EXTRA) {
    extra = opstyle_schema(styles, opcode_styles[op].extra);
  } else {
    extra = PyTuple_New(0);
  }
  if (set_opcode_attr(member, "extra", extra))
    return -1;

  if (set_opcode_attr(member, "yields", PyBool_FromLong(kos_op_yields(op))))
    return -1;
  if (set_opcode_attr(member, "pop",
                      opcode_member_or_none(opcodes, pop_for_push(op))))
    return -1;
  if (set_opcode_attr(member, "push",
                      opcode_member_or_none(opcodes, push_for_pop(op))))
    return -1;
  return set_opcode_attr(member, "discardable",
                         PyBool_FromLong(discardable_for_push(op)));
}


static PyObject *create_opcode_enum(PyObject *styles) {

  PyObject *res;
  PyObject *enum_dict = PyDict_New();
  if (! enum_dict)
    return NULL;

#define OP(name, ...)  int_enum(enum_dict, KOS_OP_, name);
#include "opcodes.incl"
#undef OP

  res = create_int_enum("OpCode", enum_dict);
  if (! res)
    return NULL;

  /* Attach the style, extra schema, yield and extent facts to each
     opcode; every member exists by now, so a pair can name its twin */
  for (KosOpCode op = 0; op <= KOS_OP_THEEND; op++) {
    PyObject *member = PyObject_GetAttrString(res, opcode_names[op]);
    if (! member) {
      Py_DECREF(res);
      return NULL;
    }

    int rc = decorate_opcode(member, res, styles, op);
    Py_DECREF(member);
    if (rc) {
      Py_DECREF(res);
      return NULL;
    }
  }

  return res;

fail:
  Py_DECREF(enum_dict);
  return NULL;
}


PyObject *KosOperandEnum = NULL;


/* IntEnum of the parameterized arithmetic operators (KosOperand, bytecode.h),
   exposed as koskari.lib.Operand so the Python compiler and the C dispatch
   share one op-type ordinal set. Written directly; keep in step with the
   KosOperand enum. */
static PyObject *create_operand_enum(void) {

  PyObject *enum_dict = PyDict_New();
  if (! enum_dict)
    return NULL;

  int_enum(enum_dict, KOS_OPERAND_, ADD);
  int_enum(enum_dict, KOS_OPERAND_, SUBTRACT);
  int_enum(enum_dict, KOS_OPERAND_, MULTIPLY);
  int_enum(enum_dict, KOS_OPERAND_, MODULO);
  int_enum(enum_dict, KOS_OPERAND_, BIT_AND);
  int_enum(enum_dict, KOS_OPERAND_, BIT_OR);
  int_enum(enum_dict, KOS_OPERAND_, BIT_XOR);
  int_enum(enum_dict, KOS_OPERAND_, BIT_LEFT);
  int_enum(enum_dict, KOS_OPERAND_, BIT_RIGHT);
  int_enum(enum_dict, KOS_OPERAND_, TRUE_DIVIDE);
  int_enum(enum_dict, KOS_OPERAND_, FLOOR_DIVIDE);
  int_enum(enum_dict, KOS_OPERAND_, MATRIX_MULTIPLY);
  int_enum(enum_dict, KOS_OPERAND_, POWER);

  return create_int_enum("Operand", enum_dict);

fail:
  Py_DECREF(enum_dict);
  return NULL;
}


PyObject *KosCompareEnum = NULL;
PyObject *KosCoreTypeEnum = NULL;


/* IntEnum of the comparison selectors (KosCompare, bytecode.h), exposed as
   koskari.lib.Compare so the Python compiler and the C dispatch share one
   selector ordinal set. Written directly; keep in step with the KosCompare
   enum (values 0..5 are CPython's Py_LT..Py_GE ordinals). */
static PyObject *create_compare_enum(void) {

  PyObject *enum_dict = PyDict_New();
  if (! enum_dict)
    return NULL;

  int_enum(enum_dict, KOS_COMPARE_, LT);
  int_enum(enum_dict, KOS_COMPARE_, LE);
  int_enum(enum_dict, KOS_COMPARE_, EQ);
  int_enum(enum_dict, KOS_COMPARE_, NE);
  int_enum(enum_dict, KOS_COMPARE_, GT);
  int_enum(enum_dict, KOS_COMPARE_, GE);
  int_enum(enum_dict, KOS_COMPARE_, IS);
  int_enum(enum_dict, KOS_COMPARE_, IS_NOT);
  int_enum(enum_dict, KOS_COMPARE_, IN);
  int_enum(enum_dict, KOS_COMPARE_, NOT_IN);
  int_enum(enum_dict, KOS_COMPARE_, IS_INSTANCE);
  int_enum(enum_dict, KOS_COMPARE_, IS_SUBCLASS);

  return create_int_enum("Compare", enum_dict);

fail:
  Py_DECREF(enum_dict);
  return NULL;
}


/* IntEnum of the core types a pattern names by their bare spelling
   (KosCoreType, matcher.h), exposed as koskari.lib.CoreType so the match
   compiler and the MATCH_CORE handler share one ordinal set. */
static PyObject *create_core_type_enum(void) {

  PyObject *enum_dict = PyDict_New();
  if (! enum_dict)
    return NULL;

  int_enum(enum_dict, KOS_CORE_, PAIR);
  int_enum(enum_dict, KOS_CORE_, SYMBOL);
  int_enum(enum_dict, KOS_CORE_, KEYWORD);
  int_enum(enum_dict, KOS_CORE_, INT);
  int_enum(enum_dict, KOS_CORE_, STR);
  int_enum(enum_dict, KOS_CORE_, TUPLE);
  int_enum(enum_dict, KOS_CORE_, LIST);

  return create_int_enum("CoreType", enum_dict);

fail:
  Py_DECREF(enum_dict);
  return NULL;
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

  if (PyModule_AddStringConstant(mod, "SOFTEN_LABEL_PREFIX",
                                 KosCode_SOFTEN_LABEL_PREFIX)) {
    return -1;
  }

  PyObject *stuff;

  /* OpStyle first: the opcode members carry OpStyle values */
  stuff = create_opstyle_enum();
  if (! stuff) {
    return -1;
  }
  if (PyDict_SetItemString(dict, "OpStyle", stuff)) {
    Py_DECREF(stuff);
    return -1;
  }
  KosOpStyleEnum = stuff;

  stuff = create_opcode_enum(KosOpStyleEnum);
  if (! stuff) {
    return -1;
  }
  if (PyDict_SetItemString(dict, "OpCode", stuff)) {
    Py_DECREF(stuff);
    return -1;
  }
  KosOpCodeEnum = stuff;

  stuff = create_operand_enum();
  if (! stuff) {
    return -1;
  }
  if (PyDict_SetItemString(dict, "Operand", stuff)) {
    Py_DECREF(stuff);
    return -1;
  }
  KosOperandEnum = stuff;

  stuff = create_compare_enum();
  if (! stuff) {
    return -1;
  }
  if (PyDict_SetItemString(dict, "Compare", stuff)) {
    Py_DECREF(stuff);
    return -1;
  }
  KosCompareEnum = stuff;

  stuff = create_core_type_enum();
  if (! stuff) {
    return -1;
  }
  if (PyDict_SetItemString(dict, "CoreType", stuff)) {
    Py_DECREF(stuff);
    return -1;
  }
  KosCoreTypeEnum = stuff;

  STR_CONST(_str_anon, "<anon>");

  return 0;
}


int koskari_bytecode_clear(PyObject *mod) {

  Py_CLEAR(KosOpCodeEnum);
  Py_CLEAR(KosOpStyleEnum);
  Py_CLEAR(KosOperandEnum);
  Py_CLEAR(KosCompareEnum);
  Py_CLEAR(KosCoreTypeEnum);
  Py_CLEAR(_str_anon);

  return 0;
}


// The end.
