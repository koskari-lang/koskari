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
   koskari/lib/bytecode.h

   Bytecode storage object for Koskari. The KosCode type holds
   immutable bytecode instructions, a constants pool, and a
   delta-encoded positions table in a single unified block. When
   constructed with protect, the block is page-aligned and marked
   read-only via mprotect; the block may later be relocated into a
   shared, singly-sealed KosArena (see arena.h) via pack_into.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
*/


#ifndef KOSKARI_BYTECODE_H
#define KOSKARI_BYTECODE_H


#include <Python.h>
#include <stdint.h>


/* === Opcode style enum === */


extern PyObject *KosOpStyleEnum;


typedef enum KosOpStyle {

  /** opcode is a compile-time feature, not emitted */
  KOSTYLE_COMPILE,

  /** opcode argument is a label (jump target) */
  KOSTYLE_LABEL,

  /** opcode argument is unused */
  KOSTYLE_NOARG,

  /** opcode argument is a count of elements on stack */
  KOSTYLE_COUNT,

  /** opcode argument is a count of paired elements on stack */
  KOSTYLE_PAIR_COUNT,

  /** opcode argument is a bitflag for star call conventions */
  KOSTYLE_STAR,

  /** opcode argument is a constant pool index */
  KOSTYLE_CONST,

  /** opcode argument is a variable name (stored in cpool) */
  KOSTYLE_VAR,

  /** opcode argument is a fast variable index */
  KOSTYLE_FASTVAR,

  /** opcode argument is an index into the extra table */
  KOSTYLE_EXTRA,

} KosOpStyle;


/* === Extra operand table === */


/**
   Schema describing how the four slots of a KosExtra row are
   interpreted for a given EXTRA opcode.
*/
typedef struct KosExtraSchema {
  KosOpStyle f0;
  KosOpStyle f1;
  KosOpStyle f2;
  KosOpStyle f3;
} KosExtraSchema;


/**
   Extended operand row. For EXTRA opcodes, arg is an index into
   KosExtra[]. Interpretation of a0..a3 is opcode-specific.
*/
typedef struct KosExtra {
  uint16_t a0;
  uint16_t a1;
  uint16_t a2;
  uint16_t a3;
} KosExtra;


/* === Opcode enum === */


extern PyObject *KosOpCodeEnum;


typedef enum KosOpCode {

#define OP(name, ...)  KOS_OP_##name ,

#include "opcodes.incl"
#undef OP

} KosOpCode;


/* === Code flags === */


typedef enum KosCodeFlag {
  /** code contains yield/yield_from, returns async generator */
  KOS_CODE_YIELDS = 0x01,
  /** code contains FAST-slot opcodes at root namespace scope */
  KOS_CODE_USES_FAST = 0x02,
  /** unified block is page-aligned and mprotect(PROT_READ)-sealed;
      teardown must unseal before freeing */
  KOS_CODE_PROTECTED = 0x04,
} KosCodeFlag;


/* === KosOp: single instruction === */


/** Cache-index value meaning "no slot reserved for this instruction".
    Every instruction defaults to this; the assembler overwrites it only for
    cache-consuming opcodes when caching is enabled for the code object.
    See design/interpreter/attr-lookup-cache.md. */
#define KOS_CACHE_UNSET 0xFFFF


/**
   A single bytecode instruction: 6 bytes total.
   Opcode, argument, and per-site cache index are all 16-bit unsigned
   integers. ``cache`` is the index of this site's reserved cache slot on the
   owning KosCode, or KOS_CACHE_UNSET when the site has no slot. The index is
   assigned at assembly and never serialized; soften emits only (op, arg).
*/
typedef struct KosOp {
  uint16_t op;    /* KosOpCode */
  uint16_t arg;   /* argument value */
  uint16_t cache; /* per-site cache slot index, or KOS_CACHE_UNSET */
} KosOp;


#define KosOp_OP(insn)    (((KosOp *) (insn))->op)
#define KosOp_ARG(insn)   (((KosOp *) (insn))->arg)
#define KosOp_CACHE(insn) (((KosOp *) (insn))->cache)


/* === KosPosition: position change entry === */


/**
   A position table entry: 6 bytes total.
   Records the source position at a specific instruction index.
   Only stored when position changes from the previous instruction.
*/
typedef struct KosPosition {
  uint16_t index;   /* instruction index where position changes */
  uint16_t line;    /* source line number */
  uint16_t col;     /* source column number */
} KosPosition;


/* === Extent kinds and descriptor === */


#define KOS_EXTENT_NONE 0xFFFF


