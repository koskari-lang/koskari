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
from ...matcher import CALLEDBY, MATCH, REST, VALUE
from ...syntax import special
from ...types import (
    get_position, is_keyword, is_nil, is_pair, is_symbol, unpack)
from ..helpers import const_only_expr, is_symbolish, match, splat_call_form


_DEFAULT_SPECIAL_PATTERNS = (MATCH(CALLEDBY(), REST("rest")),)


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


def _reduce_op(
        op,
        py_fold,
        compiler,
        source,
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

    :param op: OpCode for the OBJECT_* instruction, or None when operand is set
    :param py_fold: binary Python callable for constant folding, or None
    :param compiler: active Compiler
    :param source: raw source pair (called_by . body)
    :param operand: Operand op-type for OBJECT_OPERAND, or None
    :param py_unary: unary Python callable applied to a lone constant, or
        None when the operator has no single-argument form
    :return: ByteCodeFragment
    :raises SpecialError: when too few arguments are supplied, or when
        py_unary rejects the lone constant
    """

    called_by, body = source

    frag = compiler.fragment(called_by)

    if is_nil(body):
        raise SpecialError(
            f"{called_by} requires at least one argument",
            source)

    expr, rest = body
    if is_nil(rest) and not is_keyword(expr):
        if py_unary is None:
            raise SpecialError(
                f"{called_by} requires at least two arguments",
                source)

        frag.position_of(body)
        expr_frag = compiler.full_compile(expr)
        is_const, const_val = const_only_expr(expr_frag)

        if is_const:
            try:
                value = py_unary(const_val)
            except (TypeError, ValueError, ArithmeticError) as exc:
                raise SpecialError(
                    f"{called_by} cannot be applied to {const_val!r}: {exc}",
                    source)
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
                return cons(symbol("call"), called_by, acc, body)
            else:
                return cons(symbol("call"), called_by, body)

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


def _binary_op(op, compiler, source, opa=0, operand=None):
    """
    Compile a strictly binary operator (always pops 2 values off the stack).

    Most binary OBJECT_* opcodes are NOARG (arg ignored by the C handler) so
    opa defaults to 0. A few implementations check the arg value at runtime
    (e.g. OBJECT_DIVMOD requires opa==2); callers can override opa for those
    cases.

    When operand is given, the (strictly binary) operation is emitted as the
    parameterized OBJECT_OPERAND instruction carrying (operand, 2); otherwise
    the supplied op opcode is emitted with opa.

    :param op: OpCode for the OBJECT_* instruction, or None when operand is set
    :param compiler: active Compiler
    :param source: raw source pair (called_by . body)
    :param opa: argument value encoded into the instruction (default 0)
    :param operand: Operand op-type for OBJECT_OPERAND, or None
    :return: ByteCodeFragment
    :raises SpecialError: when argument count is not exactly 2
    """

    called_by, body = source

    frag = compiler.fragment(called_by)
    count = 0

    current = body
    while not is_nil(current):
        frag.position_of(current)
        expr, current = current
        frag.merge(compiler.full_compile(expr))
        count += 1

    if count != 2:
        raise SpecialError(
            "%s requires exactly 2 arguments, got %d" % (called_by, count),
            source)

    if operand is not None:
        frag.instruction(OpCode.OBJECT_OPERAND, (operand, 2))
    else:
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
    while not is_nil(current):
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


_FORMAT_PATTERNS = [
    MATCH(VALUE("obj")),
    MATCH(VALUE("obj"), VALUE("rules")),
]

_FORMAT_PATTERNS_FULL = tuple(
    MATCH(CALLEDBY(), *m._children)
    for m in _FORMAT_PATTERNS)


@builtin("compile/format")
@special("compile/format", patterns=_FORMAT_PATTERNS_FULL)
def special_format(compiler, source):
    """
    Compile (compile/format OBJ [RULES]). Rules may be dynamic, constant str,
    or quoted symbol constant. The all-constant case folds to CONST here,
    where a bad rules value diagnoses at compile time; every other shape
    emits naively, and the processor's format lowering ladder owns the
    demotion to OBJECT_FORMAT_CONST and the fused forms.
    """

    called_by, body = source
    matched = match(body, _FORMAT_PATTERNS)
    if matched is None:
        raise SpecialError(
            "compile/format requires one or two arguments",
            source)

    obj_expr = matched["obj"].datum
    obj_frag = compiler.full_compile(obj_expr)
    obj_is_const, obj_const = const_only_expr(obj_frag)

    if "rules" in matched:
        rules_expr = matched["rules"].datum
        rules_frag = compiler.full_compile(rules_expr)
        rules_is_const, rules_const = const_only_expr(rules_frag)
        if rules_is_const and is_symbol(rules_const):
            # a quoted symbol as rules means its string
            rules_const = str(rules_const)
    else:
        rules_expr = None

    frag = compiler.fragment(called_by)
    frag.position_of(source)

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


def _chain_compare(sel, compiler, source):
    """
    Compile a chained comparison operator as OBJECT_COMPARE carrying a
    (Compare selector, count) CMPCOUNT argument.

    :param sel: Compare selector for the comparison
    :param compiler: active Compiler
    :param source: raw source pair (called_by . body)

    :return: ByteCodeFragment
    :raises SpecialError: when fewer than 2 arguments are supplied
    """

    called_by, body = source

    call_form = splat_call_form(called_by, body)
    if call_form is not None:
        return call_form

    frag = compiler.fragment(called_by)
    frag.position_of(source)

    count = 0
    current = body
    while not is_nil(current):
        frag.position_of(current)
        expr, current = current
        frag.merge(compiler.full_compile(expr))
        count += 1

    if count < 2:
        raise SpecialError(
            "%s requires at least two arguments" % called_by,
            source)

    frag.instruction(OpCode.OBJECT_COMPARE, (sel, count))
    return frag


@builtin("compile/==", "compile/equal?")
@special("compile/==", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_eq(compiler, source):
    """
    Compile (compile/= A B). Test equality via __eq__
    """

    return _chain_compare(Compare.EQ, compiler, source)


@builtin("compile/!=", "compile/not-equal?")
@special("compile/!=", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_ne(compiler, source):
    """
    Compile (compile/!= A B). Test inequality via __ne__
    """

    return _chain_compare(Compare.NE, compiler, source)


@builtin("compile/<", "compile/lt?")
@special("compile/<", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_lt(compiler, source):
    """
    Compile (compile/< A B). Test less-than via __lt__
    """

    return _chain_compare(Compare.LT, compiler, source)


@builtin("compile/<=", "compile/le?")
@special("compile/<=", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_le(compiler, source):
    """
    Compile (compile/<= A B). Test less-than-or-equal via __le__
    """

    return _chain_compare(Compare.LE, compiler, source)


@builtin("compile/>", "compile/gt?")
@special("compile/>", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_gt(compiler, source):
    """
    Compile (compile/> A B). Test greater-than via __gt__
    """

    return _chain_compare(Compare.GT, compiler, source)


@builtin("compile/>=", "compile/ge?")
@special("compile/>=", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_ge(compiler, source):
    """
    Compile (compile/>= A B). Test greater-than-or-equal via __ge__
    """

    return _chain_compare(Compare.GE, compiler, source)


# ---------------------------------------------------------------------------
# Predicate interface (NOARG binary / COUNT variadic)
# ---------------------------------------------------------------------------

@builtin("compile/in", "compile/in?")
@special("compile/in", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_in(compiler, source):
    """
    Compile (compile/in ITEM COLLECTION). Membership test via __contains__
    """

    return _binary_op(OpCode.OBJECT_COMPARE, compiler, source,
                      opa=(Compare.IN, 2))


@builtin("compile/not-in", "compile/not-in?")
@special("compile/not-in", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_not_in(compiler, source):
    """
    Compile (compile/not-in ITEM COLLECTION). Negated membership test
    """

    return _binary_op(OpCode.OBJECT_COMPARE, compiler, source,
                      opa=(Compare.NOT_IN, 2))


@builtin("compile/is", "compile/is?")
@special("compile/is", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_is(compiler, source):
    """
    Compile (compile/is A B). Identity test
    """

    return _binary_op(OpCode.OBJECT_COMPARE, compiler, source,
                      opa=(Compare.IS, 2))


@builtin("compile/is-not", "compile/is-not?")
@special("compile/is-not", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_is_not(compiler, source):
    """
    Compile (compile/is-not A B). Negated identity test
    """

    return _binary_op(OpCode.OBJECT_COMPARE, compiler, source,
                      opa=(Compare.IS_NOT, 2))


def _type_check_op(sel, compiler, source):
    """
    Compile a type-check operator as OBJECT_COMPARE carrying the
    IS_INSTANCE or IS_SUBCLASS selector. The CMPCOUNT count is the total
    operand count (value plus N types); the handler treats operands past
    the value as the match, tupled when N > 1. Requires at least 2
    arguments (value plus at least one type).

    Calling forms:
      (compile/instance? VAL TYPE)         -> count = 2  (single type)
      (compile/instance? VAL T1 T2 ...)    -> count = N+1  (types as tuple)

    :param sel: Compare.IS_INSTANCE or Compare.IS_SUBCLASS
    :param compiler: active Compiler
    :param source: raw source pair (called_by . body)
    :return: ByteCodeFragment
    :raises SpecialError: when fewer than 2 arguments are supplied
    """

    called_by, body = source

    frag = compiler.fragment(called_by)
    exprs = list(body.unpack()) if not is_nil(body) else []

    if len(exprs) < 2:
        raise SpecialError(
            "%s requires at least 2 arguments (value and type), got %d"
            % (called_by, len(exprs)),
            source)

    frag.position_of(source)

    for expr in exprs:
        frag.merge(compiler.full_compile(expr))

    frag.instruction(OpCode.OBJECT_COMPARE, (sel, len(exprs)))
    return frag


@builtin("compile/instance?")
@special("compile/instance?", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_isinstance(compiler, source):
    """
    Compile (compile/instance? OBJ TYPE ...). Instance type check
    """

    return _type_check_op(
        Compare.IS_INSTANCE, compiler, source)


@builtin("compile/subclass?")
@special("compile/subclass?", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_issubclass(compiler, source):
    """
    Compile (compile/subclass? CLS TYPE ...). Subclass type check
    """

    return _type_check_op(
        Compare.IS_SUBCLASS, compiler, source)


# ---------------------------------------------------------------------------
# Numeric interface — reducible COUNT opcodes with constant folding
# ---------------------------------------------------------------------------

@builtin("compile/+")
@special("compile/+", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_add(compiler, source):
    """
    Compile (compile/+ A B ...). Variadic addition via __add__
    """

    return _reduce_op(
        None, _op.add, compiler, source,
        operand=Operand.ADD, py_unary=_op.pos)


@builtin("compile/-")
@special("compile/-", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_subtract(compiler, source):
    """
    Compile (compile/- A B ...). Variadic subtraction via __sub__
    """

    return _reduce_op(
        None, _op.sub, compiler, source,
        operand=Operand.SUBTRACT, py_unary=_op.neg)


@builtin("compile/*")
@special("compile/*", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_multiply(compiler, source):
    """
    Compile (compile/* A B ...). Variadic multiplication via __mul__
    """

    return _reduce_op(
        None, _op.mul, compiler, source,
        operand=Operand.MULTIPLY)


@builtin("compile/**")
@special("compile/**", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_power(compiler, source):
    """
    Compile (compile/** BASE EXP [MOD]). Exponentiation via __pow__
    """

    return _reduce_op(
        None, None, compiler, source,
        operand=Operand.POWER)


@builtin("compile/&")
@special("compile/&", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_bit_and(compiler, source):
    """
    Compile (compile/& A B ...). Variadic bitwise AND via __and__
    """

    return _reduce_op(
        None, _op.and_, compiler, source,
        operand=Operand.BIT_AND)


@builtin("compile/|")
@special("compile/|", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_bit_or(compiler, source):
    """
    Compile (compile/| A B ...). Variadic bitwise OR via __or__
    """

    return _reduce_op(
        None, _op.or_, compiler, source,
        operand=Operand.BIT_OR)


@builtin("compile/^")
@special("compile/^", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_bit_xor(compiler, source):
    """
    Compile (compile/^ A B ...). Variadic bitwise XOR via __xor__
    """

    return _reduce_op(
        None, _op.xor, compiler, source,
        operand=Operand.BIT_XOR)


@builtin("compile///")
@special("compile///", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_floor_divide(compiler, source):
    """
    Compile (compile/// A B ...). Variadic floor division via __floordiv__
    """

    return _reduce_op(
        None, _op.floordiv, compiler, source,
        operand=Operand.FLOOR_DIVIDE)


@builtin("compile/@")
@special("compile/@", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_matrix_multiply(compiler, source):
    """
    Compile (compile/@ A B ...). Variadic matrix multiplication via __matmul__
    """

    return _reduce_op(
        None, _op.matmul, compiler, source,
        operand=Operand.MATRIX_MULTIPLY)


# ---------------------------------------------------------------------------
# Numeric interface — NOARG binary opcodes (no constant folding)
# ---------------------------------------------------------------------------

@builtin("compile//")
@special("compile//", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_true_divide(compiler, source):
    """
    Compile (compile// A B ...). True division via __truediv__
    """

    return _reduce_op(
        None, _op.truediv, compiler, source,
        operand=Operand.TRUE_DIVIDE)


@builtin("compile/%")
@special("compile/%", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_modulo(compiler, source):
    """
    Compile (compile/% A B). Modulo via __mod__
    """

    return _binary_op(
        None, compiler, source, operand=Operand.MODULO)


@builtin("compile/<<")
@special("compile/<<", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_bit_left(compiler, source):
    """
    Compile (compile/<< A B). Left bit-shift via __lshift__
    """

    return _binary_op(
        None, compiler, source, operand=Operand.BIT_LEFT)


@builtin("compile/>>")
@special("compile/>>", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_bit_right(compiler, source):
    """
    Compile (compile/>> A B). Right bit-shift via __rshift__
    """

    return _binary_op(
        None, compiler, source, operand=Operand.BIT_RIGHT)


@builtin("compile/divmod")
@special("compile/divmod", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_divmod(compiler, source):
    """
    Compile (compile/divmod A B). Combined quotient and remainder
    """

    return _binary_op(OpCode.OBJECT_DIVMOD, compiler, source, opa=2)


# ---------------------------------------------------------------------------
# General object interface — NOARG unary opcodes
# ---------------------------------------------------------------------------

@builtin("compile/not")
@special("compile/not", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_not(compiler, source):
    """
    Compile (compile/not OBJ). Boolean negation via __bool__ then invert
    """

    return _unary_op(OpCode.OBJECT_NOT, compiler, source)


@builtin("compile/nil?")
@special("compile/nil?", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_nilp(compiler, source):
    """
    Compile (nil? OBJ). True if OBJ is an empty-list singleton, either
    nil or bracket-nil. Emits NILP, which compression may lower to a
    CHECK_NIL when followed by a conditional jump.
    """

    return _unary_op(OpCode.NILP, compiler, source)


@builtin("compile/repr")
@special("compile/repr", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_repr(compiler, source):
    """
    Compile (compile/repr OBJ). String representation via __repr__
    """

    return _unary_op(OpCode.OBJECT_REPR, compiler, source)


@builtin("compile/truth")
@special("compile/truth", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_truth(compiler, source):
    """
    Compile (compile/truth OBJ). Boolean coercion via __bool__
    """

    return _unary_op(OpCode.OBJECT_TRUTH, compiler, source)


@builtin("compile/iter")
@special("compile/iter", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_iter(compiler, source):
    """
    Compile (compile/iter OBJ). Iterator acquisition uniform over both
    protocols: a koskari AsyncGenerator passes through as itself, an
    async iterable yields its __aiter__, otherwise the sync __iter__
    protocol
    """

    return _unary_op(OpCode.OBJECT_ITER, compiler, source)


_NEXT_PATTERNS = [
    MATCH(VALUE("it")),
    MATCH(VALUE("it"), VALUE("default")),
]

_NEXT_PATTERNS_FULL = tuple(
    MATCH(CALLEDBY(), *m._children)
    for m in _NEXT_PATTERNS)


@builtin("compile/next")
@special("compile/next", patterns=_NEXT_PATTERNS_FULL)
def special_next(compiler, source):
    """
    Compile (compile/next IT [DEFAULT]). One step of either iteration
    protocol via OBJECT_NEXT, ambient async: exhaustion raises
    StopIteration in the one-argument form and yields DEFAULT in the
    two-argument form
    """

    called_by, body = source
    matched = match(body, _NEXT_PATTERNS)
    if matched is None:
        raise SpecialError(
            "compile/next requires one or two arguments",
            source)

    frag = compiler.fragment(called_by)
    frag.position_of(source)
    frag.merge(compiler.full_compile(matched["it"].datum))

    if "default" in matched:
        frag.merge(compiler.full_compile(matched["default"].datum))
        frag.instruction(OpCode.OBJECT_NEXT, 2)
    else:
        frag.instruction(OpCode.OBJECT_NEXT, 1)

    return frag


@builtin("compile/~")
@special("compile/~", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_bit_invert(compiler, source):
    """
    Compile (compile/~ OBJ). Bitwise inversion via __invert__
    """

    return _unary_op(OpCode.OBJECT_BIT_INVERT, compiler, source)


@builtin("compile/#str")
@special("compile/#str", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_str(compiler, source):
    """
    Compile (compile/#str OBJ ...). String conversion via __str__, variadic
    for f-string style multi-part concatenation
    """

    called_by, body = source
    if is_nil(body):
        frag = compiler.fragment(called_by)
        frag.instruction(OpCode.CONST, "")
        return frag

    call_form = splat_call_form(called_by, body)
    if call_form is not None:
        return call_form

    return _reduce_op(
        OpCode.OBJECT_STR,
        lambda a, b: str(a) + str(b),
        compiler,
        source,
        py_unary=str)


@builtin("compile/len")
@special("compile/len", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_len(compiler, source):
    """
    Compile (compile/len OBJ). Length query via __len__
    """

    return _unary_op(OpCode.OBJECT_LEN, compiler, source)


@builtin("compile/type")
@special("compile/type", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_type(compiler, source):
    """
    Compile (compile/type OBJ). Type introspection via type()
    """

    return _unary_op(OpCode.OBJECT_TYPE, compiler, source)


@builtin("compile/dir")
@special("compile/dir", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_dir(compiler, source):
    """
    Compile (compile/dir OBJ). Attribute listing via __dir__
    """

    return _unary_op(OpCode.OBJECT_DIR, compiler, source)


@builtin("compile/vars")
@special("compile/vars", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_vars(compiler, source):
    """
    Compile (compile/vars OBJ). Attribute dict via __dict__
    """

    return _unary_op(OpCode.OBJECT_VARS, compiler, source)


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


@builtin("compile/inplace")
@special("compile/inplace", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_inplace(compiler, source):
    """
    Compile (compile/inplace OP VAR VALUE). In-place update: VAR OP= VALUE.
    OP is any numeric or bitwise operator (+, -, *, /, //, %, **, @, &, |, ^,
    <<, >>).
    """

    called_by, body = source
    if is_nil(body) or not is_pair(body):
        raise SpecialError("inplace requires op, var, and value", source)

    op_sym, rest = body
    if is_nil(rest) or not is_pair(rest):
        raise SpecialError("inplace requires op, var, and value", source)

    var_sym, rest = rest
    if is_nil(rest) or not is_pair(rest):
        raise SpecialError("inplace requires op, var, and value", source)

    value_expr, rest = rest
    if not is_nil(rest):
        raise SpecialError("inplace takes exactly three arguments", source)

    if not is_symbolish(var_sym):
        raise SpecialError("inplace var must be a symbol", source)

    binop = _INPLACE_OPERAND_MAP.get(op_sym)
    if binop is None:
        raise SpecialError("unknown inplace operator", source)

    name = str(var_sym)
    frag = compiler.fragment(called_by)

    if not is_pair(value_expr):
        frag.position_of(body)

    pos = get_position(body)
    frag.request_for_write(name, position=pos)
    frag.merge(compiler.full_compile(value_expr))
    # EXTRA row [OPCOUNT(op-type, count=1), VAR(name)]; resolution lowers the
    # name to a fast slot (-> FAST_INPLACE) when possible.
    frag.instruction(OpCode.VAR_INPLACE,
                     ((binop, 1), name, 0, 0))
    frag.instruction(OpCode.CONST, None)

    return frag


@builtin("compile/inplace-item")
@special("compile/inplace-item", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_inplace_item(compiler, source):
    """
    Compile (compile/inplace-item OP OBJ KEY VALUE...). Multiple values
    reduce left-to-right through the single OBJECT_INPLACE instruction.
    """

    called_by, body = source
    if is_nil(body) or not is_pair(body):
        raise SpecialError(
            "inplace-item requires op, obj, key, and value", source)

    op_sym, rest = body
    if is_nil(rest) or not is_pair(rest):
        raise SpecialError(
            "inplace-item requires op, obj, key, and value", source)

    obj_expr, rest = rest
    if is_nil(rest) or not is_pair(rest):
        raise SpecialError(
            "inplace-item requires op, obj, key, and value", source)

    key_expr, rest = rest
    if is_nil(rest) or not is_pair(rest):
        raise SpecialError(
            "inplace-item requires op, obj, key, and value", source)

    value_exprs = list(unpack(rest))

    binop = _INPLACE_OPERAND_MAP.get(op_sym)
    if binop is None:
        raise SpecialError("unknown inplace operator", source)

    if (binop is Operand.POWER
            and len(value_exprs) != 1):
        raise SpecialError("inplace ** takes exactly one value", source)

    frag = compiler.fragment(called_by)

    if not is_pair(value_exprs[0]):
        frag.position_of(body)

    frag.merge(compiler.full_compile(obj_expr))

    keyfrag = compiler.full_compile(key_expr)
    is_const, key_val = const_only_expr(keyfrag)

    if is_const:
        frag.instruction(OpCode.DUP, 1)
        frag.instruction(OpCode.CONST_ITEM, key_val)
    else:
        frag.merge(keyfrag)
        frag.instruction(OpCode.DUP, 2)
        frag.instruction(OpCode.ITEM, 0)

    for value_expr in value_exprs:
        frag.merge(compiler.full_compile(value_expr))
    frag.instruction(OpCode.OBJECT_INPLACE,
                     (binop, 1 + len(value_exprs)))

    if is_const:
        frag.instruction(OpCode.CONST_ITEM_STORE, key_val)
    else:
        frag.instruction(OpCode.ITEM_STORE, 0)

    frag.instruction(OpCode.CONST, None)

    return frag


@builtin("compile/inplace-attr")
@special("compile/inplace-attr", patterns=_DEFAULT_SPECIAL_PATTERNS)
def special_inplace_attr(compiler, source):
    """
    Compile (compile/inplace-attr OP OBJ ATTR VALUE...). Multiple values
    reduce left-to-right into OBJ.ATTR through a single fused instruction:
    VAR_ATTR_INPLACE when OBJ is a variable (resolution lowers it to
    FAST_ATTR_INPLACE), otherwise ATTR_INPLACE with OBJ on the stack.
    """

    called_by, body = source
    if is_nil(body) or not is_pair(body):
        raise SpecialError(
            "inplace-attr requires op, obj, attr, and value", source)

    op_sym, rest = body
    if is_nil(rest) or not is_pair(rest):
        raise SpecialError(
            "inplace-attr requires op, obj, attr, and value", source)

    obj_expr, rest = rest
    if is_nil(rest) or not is_pair(rest):
        raise SpecialError(
            "inplace-attr requires op, obj, attr, and value", source)

    key_expr, rest = rest
    if is_nil(rest) or not is_pair(rest):
        raise SpecialError(
            "inplace-attr requires op, obj, attr, and value", source)

    value_exprs = list(unpack(rest))

    if not is_symbol(key_expr):
        raise SpecialError("inplace-attr attr must be a symbol", source)

    binop = _INPLACE_OPERAND_MAP.get(op_sym)
    if binop is None:
        raise SpecialError("unknown inplace operator", source)

    if (binop is Operand.POWER
            and len(value_exprs) != 1):
        raise SpecialError("inplace ** takes exactly one value", source)

    sym = str(key_expr)
    count = len(value_exprs)
    frag = compiler.fragment(called_by)

    if not is_pair(value_exprs[0]):
        frag.position_of(body)

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
