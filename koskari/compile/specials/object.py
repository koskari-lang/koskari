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
"""


import operator as _op

from ...bootstrap import builtin
from ...exceptions import SpecialError
from ...lib import Compare, OpCode, Operand, cons, symbol
from ...matcher import BODY, CALLEDBY, MATCH, REST, SYMBOL, VALUE
from ...syntax import special
from ...types import is_keyword, is_nil, is_pair, is_symbol, unpack
from ..helpers import (
    const_only_expr, emit_item_place, feeder_expr, splat_call_form)
from ..processor import item_inplace_arg, item_opcode


# Two or more operands, with the short arm carrying the arity message a
# corpus record asserts
_COMPARE_PATTERNS = [
    MATCH(CALLEDBY("called_by"), REST("args", min_tail=2)),
    MATCH(CALLEDBY("called_by"), REST("short")),
]

# A value and one or more types
_TYPE_CHECK_PATTERNS = [
    MATCH(CALLEDBY("called_by"), REST("args", min_tail=2)),
]

_BINARY_PATTERNS = [
    MATCH(CALLEDBY("called_by"), VALUE("a"), VALUE("b")),
]

_UNARY_PATTERNS = [
    MATCH(CALLEDBY("called_by"), VALUE("obj")),
]

# One or more operands folded left to right
_REDUCE_PATTERNS = [
    MATCH(CALLEDBY("called_by"), BODY("args")),
]

# The concatenating form takes no operands as the empty string
_STR_PATTERNS = [
    MATCH(CALLEDBY("called_by"), REST("args")),
]


def _reduce_fold(py_fold, compiler, body):
    """
    Consume leading constant arguments from body, folding them with
    py_fold into a single accumulated value.

    Walks body left-to-right, compiling each expression. While the result is
    a CONST-only fragment and py_fold succeeds, accumulate into a running
    total. Stops at the first non-constant expression or on a fold error,
    preserving that expression's compiled fragment so the caller can merge
    it instead of compiling it a second time.

    :param py_fold: binary Python callable used to reduce two constant values
    :param compiler: active Compiler
    :param body: pair list of remaining source arguments
    :return: (remaining_body, folded, acc, pending) where remaining_body is
        the unconsumed tail of body, folded is True if constants were
        accumulated into acc, and pending is the already-compiled fragment
        for the head of remaining_body, or None
    """

    acc = None
    have_acc = False
    pending = None

    current = body
    while not is_nil(current):
        expr, rest = current

        expr_frag = compiler.full_compile(expr)
        is_const, const_val = const_only_expr(expr_frag)

        if not is_const:
            pending = expr_frag
            break

        if not have_acc:
            acc = const_val
            have_acc = True
            current = rest
            continue

        try:
            acc = py_fold(acc, const_val)
        except (TypeError, ValueError, ArithmeticError):
            pending = expr_frag
            break

        current = rest

    return (current, have_acc, acc, pending)


def emit_reduce_op(
        compiler,
        called_by,
        args,
        op,
        py_fold,
        operand=None,
        py_unary=None):
    """
    Compile a variadic reducing operator (COUNT opcode style).

    When py_fold is not None, delegates to _reduce_fold to consume and fold
    any leading constant arguments into a single CONST before compiling the
    remainder normally. When py_fold is None, all arguments are compiled
    without folding.

    When py_unary is given the operator admits a single argument. A single
    constant argument folds through py_unary at compile time; a single
    non-constant argument emits the reduction with a count of one and the
    runtime applies the operator's unary form. When py_unary is None a
    single argument is a compile-time error.

    When operand is given, the reduction is emitted as the parameterized
    OBJECT_OPERAND instruction carrying (operand, count); otherwise the
    supplied op opcode is emitted with a plain count arg (e.g. OBJECT_STR).

    :param compiler: active Compiler
    :param called_by: the head capture
    :param args: the operand list capture
    :param op: OpCode for the OBJECT_* instruction, or None when operand is set
    :param py_fold: binary Python callable for constant folding, or None
    :param operand: Operand op-type for OBJECT_OPERAND, or None
    :param py_unary: unary Python callable applied to a lone constant, or
        None when the operator has no single-argument form
    :return: ByteCodeFragment
    :raises SpecialError: when too few arguments are supplied, or when
        py_unary rejects the lone constant
    """

    head = called_by.datum
    pos = called_by.position
    body = args.datum

    frag = compiler.fragment(head)

    expr, rest = body
    if is_nil(rest) and not is_keyword(expr):
        if py_unary is None:
            raise SpecialError(
                f"{head} requires at least two arguments",
                head, position=pos)

        frag.position_of(body)
        expr_frag = compiler.full_compile(expr)
        is_const, const_val = const_only_expr(expr_frag)

        if is_const:
            try:
                value = py_unary(const_val)
            except (TypeError, ValueError, ArithmeticError) as exc:
                raise SpecialError(
                    f"{head} cannot be applied to {const_val!r}: {exc}",
                    head, position=pos)
            frag.instruction(OpCode.CONST, value)
            return frag

        frag.merge(expr_frag)
        _emit_reduce(frag, op, operand, 1)
        return frag

    count = 0

    have_acc = False
    pending = None
    if py_fold is not None:
        frag.position_of(body)
        body, have_acc, acc, pending = _reduce_fold(py_fold, compiler, body)

    for fm in unpack(body):
        if is_keyword(fm):
            if have_acc:
                return cons(symbol("call"), head, acc, body)
            else:
                return cons(symbol("call"), head, body)

    if have_acc:
        frag.instruction(OpCode.CONST, acc)
        count += 1

    current = body
    if pending is not None:
        frag.position_of(current)
        _, current = current
        frag.merge(pending)
        count += 1

    while not is_nil(current):
        frag.position_of(current)
        expr, current = current
        frag.merge(compiler.full_compile(expr))
        count += 1

    if count == 1 and is_nil(body):
        # every argument folded into one constant
        return frag

    _emit_reduce(frag, op, operand, count)
    return frag


def _emit_reduce(frag, op, operand, count):
    """
    Append the reducing instruction for count operands: OBJECT_OPERAND
    carrying (operand, count) when operand is set, else op with count
    """

    if operand is not None:
        frag.instruction(OpCode.OBJECT_OPERAND, (operand, count))
    else:
        frag.instruction(op, count)


def emit_binary_op(compiler, called_by, a, b, op, opa=0, operand=None):
    """
    Compile a strictly binary operator (always pops 2 values off the stack).

    Most binary OBJECT_* opcodes are NOARG (arg ignored by the C handler) so
    opa defaults to 0. A few implementations check the arg value at runtime
    (e.g. OBJECT_DIVMOD requires opa==2); callers can override opa for those
    cases.

    When operand is given, the (strictly binary) operation is emitted as the
    parameterized OBJECT_OPERAND instruction carrying (operand, 2); otherwise
    the supplied op opcode is emitted with opa.

    :param compiler: active Compiler
    :param called_by: the head capture
    :param a: the left operand capture
    :param b: the right operand capture
    :param op: OpCode for the OBJECT_* instruction, or None when operand is set
    :param opa: argument value encoded into the instruction (default 0)
    :param operand: Operand op-type for OBJECT_OPERAND, or None
    :return: ByteCodeFragment
    """

    frag = compiler.fragment(called_by.datum)

    for cap in (a, b):
        frag.position(cap.position)
        frag.merge(compiler.full_compile(cap.datum))

    if operand is not None:
        frag.instruction(OpCode.OBJECT_OPERAND, (operand, 2))
    else:
        frag.instruction(op, opa)
    return frag


def emit_unary_op(compiler, called_by, obj, op):
    """
    Compile a strictly unary operator (NOARG opcode style, always pops 1)

    :param compiler: active Compiler
    :param called_by: the head capture
    :param obj: the operand capture
    :param op: OpCode for the OBJECT_* instruction
    :return: ByteCodeFragment
    """

    frag = compiler.fragment(called_by.datum)
    frag.position(obj.position)
    frag.merge(compiler.full_compile(obj.datum))
    frag.instruction(op, 0)
    return frag


_FORMAT_PATTERNS = [
    MATCH(CALLEDBY("called_by"), VALUE("obj")),
    MATCH(CALLEDBY("called_by"), VALUE("obj"), VALUE("rules")),
]


@builtin("compile/format")
@special("compile/format", patterns=_FORMAT_PATTERNS, unpack="match")
def special_format(compiler, caps):
    """
    Compile (compile/format OBJ [RULES]). Rules may be dynamic, constant str,
    or quoted symbol constant. The all-constant case folds to CONST here,
    where a bad rules value diagnoses at compile time; every other shape
    emits naively, and the processor's format lowering ladder owns the
    demotion to OBJECT_FORMAT_CONST and the fused forms.
    """

    called_by = caps["called_by"]

    obj_expr = caps["obj"].datum
    obj_frag = compiler.full_compile(obj_expr)
    obj_is_const, obj_const = const_only_expr(obj_frag)

    if "rules" in caps:
        rules_expr = caps["rules"].datum
        rules_frag = compiler.full_compile(rules_expr)
        rules_is_const, rules_const = const_only_expr(rules_frag)
        if rules_is_const and is_symbol(rules_const):
            # a quoted symbol as rules means its string
            rules_const = str(rules_const)
    else:
        rules_expr = None

    frag = compiler.fragment(called_by.datum)
    frag.position(called_by.position)

    if rules_expr:
        if obj_is_const and rules_is_const:
            frag.instruction(OpCode.CONST, format(obj_const, rules_const))
        elif rules_is_const:
            frag.merge(obj_frag)
            frag.instruction(OpCode.CONST, rules_const)
            frag.instruction(OpCode.OBJECT_FORMAT, 2)
        else:
            frag.merge(obj_frag)
            frag.merge(rules_frag)
            frag.instruction(OpCode.OBJECT_FORMAT, 2)

    else:
        if obj_is_const:
            frag.instruction(OpCode.CONST, format(obj_const))
        else:
            frag.merge(obj_frag)
            frag.instruction(OpCode.OBJECT_FORMAT, 1)

    return frag


# ---------------------------------------------------------------------------
# Comparison interface (COUNT variadic)
# ---------------------------------------------------------------------------


def emit_chain_compare(compiler, caps, sel):
    """
    Compile a chained comparison operator as OBJECT_COMPARE carrying a
    (Compare selector, count) CMPCOUNT argument

    :param compiler: active Compiler
    :param caps: the match captures
    :param sel: Compare selector for the comparison
    :return: ByteCodeFragment
    :raises SpecialError: when fewer than 2 operands are supplied
    """

    called_by = caps["called_by"]
    head = called_by.datum

    if "args" not in caps:
        raise SpecialError(
            "%s requires at least two arguments" % head,
            head, position=called_by.position)

    body = caps["args"].datum

    call_form = splat_call_form(head, body)
    if call_form is not None:
        return call_form

    frag = compiler.fragment(head)
    frag.position(called_by.position)

    count = 0
    current = body
    while not is_nil(current):
        frag.position_of(current)
        expr, current = current
        frag.merge(compiler.full_compile(expr))
        count += 1

    frag.instruction(OpCode.OBJECT_COMPARE, (sel, count))
    return frag


@builtin("compile/==", "compile/equal?")
@special("compile/==", patterns=_COMPARE_PATTERNS, unpack="match")
def special_eq(compiler, caps):
    """
    Compile (compile/= A B). Test equality via __eq__
    """

    return emit_chain_compare(compiler, caps, Compare.EQ)


@builtin("compile/!=", "compile/not-equal?")
@special("compile/!=", patterns=_COMPARE_PATTERNS, unpack="match")
def special_ne(compiler, caps):
    """
    Compile (compile/!= A B). Test inequality via __ne__
    """

    return emit_chain_compare(compiler, caps, Compare.NE)


@builtin("compile/<", "compile/lt?")
@special("compile/<", patterns=_COMPARE_PATTERNS, unpack="match")
def special_lt(compiler, caps):
    """
    Compile (compile/< A B). Test less-than via __lt__
    """

    return emit_chain_compare(compiler, caps, Compare.LT)


@builtin("compile/<=", "compile/le?")
@special("compile/<=", patterns=_COMPARE_PATTERNS, unpack="match")
def special_le(compiler, caps):
    """
    Compile (compile/<= A B). Test less-than-or-equal via __le__
    """

    return emit_chain_compare(compiler, caps, Compare.LE)


@builtin("compile/>", "compile/gt?")
@special("compile/>", patterns=_COMPARE_PATTERNS, unpack="match")
def special_gt(compiler, caps):
    """
    Compile (compile/> A B). Test greater-than via __gt__
    """

    return emit_chain_compare(compiler, caps, Compare.GT)


@builtin("compile/>=", "compile/ge?")
@special("compile/>=", patterns=_COMPARE_PATTERNS, unpack="match")
def special_ge(compiler, caps):
    """
    Compile (compile/>= A B). Test greater-than-or-equal via __ge__
    """

    return emit_chain_compare(compiler, caps, Compare.GE)


# ---------------------------------------------------------------------------
# Predicate interface (NOARG binary / COUNT variadic)
# ---------------------------------------------------------------------------

@builtin("compile/in", "compile/in?")
@special("compile/in", patterns=_BINARY_PATTERNS, unpack="kwargs")
def special_in(compiler, called_by, a, b):
    """
    Compile (compile/in ITEM COLLECTION). Membership test via __contains__
    """

    return emit_binary_op(
        compiler, called_by, a, b, OpCode.OBJECT_COMPARE,
        opa=(Compare.IN, 2))


@builtin("compile/not-in", "compile/not-in?")
@special("compile/not-in", patterns=_BINARY_PATTERNS, unpack="kwargs")
def special_not_in(compiler, called_by, a, b):
    """
    Compile (compile/not-in ITEM COLLECTION). Negated membership test
    """

    return emit_binary_op(
        compiler, called_by, a, b, OpCode.OBJECT_COMPARE,
        opa=(Compare.NOT_IN, 2))


@builtin("compile/is", "compile/is?")
@special("compile/is", patterns=_BINARY_PATTERNS, unpack="kwargs")
def special_is(compiler, called_by, a, b):
    """
    Compile (compile/is A B). Identity test
    """

    return emit_binary_op(
        compiler, called_by, a, b, OpCode.OBJECT_COMPARE,
        opa=(Compare.IS, 2))


@builtin("compile/is-not", "compile/is-not?")
@special("compile/is-not", patterns=_BINARY_PATTERNS, unpack="kwargs")
def special_is_not(compiler, called_by, a, b):
    """
    Compile (compile/is-not A B). Negated identity test
    """

    return emit_binary_op(
        compiler, called_by, a, b, OpCode.OBJECT_COMPARE,
        opa=(Compare.IS_NOT, 2))


def emit_type_check(compiler, called_by, args, sel):
    """
    Compile a type-check operator as OBJECT_COMPARE carrying the
    IS_INSTANCE or IS_SUBCLASS selector. The CMPCOUNT count is the total
    operand count (value plus N types); the handler treats operands past
    the value as the match, tupled when N > 1. Requires at least 2
    arguments (value plus at least one type).

    Calling forms:
      (compile/instance? VAL TYPE)         -> count = 2  (single type)
      (compile/instance? VAL T1 T2 ...)    -> count = N+1  (types as tuple)

    :param compiler: active Compiler
    :param called_by: the head capture
    :param args: the value-and-types capture
    :param sel: Compare.IS_INSTANCE or Compare.IS_SUBCLASS
    :return: ByteCodeFragment
    """

    frag = compiler.fragment(called_by.datum)
    exprs = list(args.datum.unpack())

    frag.position(called_by.position)

    for expr in exprs:
        frag.merge(compiler.full_compile(expr))

    frag.instruction(OpCode.OBJECT_COMPARE, (sel, len(exprs)))
    return frag


@builtin("compile/instance?")
@special("compile/instance?", patterns=_TYPE_CHECK_PATTERNS,
         unpack="kwargs")
def special_isinstance(compiler, called_by, args):
    """
    Compile (compile/instance? OBJ TYPE ...). Instance type check
    """

    return emit_type_check(compiler, called_by, args, Compare.IS_INSTANCE)


@builtin("compile/subclass?")
@special("compile/subclass?", patterns=_TYPE_CHECK_PATTERNS, unpack="kwargs")
def special_issubclass(compiler, called_by, args):
    """
    Compile (compile/subclass? CLS TYPE ...). Subclass type check
    """

    return emit_type_check(compiler, called_by, args, Compare.IS_SUBCLASS)


# ---------------------------------------------------------------------------
# Numeric interface — reducible COUNT opcodes with constant folding
# ---------------------------------------------------------------------------

@builtin("compile/+")
@special("compile/+", patterns=_REDUCE_PATTERNS, unpack="kwargs")
def special_add(compiler, called_by, args):
    """
    Compile (compile/+ A B ...). Variadic addition via __add__
    """

    return emit_reduce_op(
        compiler, called_by, args, None, _op.add,
        operand=Operand.ADD, py_unary=_op.pos)


@builtin("compile/-")
@special("compile/-", patterns=_REDUCE_PATTERNS, unpack="kwargs")
def special_subtract(compiler, called_by, args):
    """
    Compile (compile/- A B ...). Variadic subtraction via __sub__
    """

    return emit_reduce_op(
        compiler, called_by, args, None, _op.sub,
        operand=Operand.SUBTRACT, py_unary=_op.neg)


@builtin("compile/*")
@special("compile/*", patterns=_REDUCE_PATTERNS, unpack="kwargs")
def special_multiply(compiler, called_by, args):
    """
    Compile (compile/* A B ...). Variadic multiplication via __mul__
    """

    return emit_reduce_op(
        compiler, called_by, args, None, _op.mul,
        operand=Operand.MULTIPLY)


@builtin("compile/**")
@special("compile/**", patterns=_REDUCE_PATTERNS, unpack="kwargs")
def special_power(compiler, called_by, args):
    """
    Compile (compile/** BASE EXP [MOD]). Exponentiation via __pow__
    """

    return emit_reduce_op(
        compiler, called_by, args, None, None,
        operand=Operand.POWER)


@builtin("compile/&")
@special("compile/&", patterns=_REDUCE_PATTERNS, unpack="kwargs")
def special_bit_and(compiler, called_by, args):
    """
    Compile (compile/& A B ...). Variadic bitwise AND via __and__
    """

    return emit_reduce_op(
        compiler, called_by, args, None, _op.and_,
        operand=Operand.BIT_AND)


@builtin("compile/|")
@special("compile/|", patterns=_REDUCE_PATTERNS, unpack="kwargs")
def special_bit_or(compiler, called_by, args):
    """
    Compile (compile/| A B ...). Variadic bitwise OR via __or__
    """

    return emit_reduce_op(
        compiler, called_by, args, None, _op.or_,
        operand=Operand.BIT_OR)


@builtin("compile/^")
@special("compile/^", patterns=_REDUCE_PATTERNS, unpack="kwargs")
def special_bit_xor(compiler, called_by, args):
    """
    Compile (compile/^ A B ...). Variadic bitwise XOR via __xor__
    """

    return emit_reduce_op(
        compiler, called_by, args, None, _op.xor,
        operand=Operand.BIT_XOR)


@builtin("compile///")
@special("compile///", patterns=_REDUCE_PATTERNS, unpack="kwargs")
def special_floor_divide(compiler, called_by, args):
    """
    Compile (compile/// A B ...). Variadic floor division via __floordiv__
    """

    return emit_reduce_op(
        compiler, called_by, args, None, _op.floordiv,
        operand=Operand.FLOOR_DIVIDE)


@builtin("compile/@")
@special("compile/@", patterns=_REDUCE_PATTERNS, unpack="kwargs")
def special_matrix_multiply(compiler, called_by, args):
    """
    Compile (compile/@ A B ...). Variadic matrix multiplication via __matmul__
    """

    return emit_reduce_op(
        compiler, called_by, args, None, _op.matmul,
        operand=Operand.MATRIX_MULTIPLY)


# ---------------------------------------------------------------------------
# Numeric interface — NOARG binary opcodes (no constant folding)
# ---------------------------------------------------------------------------

@builtin("compile//")
@special("compile//", patterns=_REDUCE_PATTERNS, unpack="kwargs")
def special_true_divide(compiler, called_by, args):
    """
    Compile (compile// A B ...). True division via __truediv__
    """

    return emit_reduce_op(
        compiler, called_by, args, None, _op.truediv,
        operand=Operand.TRUE_DIVIDE)


@builtin("compile/%")
@special("compile/%", patterns=_BINARY_PATTERNS, unpack="kwargs")
def special_modulo(compiler, called_by, a, b):
    """
    Compile (compile/% A B). Modulo via __mod__
    """

    return emit_binary_op(
        compiler, called_by, a, b, None, operand=Operand.MODULO)


@builtin("compile/<<")
@special("compile/<<", patterns=_BINARY_PATTERNS, unpack="kwargs")
def special_bit_left(compiler, called_by, a, b):
    """
    Compile (compile/<< A B). Left bit-shift via __lshift__
    """

    return emit_binary_op(
        compiler, called_by, a, b, None, operand=Operand.BIT_LEFT)


@builtin("compile/>>")
@special("compile/>>", patterns=_BINARY_PATTERNS, unpack="kwargs")
def special_bit_right(compiler, called_by, a, b):
    """
    Compile (compile/>> A B). Right bit-shift via __rshift__
    """

    return emit_binary_op(
        compiler, called_by, a, b, None, operand=Operand.BIT_RIGHT)


@builtin("compile/divmod")
@special("compile/divmod", patterns=_BINARY_PATTERNS, unpack="kwargs")
def special_divmod(compiler, called_by, a, b):
    """
    Compile (compile/divmod A B). Combined quotient and remainder
    """

    return emit_binary_op(
        compiler, called_by, a, b, OpCode.OBJECT_DIVMOD, opa=2)


# ---------------------------------------------------------------------------
# General object interface — NOARG unary opcodes
# ---------------------------------------------------------------------------

@builtin("compile/not")
@special("compile/not", patterns=_UNARY_PATTERNS, unpack="kwargs")
def special_not(compiler, called_by, obj):
    """
    Compile (compile/not OBJ). Boolean negation via __bool__ then invert
    """

    return emit_unary_op(compiler, called_by, obj, OpCode.OBJECT_NOT)


@builtin("compile/nil?")
@special("compile/nil?", patterns=_UNARY_PATTERNS, unpack="kwargs")
def special_nilp(compiler, called_by, obj):
    """
    Compile (nil? OBJ). True if OBJ is an empty-list singleton, either
    nil or bracket-nil. Emits NILP, which compression may lower to a
    CHECK_NIL when followed by a conditional jump.
    """

    return emit_unary_op(compiler, called_by, obj, OpCode.NILP)


@builtin("compile/repr")
@special("compile/repr", patterns=_UNARY_PATTERNS, unpack="kwargs")
def special_repr(compiler, called_by, obj):
    """
    Compile (compile/repr OBJ). String representation via __repr__
    """

    return emit_unary_op(compiler, called_by, obj, OpCode.OBJECT_REPR)


@builtin("compile/truth")
@special("compile/truth", patterns=_UNARY_PATTERNS, unpack="kwargs")
def special_truth(compiler, called_by, obj):
    """
    Compile (compile/truth OBJ). Boolean coercion via __bool__
    """

    return emit_unary_op(compiler, called_by, obj, OpCode.OBJECT_TRUTH)


@builtin("compile/iter")
@special("compile/iter", patterns=_UNARY_PATTERNS, unpack="kwargs")
def special_iter(compiler, called_by, obj):
    """
    Compile (compile/iter OBJ). Iterator acquisition uniform over both
    protocols: a koskari AsyncGenerator passes through as itself, an
    async iterable yields its __aiter__, otherwise the sync __iter__
    protocol
    """

    return emit_unary_op(compiler, called_by, obj, OpCode.OBJECT_ITER)


_NEXT_PATTERNS = [
    MATCH(CALLEDBY("called_by"), VALUE("it")),
    MATCH(CALLEDBY("called_by"), VALUE("it"), VALUE("default")),
]


@builtin("compile/next")
@special("compile/next", patterns=_NEXT_PATTERNS, unpack="match")
def special_next(compiler, caps):
    """
    Compile (compile/next IT [DEFAULT]). One step of either iteration
    protocol via OBJECT_NEXT, ambient async: exhaustion raises
    StopIteration in the one-argument form and yields DEFAULT in the
    two-argument form
    """

    called_by = caps["called_by"]

    frag = compiler.fragment(called_by.datum)
    frag.position(called_by.position)
    frag.merge(compiler.full_compile(caps["it"].datum))

    if "default" in caps:
        frag.merge(compiler.full_compile(caps["default"].datum))
        frag.instruction(OpCode.OBJECT_NEXT, 2)
    else:
        frag.instruction(OpCode.OBJECT_NEXT, 1)

    return frag


@builtin("compile/~")
@special("compile/~", patterns=_UNARY_PATTERNS, unpack="kwargs")
def special_bit_invert(compiler, called_by, obj):
    """
    Compile (compile/~ OBJ). Bitwise inversion via __invert__
    """

    return emit_unary_op(compiler, called_by, obj, OpCode.OBJECT_BIT_INVERT)


@builtin("compile/#str")
@special("compile/#str", patterns=_STR_PATTERNS, unpack="kwargs")
def special_str(compiler, called_by, args):
    """
    Compile (compile/#str OBJ ...). String conversion via __str__, variadic
    for f-string style multi-part concatenation
    """

    head = called_by.datum
    body = args.datum

    if is_nil(body):
        frag = compiler.fragment(head)
        frag.instruction(OpCode.CONST, "")
        return frag

    call_form = splat_call_form(head, body)
    if call_form is not None:
        return call_form

    return emit_reduce_op(
        compiler,
        called_by,
        args,
        OpCode.OBJECT_STR,
        lambda a, b: str(a) + str(b),
        py_unary=str)


@builtin("compile/len")
@special("compile/len", patterns=_UNARY_PATTERNS, unpack="kwargs")
def special_len(compiler, called_by, obj):
    """
    Compile (compile/len OBJ). Length query via __len__
    """

    return emit_unary_op(compiler, called_by, obj, OpCode.OBJECT_LEN)


@builtin("compile/type")
@special("compile/type", patterns=_UNARY_PATTERNS, unpack="kwargs")
def special_type(compiler, called_by, obj):
    """
    Compile (compile/type OBJ). Type introspection via type()
    """

    return emit_unary_op(compiler, called_by, obj, OpCode.OBJECT_TYPE)


@builtin("compile/dir")
@special("compile/dir", patterns=_UNARY_PATTERNS, unpack="kwargs")
def special_dir(compiler, called_by, obj):
    """
    Compile (compile/dir OBJ). Attribute listing via __dir__
    """

    return emit_unary_op(compiler, called_by, obj, OpCode.OBJECT_DIR)


@builtin("compile/vars")
@special("compile/vars", patterns=_UNARY_PATTERNS, unpack="kwargs")
def special_vars(compiler, called_by, obj):
    """
    Compile (compile/vars OBJ). Attribute dict via __dict__
    """

    return emit_unary_op(compiler, called_by, obj, OpCode.OBJECT_VARS)


# operator symbol -> Operand op-type. Both the plain and compile/ symbol forms
# of each operator map to the same op-type.
_OPERAND_BY_SYM = {
    '+': Operand.ADD,
    '-': Operand.SUBTRACT,
    '*': Operand.MULTIPLY,
    '**': Operand.POWER,
    '/': Operand.TRUE_DIVIDE,
    '//': Operand.FLOOR_DIVIDE,
    '%': Operand.MODULO,
    '@': Operand.MATRIX_MULTIPLY,
    '<<': Operand.BIT_LEFT,
    '>>': Operand.BIT_RIGHT,
    '&': Operand.BIT_AND,
    '^': Operand.BIT_XOR,
    '|': Operand.BIT_OR,
}

_INPLACE_OPERAND_MAP = {}
for _sym_text, _binop in _OPERAND_BY_SYM.items():
    _INPLACE_OPERAND_MAP[symbol(_sym_text)] = _binop


_INPLACE_PATTERNS = [
    MATCH(
        CALLEDBY("called_by"),
        SYMBOL("op"),
        SYMBOL("var"),
        VALUE("value")),
]

_INPLACE_ITEM_PATTERNS = [
    MATCH(
        CALLEDBY("called_by"),
        SYMBOL("op"),
        VALUE("obj"),
        VALUE("key"),
        BODY("values")),
]

_INPLACE_ATTR_PATTERNS = [
    MATCH(
        CALLEDBY("called_by"),
        SYMBOL("op"),
        VALUE("obj"),
        SYMBOL("attr"),
        BODY("values")),
]


def inplace_operand(op):
    """
    The Operand op-type an inplace operator symbol names

    :param op: the operator capture
    :return: the Operand
    :raises SpecialError: when the symbol names no operator
    """

    binop = _INPLACE_OPERAND_MAP.get(op.datum)
    if binop is None:
        raise SpecialError(
            "unknown inplace operator", op.datum, position=op.position)

    return binop


@builtin("compile/inplace")
@special("compile/inplace", patterns=_INPLACE_PATTERNS, unpack="kwargs")
def special_inplace(compiler, called_by, op, var, value):
    """
    Compile (compile/inplace OP VAR VALUE). In-place update: VAR OP= VALUE.
    OP is any numeric or bitwise operator (+, -, *, /, //, %, **, @, &, |, ^,
    <<, >>).
    """

    binop = inplace_operand(op)

    value_expr = value.datum
    name = str(var.datum)
    pos = op.position

    frag = compiler.fragment(called_by.datum)

    if not is_pair(value_expr):
        frag.position(pos)

    frag.request_for_write(name, position=pos)
    frag.merge(compiler.full_compile(value_expr))
    # EXTRA row [OPCOUNT(op-type, count=1), VAR(name)]; resolution lowers the
    # name to a fast slot (-> FAST_INPLACE) when possible.
    frag.instruction(OpCode.VAR_INPLACE,
                     ((binop, 1), name, 0, 0))
    frag.instruction(OpCode.CONST, None)

    return frag


@builtin("compile/inplace-item")
@special("compile/inplace-item", patterns=_INPLACE_ITEM_PATTERNS,
         unpack="kwargs")
def special_inplace_item(compiler, called_by, op, obj, key, values):
    """
    Compile (compile/inplace-item OP OBJ KEY VALUE...). Multiple values
    reduce left-to-right through the single OBJECT_INPLACE instruction.
    """

    binop = inplace_operand(op)

    value_exprs = list(values.datum.unpack())

    if (binop is Operand.POWER
            and len(value_exprs) != 1):
        raise SpecialError(
            "inplace ** takes exactly one value",
            op.datum, position=op.position)

    count = len(value_exprs)
    frag = compiler.fragment(called_by.datum)

    if not is_pair(value_exprs[0]):
        frag.position(op.position)

    keyfrag = compiler.full_compile(key.datum)
    key = feeder_expr(keyfrag)
    valfrags = [compiler.full_compile(value_expr) for value_expr in value_exprs]
    value_feeders = all(feeder_expr(f)[0] is not None for f in valfrags)

    obj_op, key_op, fields = emit_item_place(
        compiler, frag, obj, keyfrag, key, value_feeders)
    for valfrag in valfrags:
        frag.merge(valfrag)
    if fields:
        frag.instruction(
            item_opcode(obj_op, key_op, 'INPLACE'),
            item_inplace_arg((binop, count), fields))
    else:
        frag.instruction(OpCode.ITEM_INPLACE, (binop, count))

    frag.instruction(OpCode.CONST, None)

    return frag


@builtin("compile/inplace-attr")
@special("compile/inplace-attr", patterns=_INPLACE_ATTR_PATTERNS,
         unpack="kwargs")
def special_inplace_attr(compiler, called_by, op, obj, attr, values):
    """
    Compile (compile/inplace-attr OP OBJ ATTR VALUE...). Multiple values
    reduce left-to-right into OBJ.ATTR through a single fused instruction:
    VAR_ATTR_INPLACE when OBJ is a variable (resolution lowers it to
    FAST_ATTR_INPLACE), otherwise ATTR_INPLACE with OBJ on the stack.
    """

    binop = inplace_operand(op)

    value_exprs = list(values.datum.unpack())

    if (binop is Operand.POWER
            and len(value_exprs) != 1):
        raise SpecialError(
            "inplace ** takes exactly one value",
            op.datum, position=op.position)

    obj_expr = obj.datum
    sym = str(attr.datum)
    count = len(value_exprs)
    frag = compiler.fragment(called_by.datum)

    if not is_pair(value_exprs[0]):
        frag.position(op.position)

    if is_symbol(obj_expr):
        # obj is a variable: fused VAR_ATTR_INPLACE, derefing the name and
        # get/op/set-ing the attribute in one op. Only the values ride the
        # stack. Resolution lowers this to FAST_ATTR_INPLACE where possible.
        name = str(obj_expr)
        frag.request_var(name)
        for value_expr in value_exprs:
            frag.merge(compiler.full_compile(value_expr))
        frag.instruction(OpCode.VAR_ATTR_INPLACE,
                         ((binop, count), name, sym, 0))
    else:
        # obj is an expression: fused ATTR_INPLACE, obj left on the stack
        # below the values -> [obj, v0 .. v_{count-1}].
        frag.merge(compiler.full_compile(obj_expr))
        for value_expr in value_exprs:
            frag.merge(compiler.full_compile(value_expr))
        frag.instruction(OpCode.ATTR_INPLACE,
                         ((binop, count), sym, 0, 0))

    frag.instruction(OpCode.CONST, None)

    return frag


# The end.