typedef enum KosExtentKind {
  KOS_EXT_TRY,
  KOS_EXT_LOOP,
  KOS_EXT_PROMPT,
  KOS_EXT_CC,
  KOS_EXT_SHIFT,
  KOS_EXT_SHIFT_BODY,  /* deprecated; use KOS_EXT_SHIFT */
  KOS_EXT_CALLCC_BIND,
  KOS_EXT_FINALLY,
  KOS_EXT_WIND,
  KOS_EXT_WITH,
  KOS_EXT_DYN_WITH,
  KOS_EXT_FOREACH,
  KOS_EXT_NS,
  KOS_EXT_COLLECT,
  KOS_EXT_PARAM,
} KosExtentKind;


/**
   Static extent descriptor. Describes a block extent with push_pc and
   pop_pc. Handler and jump target PCs are derived on demand from
   code[push_pc] and pop_pc as needed.
*/
typedef struct KosExtentDesc {
  KosExtentKind kind;
  uint32_t push_pc;
  uint32_t pop_pc;
  uint32_t parent_extent_id;

  uint32_t flags;
} KosExtentDesc;


/* === KosCode: bytecode object === */


extern PyTypeObject KosCodeType;


struct KosCode {
  PyObject_HEAD

  PyObject *name;       /* function/expression name (PyUnicode) */
  PyObject *filename;   /* source filename (PyUnicode) */
  PyObject *nspec;      /* namespace spec (KosNamespaceSpec) */

  uint32_t flags;       /* KosCodeFlag bitfield */
  size_t allocation;    /* total mmap allocation size */

  uint32_t code_len;    /* instruction count */
  KosOp *code;          /* instruction array */

  uint32_t pos_len;     /* position entry count */
  KosPosition *pos;     /* positions table */

  uint32_t cpool_len;   /* constant pool size */
  PyObject **cpool;     /* constant pool */

  uint32_t extra_len;   /* extra operand table size */
  KosExtra *extra;      /* extra operand table */

  uint32_t extent_len;
  KosExtentDesc *extents;
  uint16_t *top_extent_id;

  PyCodeObject *faux;   /* faux code object for tracebacks */
  PyObject *extras;     /* extra metadata (PyDict) */

  /* strong reference to the owning KosArena when the unified block has
     been relocated into a shared arena; NULL for an unpacked object.
     When set, the arena owns the block memory and teardown must not
     free it. Non-GC leaf, never part of a cycle. */
  PyObject *arena;

  PyObject *weakreflist;  /* weak reference support */
};


/* === Constructor === */


/**
   Create a new KosCode object.

   :param nspec: namespace spec (KosNamespaceSpec)
   :param instr: sequence of (opcode, argument) pairs
   :param name: function/expression name (PyUnicode or NULL)
   :param filename: source filename (PyUnicode or NULL)
   :param protect: nonzero to page-align the unified block and seal it
     read-only with mprotect; zero for a plain writable malloc block

   :return: new KosCode reference, or NULL on error
*/
PyObject *KosCode_New(
    PyObject *nspec,
    PyObject *instr,
    PyObject *name,
    PyObject *filename,
    int protect);


/* === Accessors === */


/**
   Get a constant from the constant pool.

   :param self: KosCode object
   :param index: constant pool index

   :return: borrowed reference, or NULL with error set
*/
PyObject *KosCode_GetConstant(PyObject *self, uint32_t index);


/**
   Get the instruction at a given index.

   :param self: KosCode object
   :param index: instruction index

   :return: new tuple (opcode, arg), or NULL with error set
*/
PyObject *KosCode_GetInstruction(PyObject *self, uint32_t index);


/**
   Look up source position for an instruction index using binary search.

   :param self: KosCode object
   :param index: instruction index
   :param out_line: output for line number
   :param out_col: output for column number

   :return: 0 on success, -1 on error
*/
int KosCode_GetPosition(
    PyObject *self,
    uint32_t index,
    uint16_t *out_line,
    uint16_t *out_col);


/**
   C API for location lookup. Wraps KosCode_GetPosition.

   :param code: KosCode object (PyObject * or KosCode *)
   :param pc: instruction index
   :param line: output for line number
   :param col: output for column number

   :return: 0 on success, -1 on error
*/
int kos_loc_lookup(PyObject *code, uint32_t pc, uint16_t *line, uint16_t *col);


/* === Extent lookup API === */


uint32_t kos_extent_top(PyObject *code, uint32_t pc);
uint32_t kos_extent_find(PyObject *code, uint32_t pc, KosExtentKind kind);
uint32_t kos_extent_find_by_pop_pc(PyObject *code, uint32_t pop_pc,
    KosExtentKind kind);
KosExtentDesc *kos_extent_desc(PyObject *code, uint32_t extent_id);


