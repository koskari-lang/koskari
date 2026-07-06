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
koskari.compile.specials.quasiquote

Special form handlers for compile/quasiquote, compile/unquote,
and compile/unquote-splicing.

Quasi-quote constructs a pair structure at runtime, with selected
sub-expressions evaluated via unquote (,) and lists spliced in via
unquote-splicing (,@).

The compiler emits CONST, PROPER, CONS, and PAIR_SPLICE opcodes:

- CONST atom: push a constant symbol, keyword, number, etc.
- PROPER n: pop N elements, build a proper list (appends nil automatically)
- CONS n: pop N elements using the last as an explicit tail (for dotted pairs)
- PAIR_SPLICE n: concatenate N pair/tuple segments into one proper list

When a segment of a quasi-quoted list contains only compile-time-constant
elements, it is emitted as a single CONST (sym_a, sym_b, ...) tuple rather
than N individual CONST instructions followed by PROPER N. PAIR_SPLICE
accepts tuples as segments natively.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from ...lib import bracket_nil, bracket_pair, nil, OpCode, symbol
from ...types import is_nil, is_pair
from ...exceptions import SpecialError
from ..compiler import _is_const
from ...matcher import CALLEDBY, MATCH, REST
from ...bootstrap import builtin
from ...syntax import special

_symbol_quasiquote = symbol("compile/quasiquote")
_symbol_quote = symbol("compile/quote")
_symbol_unquote = symbol("compile/unquote")
_symbol_unquote_splicing = symbol("compile/unquote-splicing")

_DEFAULT_PATTERNS = (MATCH(CALLEDBY(), REST("rest")),)


def _qq_expr(compiler, frag, expr, level):
    """
    Emit bytecode for a single quasi-quoted expression at the given nesting
    level. At level 0, unquote escapes to evaluated code. Above level 0,
    unquote and quasiquote forms are reconstructed as literal data.

    :param compiler: the active Compiler
    :param frag: the ByteCodeFragment to emit into
    :param expr: the source expression to quasi-quote
    :param level: nesting depth (0 = active quasi-quote level)
    """

    if _is_const(expr):
        frag.instruction(OpCode.KOS_OP_CONST, expr)
        return

    # redundant with _is_const(expr)
    # if is_nil(expr):
    #     frag.instruction(OpCode.KOS_OP_CONST, nil)
    #     return

    if not is_pair(expr):
        frag.instruction(OpCode.KOS_OP_CONST, expr)
        return

    head, tail = expr

    if head is _symbol_unquote:
        if is_nil(tail) or not is_pair(tail):
            raise SpecialError("unquote requires one argument", expr)
        inner, rest = tail
        if not is_nil(rest):
            raise SpecialError("unquote takes one argument", expr)

        if level == 0:
            frag.merge(compiler.full_compile(inner))
        else:
            frag.instruction(OpCode.KOS_OP_CONST, _symbol_unquote)
            _qq_expr(compiler, frag, inner, level - 1)
            frag.instruction(OpCode.KOS_OP_PROPER, 2)

        return

    if head is _symbol_unquote_splicing:
        raise SpecialError("unquote-splicing not in list position", expr)

    if head is _symbol_quasiquote:
        if is_nil(tail) or not is_pair(tail):
            raise SpecialError("quasiquote requires one argument", expr)
        inner, rest = tail
        if not is_nil(rest):
            raise SpecialError("quasiquote takes one argument", expr)

        frag.instruction(OpCode.KOS_OP_CONST, _symbol_quasiquote)
        _qq_expr(compiler, frag, inner, level + 1)
        frag.instruction(OpCode.KOS_OP_PROPER, 2)
        return

    # [ ] lists build bracket cells directly during reconstruction:
    # _qq_list picks its constructor (bracket-cons / bracket-splice) at each
    # emission site, so construction is parallel rather than build-then-coerce.
    _qq_list(compiler, frag, expr, level, isinstance(expr, bracket_pair))


def _is_splice(expr, level):
    """
    Return True if expr is a (unquote-splicing ...) form active at level 0
    """

    return (
        level == 0
        and is_pair(expr)
        and not is_nil(expr)
        and expr[0] is _symbol_unquote_splicing
    )


def _emit_call_var(frag, name, count):
    """
    Emit a CALL_VAR to a named runtime builtin with `count` args already on
    the stack (used for the bracket-cons / bracket-splice runtime calls).
    """

    frag.request_var(name)
    frag.instruction(OpCode.KOS_OP_CALL_VAR, (name, count, 0, 0))


