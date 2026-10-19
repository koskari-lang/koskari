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

The compiler emits CONST, PROPER, CONS, PAIR_SPLICE and their bracket
twins:

- CONST atom: push a constant symbol, keyword, number, etc.
- PROPER n: pop N elements, build a proper list (appends nil automatically)
- CONS n: pop N elements using the last as an explicit tail (for dotted pairs)
- PAIR_SPLICE (n, t): concatenate pair/tuple segments into one list,
  ending in nil (t = 0) or in the top stacked value (t = 1)
- BRACKET_PROPER n, BRACKET_CONS n, BRACKET_SPLICE (n, t): the same three for
  a [ ] form; BRACKET_SPLICE makes only the leading cell a bracket pair

When a segment of a quasi-quoted list contains only compile-time-constant
elements, it is emitted as a single CONST (sym_a, sym_b, ...) tuple rather
than N individual CONST instructions followed by PROPER N. PAIR_SPLICE and
BRACKET_SPLICE accept tuples as segments natively.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from ...bootstrap import builtin
from ...exceptions import SpecialError
from ...lib import OpCode, bracket_nil, bracket_pair, nil, symbol
from ...matcher import CALLEDBY, MATCH, VALUE
from ...syntax import special
from ...types import is_const, is_nil, is_pair, strip_mark


_symbol_quasiquote = symbol("quasiquote")
_symbol_quote = symbol("quote")
_symbol_unquote = symbol("unquote")
_symbol_unquote_splicing = symbol("unquote-splicing")

_ONE_PATTERNS = [
    MATCH(CALLEDBY("called_by"), VALUE("expr")),
]


def _qq_expr(compiler, frag, expr, level, keep=False):
    """
    Emit bytecode for a single quasi-quoted expression at the given nesting
    level. At level 0, unquote escapes to evaluated code. Above level 0,
    unquote and quasiquote forms are reconstructed as literal data.

    :param compiler: the active Compiler
    :param frag: the ByteCodeFragment to emit into
    :param expr: the source expression to quasi-quote
    :param level: nesting depth (0 = active quasi-quote level)
    :param keep: True to emit a scoped atom as written; False projects
        it to the symbol it spells, since quoted data is read by its
        bare spelling
    """

    if not keep:
        expr = strip_mark(expr)

    if is_const(expr) or not is_pair(expr):
        frag.instruction(OpCode.CONST, expr)
        return

    head, tail = expr
    head = strip_mark(head)

    if head is _symbol_unquote:
        if is_nil(tail) or not is_pair(tail):
            raise SpecialError("unquote requires one argument", expr)
        inner, rest = tail
        if not is_nil(rest):
            raise SpecialError("unquote takes one argument", expr)

        if level == 0:
            frag.merge(compiler.full_compile(inner))
        else:
            frag.instruction(OpCode.CONST, _symbol_unquote)
            _qq_expr(compiler, frag, inner, level - 1, keep)
            frag.instruction(OpCode.PROPER, 2)

        return

    if head is _symbol_unquote_splicing:
        if level == 0:
            raise SpecialError("unquote-splicing not in list position", expr)
        if is_nil(tail) or not is_pair(tail):
            raise SpecialError("unquote-splicing requires one argument", expr)
        inner, rest = tail
        if not is_nil(rest):
            raise SpecialError("unquote-splicing takes one argument", expr)

        # at level > 0 the splice is data, as an inner unquote is
        frag.instruction(OpCode.CONST, _symbol_unquote_splicing)
        _qq_expr(compiler, frag, inner, level - 1, keep)
        frag.instruction(OpCode.PROPER, 2)
        return

    if head is _symbol_quasiquote:
        if is_nil(tail) or not is_pair(tail):
            raise SpecialError("quasiquote requires one argument", expr)
        inner, rest = tail
        if not is_nil(rest):
            raise SpecialError("quasiquote takes one argument", expr)

        frag.instruction(OpCode.CONST, _symbol_quasiquote)
        _qq_expr(compiler, frag, inner, level + 1, keep)
        frag.instruction(OpCode.PROPER, 2)
        return

    # [ ] lists build bracket cells directly during reconstruction:
    # _qq_list picks the bracket opcode at each emission site, so
    # construction is parallel rather than build-then-coerce.
    _qq_list(
        compiler, frag, expr, level, isinstance(expr, bracket_pair), keep)