/**
   Get ic_pc for innermost SHIFT_BODY extent at pc. Returns KOS_EXTENT_NONE
   if none.
*/
uint32_t kos_shift_ic_for(PyObject *code, uint32_t pc);


/**
   Get or create the faux CPython code object for tracebacks.

   :param self: KosCode object

   :return: borrowed reference to PyCodeObject
*/
PyCodeObject *KosCode_Faux(PyObject *self);


/**
   Get the opcode style for a given opcode.

   :param op: opcode enum value

   :return: KosOpStyle, or -1 if invalid
*/
KosOpStyle kos_opcode_style(KosOpCode op);


/**
   Get the name string for a given opcode.

   :param op: opcode enum value

   :return: string name, or "UNKNOWN_OP" if invalid
*/
const char *kos_opcode_name(KosOpCode op);


/**
   Get the extra operand schema for a given opcode.

   :param op: opcode enum value

   :return: KosExtraSchema (f0..f3 as KosOpStyle); all NOARG for
     non-EXTRA opcodes
*/
KosExtraSchema kos_opcode_extra_schema(KosOpCode op);


/**
   Get the count of per-site cache slots an opcode requests. Zero means the
   opcode is not a cache consumer. The initial framework landing returns 0
   for every opcode; individual opcodes opt in over time. Currently 0 or 1
   in practice, but a small count is supported so a single opcode may reserve
   a contiguous run of slots (see design/interpreter/attr-lookup-cache.md).

   :param op: opcode enum value

   :return: number of cache slots the opcode wants (0 for non-consumers)
*/
uint8_t kos_opcode_cache_count(KosOpCode op);


/* === Accessor macros === */


#define KosCode_FLAGS(c)                        \
  (((KosCode *) c)->flags)

#define KosCode_YIELDS(c)                       \
  (KosCode_FLAGS(c) & KOS_CODE_YIELDS)

#define KosCode_USES_FAST(c)                    \
  (KosCode_FLAGS(c) & KOS_CODE_USES_FAST)

#define KosCode_PROTECTED(c)                    \
  (KosCode_FLAGS(c) & KOS_CODE_PROTECTED)

#define KosCode_NAME(c)                         \
  (((KosCode *) c)->name)

#define KosCode_FILENAME(c)                     \
  (((KosCode *) c)->filename)

#define KosCode_NSPEC(c)                        \
  (((KosCode *) c)->nspec)

#define KosCode_ALLOCATION(c)                   \
  (((KosCode *) c)->allocation)

#define KosCode_CODE_LEN(c)                     \
  (((KosCode *) c)->code_len)

#define KosCode_CODE(c)                         \
  (((KosCode *) c)->code)

#define KosCode_GET_OP(c, i)                    \
  (KosCode_CODE(c)[i].op)

#define KosCode_GET_ARG(c, i)                   \
  (KosCode_CODE(c)[i].arg)

#define KosCode_POS_LEN(c)                      \
  (((KosCode *) c)->pos_len)

#define KosCode_POS(c)                          \
  (((KosCode *) c)->pos)

#define KosCode_CPOOL_LEN(c)                    \
  (((KosCode *) c)->cpool_len)

#define KosCode_CPOOL(c)                        \
  (((KosCode *) c)->cpool)

#define KosCode_GET_CONST(c, i)                 \
  (KosCode_CPOOL(c)[i])

#define KosCode_EXTRA_LEN(c)                     \
  (((KosCode *) c)->extra_len)

#define KosCode_EXTRA(c)                         \
  (((KosCode *) c)->extra)

#define KosCode_GetExtra(c, idx)                 \
  (KosCode_EXTRA(c)[idx])

#define KosCode_EXTENT_LEN(c)                    \
  (((KosCode *) c)->extent_len)

#define KosCode_EXTENTS(c)                      \
  (((KosCode *) c)->extents)

#define KosCode_TOP_EXTENT_ID(c)                 \
  (((KosCode *) c)->top_extent_id)

#define KosCode_FAUX(c)                         \
  (((KosCode *) c)->faux)

#define KosCode_ARENA(c)                        \
  (((KosCode *) c)->arena)

#define KosCode_EXTRAS(c)                       \
  (((KosCode *) c)->extras)

#define KosCode_CheckExact(obj)                         \
  ((obj) && ((obj)->ob_type == &KosCodeType))

#define KosCode_Check(obj)                                      \
  ((obj) && PyType_IsSubtype((obj)->ob_type, &KosCodeType))


/* === Unit lifecycle === */


int koskari_bytecode_init(PyObject *mod);
int koskari_bytecode_clear(PyObject *mod);


#endif


// The end.
