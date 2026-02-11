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
   delta-encoded positions table. After initialization, the code
   and constant pool memory is marked read-only via mprotect.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
   :ai-assistant: Claude Opus 4.5 via Cursor
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

} KosOpStyle;


/* === Opcode enum === */


extern PyObject *KosOpCodeEnum;


typedef enum KosOpCode {

#define OP(name, style)   \
  KOS_OP_##name ,

#include "opcodes.incl"
#undef OP

} KosOpCode;


/* === Code flags === */


typedef enum KosCodeFlag {
  /** code contains yield/yield_from, returns async generator */
  KOS_CODE_YIELDS = 0x01,
} KosCodeFlag;


/* === KosOp: single instruction === */


/**
   A single bytecode instruction: 4 bytes total.
   Opcode and argument are both 16-bit unsigned integers.
*/
typedef struct KosOp {
  uint16_t op;    /* KosOpCode */
  uint16_t arg;   /* argument value */
} KosOp;


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
  KOS_EXT_SHIFT_BODY,
  KOS_EXT_CALLCC_BIND,
  KOS_EXT_FINALLY,
  KOS_EXT_WIND,
  KOS_EXT_WITH,
  KOS_EXT_FOREACH,
  KOS_EXT_BEGIN,
  KOS_EXT_NS,
} KosExtentKind;


/**
   Static extent descriptor. Describes a block extent with push_pc,
   pop_pc, and kind-specific handler/control targets.
*/
typedef struct KosExtentDesc {
  KosExtentKind kind;
  uint32_t push_pc;
  uint32_t pop_pc;
  uint32_t parent_extent_id;

  uint32_t flags;

  /* Kind-specific targets; use KOS_EXTENT_NONE for N/A */
  uint32_t except_pc;
  uint32_t else_pc;
  uint32_t finally_pc;
  uint32_t exit_pc;
  uint32_t ic_pc;
  uint32_t break_pc;
  uint32_t continue_pc;
} KosExtentDesc;


/* === KosCode: bytecode object === */


extern PyTypeObject KosCodeType;


typedef struct KosCode {
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

  uint32_t extent_len;
  KosExtentDesc *extents;
  uint16_t *top_extent_id;
  size_t extent_allocation;

  PyCodeObject *faux;   /* faux code object for tracebacks */
  PyObject *extras;     /* extra metadata (PyDict) */
} KosCode;


/* === Constructor === */


/**
   Create a new KosCode object.

   :param nspec: namespace spec (KosNamespaceSpec)
   :param instr: sequence of (opcode, argument) pairs
   :param name: function/expression name (PyUnicode or NULL)
   :param filename: source filename (PyUnicode or NULL)

   :return: new KosCode reference, or NULL on error
*/
PyObject *KosCode_New(
    PyObject *nspec,
    PyObject *instr,
    PyObject *name,
    PyObject *filename);


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
KosExtentDesc *kos_extent_desc(PyObject *code, uint32_t extent_id);


/**
   TRY handler targets for a pc inside a TRY or FINALLY extent.
*/
typedef struct KosTryHandler {
  uint32_t extent_id;
  uint32_t except_pc;
  uint32_t finally_pc;
  uint32_t exit_pc;
} KosTryHandler;


/**
   Get innermost TRY/FINALLY handler for pc. Returns 0 if found and filled,
   -1 if no code or no extent table.
*/
int kos_try_handler_for(PyObject *code, uint32_t pc, KosTryHandler *out);


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


/* === Accessor macros === */


#define KosCode_FLAGS(c)                        \
  (((KosCode *) c)->flags)

#define KosCode_YIELDS(c)                       \
  (KosCode_FLAGS(c) & KOS_CODE_YIELDS)

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

#define KosCode_EXTENT_LEN(c)                    \
  (((KosCode *) c)->extent_len)

#define KosCode_EXTENTS(c)                      \
  (((KosCode *) c)->extents)

#define KosCode_TOP_EXTENT_ID(c)                 \
  (((KosCode *) c)->top_extent_id)

#define KosCode_FAUX(c)                         \
  (((KosCode *) c)->faux)

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