def _emit_segment(compiler, frag, segment, as_tuple_for_splice=False,
                  bracket=False):
    """
    Emit a completed proper-list segment of non-splice elements and return
    True, or return False if segment is empty.

    When as_tuple_for_splice is True and every element is a compile-time
    constant, emits a single CONST tuple (which PAIR_SPLICE consumes
    directly). Otherwise emits each element, then closes the list: a
    plain-pair segment with PROPER n, or — when this is a bracket list's
    whole-list build (bracket and not a splice segment) — a bracket-cons
    call with an explicit bracket-nil tail, so the cells are built bracket
    directly.

    :param compiler: the active Compiler
    :param frag: the ByteCodeFragment to emit into
    :param segment: list of pre-compiled (frag_or_const, is_const) pairs
    :param as_tuple_for_splice: if True, allow CONST-tuple fast path
    :param bracket: if True, the enclosing list is a bracket-pair
    :return: True if anything was emitted
    """

    if not segment:
        return False

    if as_tuple_for_splice and all(flag for _, flag in segment):
        const_tuple = tuple(v for v, _ in segment)
        frag.instruction(OpCode.KOS_OP_CONST, const_tuple)
        return True

    for sub_frag, is_c in segment:
        if is_c:
            frag.instruction(OpCode.KOS_OP_CONST, sub_frag)
        else:
            frag.merge(sub_frag)

    if bracket and not as_tuple_for_splice:
        # whole-list build for a [ ] form: bracket-cons elements + bracket-nil
        frag.instruction(OpCode.KOS_OP_CONST, bracket_nil)
        _emit_call_var(frag, "bracket-cons", len(segment) + 1)
    else:
        frag.instruction(OpCode.KOS_OP_PROPER, len(segment))

    return True


def _compile_element(compiler, frag, expr, level):
    """
    Compile one element of a quasi-quoted list for storage in a segment.

    Returns (value_or_subfrag, is_const). If is_const is True, value_or_subfrag
    is the constant value itself. If False, it is a ByteCodeFragment whose
    instructions emit the value when merged.

    :param compiler: the active Compiler
    :param frag: the enclosing fragment (used for fragment creation)
    :param expr: the element expression
    :param level: nesting depth
    :return: (value_or_subfrag, is_const)
    """

    if _is_const(expr):
        return (expr, True)

    if is_nil(expr):
        return (nil, True)

    sub = compiler.fragment("qq_elem")
    _qq_expr(compiler, sub, expr, level)
    return (sub, False)