def _is_splice(expr, level):
    """
    Return True if expr is a (unquote-splicing ...) form active at level 0
    """

    return (
        level == 0
        and is_pair(expr)
        and not is_nil(expr)
        and strip_mark(expr[0]) is _symbol_unquote_splicing
    )


def _emit_segment(compiler, frag, segment, as_tuple_for_splice=False,
                  bracket=False):
    """
    Emit a completed proper-list segment of non-splice elements and return
    True, or return False if segment is empty.

    When as_tuple_for_splice is True and every element is a compile-time
    constant, emits a single CONST tuple (which PAIR_SPLICE consumes
    directly). Otherwise emits each element, then closes the list: a
    plain-pair segment with PROPER n, or -- when this is a bracket list's
    whole-list build (bracket and not a splice segment) -- BRACKET_PROPER
    n, so the cells are built bracket directly.

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
        frag.instruction(OpCode.CONST, const_tuple)
        return True

    for sub_frag, is_c in segment:
        if is_c:
            frag.instruction(OpCode.CONST, sub_frag)
        else:
            frag.merge(sub_frag)

    if bracket and not as_tuple_for_splice:
        # whole-list build for a [ ] form
        frag.instruction(OpCode.BRACKET_PROPER, len(segment))
    else:
        frag.instruction(OpCode.PROPER, len(segment))

    return True


def _compile_element(compiler, frag, expr, level, keep=False):
    """
    Compile one element of a quasi-quoted list for storage in a segment.

    Returns (value_or_subfrag, is_const). If is_const is True, value_or_subfrag
    is the constant value itself. If False, it is a ByteCodeFragment whose
    instructions emit the value when merged.

    :param compiler: the active Compiler
    :param frag: the enclosing fragment (used for fragment creation)
    :param expr: the element expression
    :param level: nesting depth
    :param keep: True to emit a scoped atom as written
    :return: (value_or_subfrag, is_const)
    """

    if not keep:
        expr = strip_mark(expr)

    if is_const(expr):
        return (expr, True)

    if is_nil(expr):
        return (nil, True)

    sub = compiler.fragment("qq_elem")
    _qq_expr(compiler, sub, expr, level, keep)
    return (sub, False)


def _qq_list(compiler, frag, lst, level, bracket=False, keep=False):
    """
    Emit bytecode for a quasi-quoted pair (list or dotted pair).

    Walks the pair structure, accumulating elements into segments separated
    by unquote-splicing sites. Each segment is flushed via _emit_segment.
    Splice expressions are compiled and pushed directly as PAIR_SPLICE
    segments. A final PAIR_SPLICE (BRACKET_SPLICE for a bracket-pair) is
    emitted when there was at least one splice.

    Dotted-pair tails use CONS instead of PROPER so the cdr is explicit.
    With a splice, the tail value is pushed after the segments and the
    splice's tail field is set, so `(a ,@xs . b) gives (a x1 x2 . b), or
    (a . b) when xs is empty. A splice as the tail itself is an error.

    When `bracket` is True (the source is a bracket-pair), the opcodes
    are swapped for their bracket twins: BRACKET_PROPER for whole-list
    builds, BRACKET_CONS for dotted builds, BRACKET_SPLICE for splices,
    and bracket-nil for empty. Construction is direct -- no plain list is
    built and then converted.

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

        # elem is the last element before a dotted tail when current is a
        # non-nil atom, or the reader's (unquote x) or (unquote-splicing x)
        # for `. ,x` or `. ,@x`, which arrives without a wrapping
        # cons(..., nil). Proper `(,x ,y)` has current = cons(uqy, nil)
        # after the first step, whose car is a pair, so the test does not
        # fire there. `is_pair(nil)` is true; guard before `.cdr`.
        tail_follows = False
        if not is_nil(current):
            if not is_pair(current):
                tail_follows = True
            elif not is_nil(current.cdr):
                tail_head = strip_mark(current.car)
                tail_follows = (tail_head is _symbol_unquote
                                or tail_head is _symbol_unquote_splicing)

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
            segment.append(
                _compile_element(compiler, frag, elem, level, keep))

        if tail_follows:
            break

    # current is now the cdr after the last pair: nil (proper) or a value (dotted)
    if not is_nil(current):
        if _is_splice(current, level):
            raise SpecialError(
                "unquote-splicing cannot appear in dotted tail position",
                current)

        if has_splice:
            # the final segment joins the splice segments; the tail value
            # follows them, and the splice's tail field makes it the last
            # cell's cdr
            if _emit_segment(
                    compiler, frag, segment, as_tuple_for_splice=True):
                splice_count += 1
        else:
            # the segment elements stay unclosed; CONS takes the tail
            for sub_frag, is_c in segment:
                if is_c:
                    frag.instruction(OpCode.CONST, sub_frag)
                else:
                    frag.merge(sub_frag)

        # Emit the dotted tail value, an atom projected like any other
        if is_const(current):
            frag.instruction(
                OpCode.CONST, current if keep else strip_mark(current))
        elif (is_pair(current)
              and not is_nil(current)
              and strip_mark(current[0]) is _symbol_unquote
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
            _qq_expr(compiler, frag, current, level, keep)

        if has_splice:
            # the segments plus the tail, tail field set
            row = (splice_count + 1, 1, 0, 0)
            op = OpCode.BRACKET_SPLICE if bracket else OpCode.PAIR_SPLICE
            frag.instruction(op, row)
        else:
            # explicit-tail build: BRACKET_CONS for [ ], CONS for ( )
            op = OpCode.BRACKET_CONS if bracket else OpCode.CONS
            frag.instruction(op, len(segment) + 1)
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
        # literal PROPER/tuple chunks plus each splice result. BRACKET_SPLICE
        # is the [ ] twin (only its leading cell is a bracket-pair).
        if splice_count >= 1:
            row = (splice_count, 0, 0, 0)
            if bracket:
                frag.instruction(OpCode.BRACKET_SPLICE, row)
            else:
                frag.instruction(OpCode.PAIR_SPLICE, row)
    elif splice_count == 0:
        # Empty list: `() → nil, `[] → bracket-nil
        frag.instruction(OpCode.CONST, bracket_nil if bracket else nil)


@builtin("compile/quasiquote")
@special("compile/quasiquote", patterns=_ONE_PATTERNS, unpack="kwargs")
def special_quasiquote(compiler, called_by, expr):
    """
    Compile (compile/quasiquote EXPR) — the backtick quasi-quote special form.

    Emits CONST, PROPER, CONS, PAIR_SPLICE and their bracket twins to
    construct the quoted structure at runtime, with unquote and unquote-splicing sites
    evaluated as normal expressions.
    """

    frag = compiler.fragment(called_by.datum)
    frag.position(called_by.position)
    _qq_expr(compiler, frag, expr.datum, 0)
    return frag


@builtin("compile/quasiquote-syntax")
@special("compile/quasiquote-syntax", patterns=_ONE_PATTERNS,
         unpack="kwargs")
def special_quasiquote_syntax(compiler, called_by, expr):
    """
    Compile (compile/quasiquote-syntax EXPR) as ``quasiquote`` does,
    keeping the marks on the atoms it emits as constants. A procedural
    macro that a template defined builds its output with this form: the
    symbols the outer template wrote carry the outer expansion's marks,
    and keeping them lets the output resolve to what that expansion
    bound. Unquote and unquote-splicing sites are the same as in
    ``quasiquote``
    """

    frag = compiler.fragment(called_by.datum)
    frag.position(called_by.position)
    _qq_expr(compiler, frag, expr.datum, 0, keep=True)
    return frag


@builtin("compile/unquote")
@special("compile/unquote", patterns=_ONE_PATTERNS, unpack="kwargs")
def special_unquote(compiler, called_by, expr):
    """
    Raise SpecialError — unquote is only valid inside quasiquote
    """

    raise SpecialError(
        "unquote used outside of quasiquote",
        expr.datum, position=called_by.position)


@builtin("compile/unquote-splicing")
@special("compile/unquote-splicing", patterns=_ONE_PATTERNS,
         unpack="kwargs")
def special_unquote_splicing(compiler, called_by, expr):
    """
    Raise SpecialError — unquote-splicing is only valid inside quasiquote
    """

    raise SpecialError(
        "unquote-splicing used outside of quasiquote",
        expr.datum, position=called_by.position)


# The end.
