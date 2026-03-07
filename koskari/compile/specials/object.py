# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.

"""
koskari.compile.specials.object

Special form handlers for the OBJECT_* opcode family. Covers comparisons,
predicates, numeric operations, and general object interface opcodes.

Left-to-right constant folding is applied for reducible (COUNT) opcodes:
consecutive constant arguments at the head of an argument list are folded
into a single CONST. Folding stops at the first non-constant argument to
preserve dunder dispatch semantics.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


import operator as _op

from ...lib import nil, OpCode, symbol
from ...exceptions import SpecialError
from ..helpers import const_only_expr, special


def _reduce_fold(py_fold, compiler, frag, body):
    """
    Consume leading constant arguments from body, fold them with py_fold,
    and emit a single CONST into frag if any folding occurred.

    Walks body left-to-right, compiling each expression. While the result is
    a CONST-only fragment and py_fold succeeds, accumulate into a running
    total. Stops at the first non-constant expression or on a fold error.

    :param py_fold: binary Python callable used to reduce two constant values
    :param compiler: active Compiler
    :param frag: ByteCodeFragment to emit the folded CONST into
    :param body: pair list of remaining source arguments
    :return: (remaining_body, folded) where remaining_body is the unconsumed
        tail of body and folded is True if a CONST was emitted
    """

    acc = None
    have_acc = False

    current = body
    while current is not nil:
        expr, rest = current

        expr_frag = compiler.full_compile(expr)
        is_const, const_val = const_only_expr(expr_frag)

        if not is_const:
            break

        if not have_acc:
            acc = const_val
            have_acc = True
            current = rest
            continue

        try:
            acc = py_fold(acc, const_val)
        except (TypeError, ValueError, ArithmeticError):
            break

        current = rest

    if have_acc:
        frag.instruction(OpCode.KOS_OP_CONST, acc)

    return (current, have_acc)


def _reduce_op(op, py_fold, compiler, source):
    """
    Compile a variadic reducing operator (COUNT opcode style).

    When py_fold is not None, delegates to _reduce_fold to consume and fold
    any leading constant arguments into a single CONST before compiling the
    remainder normally. When py_fold is None, all arguments are compiled
    without folding.

    :param op: OpCode for the OBJECT_* instruction
    :param py_fold: binary Python callable for constant folding, or None
    :param compiler: active Compiler
    :param source: raw source pair (called_by . body)
    :return: ByteCodeFragment
    :raises SpecialError: when zero arguments are supplied
    """

    called_by, body = source

    frag = compiler.fragment(called_by)
    count = 0

    if py_fold is not None:
        frag.position_of(body)
        body, folded = _reduce_fold(py_fold, compiler, frag, body)
        if folded:
            count += 1

    current = body
    while current is not nil:
        frag.position_of(current)
        expr, current = current
        frag.merge(compiler.full_compile(expr))
        count += 1

    if count == 0:
        raise SpecialError(
            "%s requires at least one argument" % called_by,
            source)

    if count == 1 and body is nil:
        return frag

    frag.instruction(op, count)
    return frag


def _binary_op(op, compiler, source, opa=0):
    """
    Compile a strictly binary operator (always pops 2 values off the stack).

    Most binary OBJECT_* opcodes are NOARG (arg ignored by the C handler) so
    opa defaults to 0. A few implementations check the arg value at runtime
    (e.g. OBJECT_MODULO and OBJECT_DIVMOD require opa==2); callers can
    override opa for those cases.

    :param op: OpCode for the OBJECT_* instruction
    :param compiler: active Compiler
    :param source: raw source pair (called_by . body)
    :param opa: argument value encoded into the instruction (default 0)
    :return: ByteCodeFragment
    :raises SpecialError: when argument count is not exactly 2
    """

    called_by, body = source

    frag = compiler.fragment(called_by)
    count = 0

    current = body
    while current is not nil:
        frag.position_of(current)
        expr, current = current
        frag.merge(compiler.full_compile(expr))
        count += 1

    if count != 2:
        raise SpecialError(
            "%s requires exactly 2 arguments, got %d" % (called_by, count),
            source)

    frag.instruction(op, opa)
    return frag


def _unary_op(op, compiler, source):
    """
    Compile a strictly unary operator (NOARG opcode style, always pops 1).

    :param op: OpCode for the OBJECT_* instruction
    :param compiler: active Compiler
    :param source: raw source pair (called_by . body)
    :return: ByteCodeFragment
    :raises SpecialError: when argument count is not exactly 1
    """

    called_by, body = source

    frag = compiler.fragment(called_by)
    count = 0

    current = body
    while current is not nil:
        frag.position_of(current)
        expr, current = current
        frag.merge(compiler.full_compile(expr))
        count += 1

    if count != 1:
        raise SpecialError(
            "%s requires exactly 1 argument, got %d" % (called_by, count),
            source)

    frag.instruction(op, 0)
    return frag


# ---------------------------------------------------------------------------
# Comparison interface (NOARG binary)
# ---------------------------------------------------------------------------

@special(symbol("compile/="), aliases=(symbol("compile/eq?"),))
def special_eq(compiler, source, tc=False):
    """
    Compile (compile/= A B). Test equality via __eq__
    """

    return _binary_op(OpCode.KOS_OP_OBJECT_EQ, compiler, source)


@special(symbol("compile/!="), aliases=(symbol("compile/neq?"),))
def special_ne(compiler, source, tc=False):
    """
    Compile (compile/!= A B). Test inequality via __ne__
    """

    return _binary_op(OpCode.KOS_OP_OBJECT_NE, compiler, source)


@special(symbol("compile/<"), aliases=(symbol("compile/lt?"),))
def special_lt(compiler, source, tc=False):
    """
    Compile (compile/< A B). Test less-than via __lt__
    """

    return _binary_op(OpCode.KOS_OP_OBJECT_LT, compiler, source)


@special(symbol("compile/<="), aliases=(symbol("compile/le?"),))
def special_le(compiler, source, tc=False):
    """
    Compile (compile/<= A B). Test less-than-or-equal via __le__
    """

    return _binary_op(OpCode.KOS_OP_OBJECT_LE, compiler, source)


@special(symbol("compile/>"), aliases=(symbol("compile/gt?"),))
def special_gt(compiler, source, tc=False):
    """
    Compile (compile/> A B). Test greater-than via __gt__
    """

    return _binary_op(OpCode.KOS_OP_OBJECT_GT, compiler, source)


@special(symbol("compile/>="), aliases=(symbol("compile/ge?"),))
def special_ge(compiler, source, tc=False):
    """
    Compile (compile/>= A B). Test greater-than-or-equal via __ge__
    """

    return _binary_op(OpCode.KOS_OP_OBJECT_GE, compiler, source)


# ---------------------------------------------------------------------------
# Predicate interface (NOARG binary / COUNT variadic)
# ---------------------------------------------------------------------------

@special(symbol("compile/in"))
def special_in(compiler, source, tc=False):
    """
    Compile (compile/in ITEM COLLECTION). Membership test via __contains__
    """

    return _binary_op(OpCode.KOS_OP_OBJECT_IN, compiler, source)


@special(symbol("compile/not-in"))
def special_not_in(compiler, source, tc=False):
    """
    Compile (compile/not-in ITEM COLLECTION). Negated membership test
    """

    return _binary_op(OpCode.KOS_OP_OBJECT_NOT_IN, compiler, source)


@special(symbol("compile/is"))
def special_is(compiler, source, tc=False):
    """
    Compile (compile/is A B). Identity test
    """

    return _binary_op(OpCode.KOS_OP_OBJECT_IS, compiler, source)


@special(symbol("compile/is-not"))
def special_is_not(compiler, source, tc=False):
    """
    Compile (compile/is-not A B). Negated identity test
    """

    return _binary_op(OpCode.KOS_OP_OBJECT_IS_NOT, compiler, source)


def _type_check_op(op, compiler, source):
    """
    Compile a type-check operator (OBJECT_IS_INSTANCE or OBJECT_IS_SUBCLASS).

    The C handler's arg convention differs from the usual COUNT pattern:
    arg < 2 means pop value and match as two separate stack items (for the
    single-type case). arg >= 2 means collect arg items from the stack tail
    as a tuple and use that as the match (for multi-base checks). Requires
    at least 2 arguments (value plus at least one type).

    Calling forms:
      (compile/isinstance VAL TYPE)         -> arg = 1  (single type)
      (compile/isinstance VAL T1 T2 ...)    -> arg = N  (N types as tuple)

    :param op: OpCode.KOS_OP_OBJECT_IS_INSTANCE or KOS_OP_OBJECT_IS_SUBCLASS
    :param compiler: active Compiler
    :param source: raw source pair (called_by . body)
    :return: ByteCodeFragment
    :raises SpecialError: when fewer than 2 arguments are supplied
    """

    called_by, body = source

    frag = compiler.fragment(called_by)
    exprs = list(body.unpack()) if body is not nil else []

    if len(exprs) < 2:
        raise SpecialError(
            "%s requires at least 2 arguments (value and type), got %d"
            % (called_by, len(exprs)),
            source)

    frag.position_of(source)

    value_expr = exprs[0]
    type_exprs = exprs[1:]

    frag.merge(compiler.full_compile(value_expr))
    for te in type_exprs:
        frag.merge(compiler.full_compile(te))

    frag.instruction(op, len(type_exprs))
    return frag


@special(symbol("compile/isinstance"))
def special_isinstance(compiler, source, tc=False):
    """
    Compile (compile/isinstance OBJ TYPE ...). Instance type check
    """

    return _type_check_op(
        OpCode.KOS_OP_OBJECT_IS_INSTANCE, compiler, source)


@special(symbol("compile/issubclass"))
def special_issubclass(compiler, source, tc=False):
    """
    Compile (compile/issubclass CLS TYPE ...). Subclass type check
    """

    return _type_check_op(
        OpCode.KOS_OP_OBJECT_IS_SUBCLASS, compiler, source)


# ---------------------------------------------------------------------------
# Numeric interface — reducible COUNT opcodes with constant folding
# ---------------------------------------------------------------------------

@special(symbol("compile/+"))
def special_add(compiler, source, tc=False):
    """
    Compile (compile/+ A B ...). Variadic addition via __add__
    """

    return _reduce_op(
        OpCode.KOS_OP_OBJECT_ADD, _op.add, compiler, source)


@special(symbol("compile/-"))
def special_subtract(compiler, source, tc=False):
    """
    Compile (compile/- A B ...). Variadic subtraction via __sub__
    """

    return _reduce_op(
        OpCode.KOS_OP_OBJECT_SUBTRACT, _op.sub, compiler, source)


@special(symbol("compile/*"))
def special_multiply(compiler, source, tc=False):
    """
    Compile (compile/* A B ...). Variadic multiplication via __mul__
    """

    return _reduce_op(
        OpCode.KOS_OP_OBJECT_MULTIPLY, _op.mul, compiler, source)


@special(symbol("compile/**"))
def special_power(compiler, source, tc=False):
    """
    Compile (compile/** BASE EXP [MOD]). Exponentiation via __pow__
    """

    return _reduce_op(
        OpCode.KOS_OP_OBJECT_POWER, None, compiler, source)


@special(symbol("compile/&"))
def special_bit_and(compiler, source, tc=False):
    """
    Compile (compile/& A B ...). Variadic bitwise AND via __and__
    """

    return _reduce_op(
        OpCode.KOS_OP_OBJECT_BIT_AND, _op.and_, compiler, source)


@special(symbol("compile/|"))
def special_bit_or(compiler, source, tc=False):
    """
    Compile (compile/| A B ...). Variadic bitwise OR via __or__
    """

    return _reduce_op(
        OpCode.KOS_OP_OBJECT_BIT_OR, _op.or_, compiler, source)


@special(symbol("compile/^"))
def special_bit_xor(compiler, source, tc=False):
    """
    Compile (compile/^ A B ...). Variadic bitwise XOR via __xor__
    """

    return _reduce_op(
        OpCode.KOS_OP_OBJECT_BIT_XOR, _op.xor, compiler, source)


@special(symbol("compile///"))
def special_floor_divide(compiler, source, tc=False):
    """
    Compile (compile/// A B ...). Variadic floor division via __floordiv__
    """

    return _reduce_op(
        OpCode.KOS_OP_OBJECT_FLOOR_DIVIDE, _op.floordiv, compiler, source)


@special(symbol("compile/@"))
def special_matrix_multiply(compiler, source, tc=False):
    """
    Compile (compile/@ A B ...). Variadic matrix multiplication via __matmul__
    """

    return _reduce_op(
        OpCode.KOS_OP_OBJECT_MATRIX_MULTIPLY, _op.matmul, compiler, source)


# ---------------------------------------------------------------------------
# Numeric interface — NOARG binary opcodes (no constant folding)
# ---------------------------------------------------------------------------

@special(symbol("compile//"))
def special_true_divide(compiler, source, tc=False):
    """
    Compile (compile// A B ...). True division via __truediv__
    """

    return _reduce_op(
        OpCode.KOS_OP_OBJECT_TRUE_DIVIDE, _op.truediv, compiler, source)


@special(symbol("compile/%"))
def special_modulo(compiler, source, tc=False):
    """
    Compile (compile/% A B). Modulo via __mod__
    """

    return _binary_op(OpCode.KOS_OP_OBJECT_MODULO, compiler, source, opa=2)


@special(symbol("compile/<<"))
def special_bit_left(compiler, source, tc=False):
    """
    Compile (compile/<< A B). Left bit-shift via __lshift__
    """

    return _binary_op(OpCode.KOS_OP_OBJECT_BIT_LEFT, compiler, source)


@special(symbol("compile/>>"))
def special_bit_right(compiler, source, tc=False):
    """
    Compile (compile/>> A B). Right bit-shift via __rshift__
    """

    return _binary_op(OpCode.KOS_OP_OBJECT_BIT_RIGHT, compiler, source)


@special(symbol("compile/divmod"))
def special_divmod(compiler, source, tc=False):
    """
    Compile (compile/divmod A B). Combined quotient and remainder
    """

    return _binary_op(OpCode.KOS_OP_OBJECT_DIVMOD, compiler, source, opa=2)


# ---------------------------------------------------------------------------
# General object interface — NOARG unary opcodes
# ---------------------------------------------------------------------------

@special(symbol("compile/not"))
def special_not(compiler, source, tc=False):
    """
    Compile (compile/not OBJ). Boolean negation via __bool__ then invert
    """

    return _unary_op(OpCode.KOS_OP_OBJECT_NOT, compiler, source)


@special(symbol("compile/repr"))
def special_repr(compiler, source, tc=False):
    """
    Compile (compile/repr OBJ). String representation via __repr__
    """

    return _unary_op(OpCode.KOS_OP_OBJECT_REPR, compiler, source)


@special(symbol("compile/truth"))
def special_truth(compiler, source, tc=False):
    """
    Compile (compile/truth OBJ). Boolean coercion via __bool__
    """

    return _unary_op(OpCode.KOS_OP_OBJECT_TRUTH, compiler, source)


@special(symbol("compile/iter"))
def special_iter(compiler, source, tc=False):
    """
    Compile (compile/iter OBJ). Iterator creation via __iter__
    """

    return _unary_op(OpCode.KOS_OP_OBJECT_ITER, compiler, source)


@special(symbol("compile/~"))
def special_bit_invert(compiler, source, tc=False):
    """
    Compile (compile/~ OBJ). Bitwise inversion via __invert__
    """

    return _unary_op(OpCode.KOS_OP_OBJECT_BIT_INVERT, compiler, source)


@special(symbol("compile/str"))
def special_str(compiler, source, tc=False):
    """
    Compile (compile/str OBJ ...). String conversion via __str__, variadic
    for f-string style multi-part concatenation
    """

    return _reduce_op(
        OpCode.KOS_OP_OBJECT_STR, None, compiler, source)


@special(symbol("compile/len"))
def special_len(compiler, source, tc=False):
    """
    Compile (compile/len OBJ). Length query via __len__
    """

    return _unary_op(OpCode.KOS_OP_OBJECT_LEN, compiler, source)


@special(symbol("compile/type"))
def special_type(compiler, source, tc=False):
    """
    Compile (compile/type OBJ). Type introspection via type()
    """

    return _unary_op(OpCode.KOS_OP_OBJECT_TYPE, compiler, source)


@special(symbol("compile/dir"))
def special_dir(compiler, source, tc=False):
    """
    Compile (compile/dir OBJ). Attribute listing via __dir__
    """

    return _unary_op(OpCode.KOS_OP_OBJECT_DIR, compiler, source)


@special(symbol("compile/vars"))
def special_vars(compiler, source, tc=False):
    """
    Compile (compile/vars OBJ). Attribute dict via __dict__
    """

    return _unary_op(OpCode.KOS_OP_OBJECT_VARS, compiler, source)


@special(symbol("compile/aiter"))
def special_aiter(compiler, source, tc=False):
    """
    Compile (compile/aiter OBJ). Async iterator creation via __aiter__
    """

    return _unary_op(OpCode.KOS_OP_OBJECT_AITER, compiler, source)


# The end.