def _qq_list(compiler, frag, lst, level, bracket=False):
    """
    Emit bytecode for a quasi-quoted pair (list or dotted pair).

    Walks the pair structure, accumulating elements into segments separated
    by unquote-splicing sites. Each segment is flushed via _emit_segment.
    Splice expressions are compiled and pushed directly as PAIR_SPLICE args.
    A final PAIR_SPLICE n is emitted when there was at least one splice.

    Dotted-pair tails use CONS instead of PROPER so the cdr is explicit.

    When `bracket` is True (the source is a bracket-pair), the constructors
    are swapped for their bracket forms: bracket-cons for whole-list and
    dotted builds, bracket-splice for splices, and bracket-nil for empty.
    Construction is direct — no plain list is built and then converted.

    :param compiler: the active Compiler
    :param frag: the ByteCodeFragment to emit into
    :param lst: the pair (proper or improper) to walk
    :param level: nesting depth
    :param bracket: True when lst is a bracket-pair
    """

    segment = []
    splice_count = 0
    has_splice = False

    current = lst

    while is_pair(current) and not is_nil(current):
        elem = current.car
        current = current.cdr

        # If current is now a non-nil, non-pair value, it's the dotted tail.
        # Stop iterating: the loop condition will exit and the dotted-tail
        # branch below will handle it.
        if not is_nil(current) and not is_pair(current):

            # elem is the last element before the dotted tail of an improper
            # list
            segment.append(_compile_element(compiler, frag, elem, level))
            break

        # Improper list whose dotted tail is (compile/unquote …): `current` is
        # that pair without a wrapping cons(..., nil).  Proper `(,x ,y)` has
        # current = cons(uqy, nil) after the first step (cdr is nil), so this
        # branch does not fire there.
        # `is_pair(nil)` is true; guard before `.cdr` (cannot get cdr of nil).
        if (not is_nil(current)
                and is_pair(current)
                and not is_nil(current.cdr)
                and current.car is _symbol_unquote):
            segment.append(_compile_element(compiler, frag, elem, level))
            break

        if _is_splice(elem, level):
            splice_inner_list = elem[1]
            if is_nil(splice_inner_list) or not is_pair(splice_inner_list):
                raise SpecialError(
                    "unquote-splicing requires one argument",
                    elem)
            splice_inner, splice_rest = splice_inner_list
            if not is_nil(splice_rest):
                raise SpecialError(
                    "unquote-splicing takes one argument",
                    elem)

            has_splice = True

            if _emit_segment(compiler, frag, segment, as_tuple_for_splice=True):
                splice_count += 1
            segment = []

            frag.merge(compiler.full_compile(splice_inner))
            splice_count += 1

        else:
            segment.append(_compile_element(compiler, frag, elem, level))

    # current is now the cdr after the last pair: nil (proper) or a value (dotted)
    if not is_nil(current):
        if _is_splice(current, level):
            raise SpecialError(
                "unquote-splicing cannot appear in dotted tail position",
                current)

        if has_splice:
            raise SpecialError(
                "unquote-splicing cannot be mixed with a dotted tail",
                lst)

        # Emit the segment elements (no PROPER — tail will be explicit for CONS)
        for sub_frag, is_c in segment:
            if is_c:
                frag.instruction(OpCode.KOS_OP_CONST, sub_frag)
            else:
                frag.merge(sub_frag)

        # Emit the dotted tail value
        if _is_const(current):
            frag.instruction(OpCode.KOS_OP_CONST, current)
        elif (is_pair(current)
              and not is_nil(current)
              and current[0] is _symbol_unquote
              and level == 0):
            unquote_body = current[1]
            if is_nil(unquote_body) or not is_pair(unquote_body):
                raise SpecialError(
                    "unquote requires one argument",
                    current)
            inner, rest = unquote_body
            if not is_nil(rest):
                raise SpecialError(
                    "unquote takes one argument",
                    current)
            frag.merge(compiler.full_compile(inner))
        else:
            _qq_expr(compiler, frag, current, level)

        # explicit-tail build: bracket-cons for [ ], CONS for ( )
        if bracket:
            _emit_call_var(frag, "bracket-cons", len(segment) + 1)
        else:
            frag.instruction(OpCode.KOS_OP_CONS, len(segment) + 1)
        return

    # Proper list: flush the final segment
    if _emit_segment(compiler, frag, segment, as_tuple_for_splice=has_splice,
                     bracket=bracket):
        splice_count += 1

    if has_splice:
        # Always emit a splice when any ,@ ran. A lone ,@ leaves one segment on
        # the stack (list/tuple/pair/iterable); skipping it left Python
        # list/tuple values unevaluated into Koskari proper lists (PAIR_SPLICE.c
        # flattens segments into cons cells). splice_count counts every segment:
        # literal PROPER/tuple chunks plus each splice result. bracket-splice is
        # the [ ] twin (only its leading cell is a bracket-pair).
        if splice_count >= 1:
            if bracket:
                _emit_call_var(frag, "bracket-splice", splice_count)
            else:
                frag.instruction(OpCode.KOS_OP_PAIR_SPLICE, splice_count)
    elif splice_count == 0:
        # Empty list: `() → nil, `[] → bracket-nil
        frag.instruction(OpCode.KOS_OP_CONST, bracket_nil if bracket else nil)


@builtin("compile/quasiquote")
@special("compile/quasiquote", patterns=_DEFAULT_PATTERNS)
def special_quasiquote(compiler, source, tc=False):
    """
    Compile (compile/quasiquote EXPR) — the backtick quasi-quote special form.

    Emits CONST, PROPER, CONS, and PAIR_SPLICE opcodes to construct the
    quoted structure at runtime, with unquote and unquote-splicing sites
    evaluated as normal expressions.
    """

    called_by, body = source

    if is_nil(body) or not is_pair(body):
        raise SpecialError("quasiquote requires one argument", source)

    expr, rest = body
    if not is_nil(rest):
        raise SpecialError("quasiquote takes one argument", source)

    frag = compiler.fragment(called_by)
    frag.position_of(source)
    _qq_expr(compiler, frag, expr, 0)
    return frag


@builtin("compile/unquote")
@special("compile/unquote", patterns=_DEFAULT_PATTERNS)
def special_unquote(compiler, source, tc=False):
    """
    Raise SpecialError — unquote is only valid inside quasiquote
    """

    raise SpecialError("unquote used outside of quasiquote", source)


@builtin("compile/unquote-splicing")
@special("compile/unquote-splicing", patterns=_DEFAULT_PATTERNS)
def special_unquote_splicing(compiler, source, tc=False):
    """
    Raise SpecialError — unquote-splicing is only valid inside quasiquote
    """

    raise SpecialError(
        "unquote-splicing used outside of quasiquote",
        source)


# The end.
