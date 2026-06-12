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
koskari.reader.layouts.basics

Layout handlers for basic block syntax.
"""


from ...exceptions import LayoutError
from ...lib import car, cdr, cons, nil, symbol
from ...bootstrap import add_builtin, builtin
from ...types import get_position, is_pair, is_keyword
from .helpers import layout


@builtin('layout/block')
@layout(symbol('layout/block'))
def layout_block(parser, source, peers):
    """
    the default block layout handler

    eg.
    foo x:
      bar

    becomes
    (foo x bar)
    """

    if peers:
        raise LayoutError(
            'block does not take peers',
            get_position(source))

    header, body = source
    header.extend(body)
    return header


@builtin('layout/block-begin')
@layout(symbol('layout/block-begin'))
def layout_block_begin(parser, source, peers):
    """
    block layout handler that wraps the body in compile/begin

    eg.
    foo x:
      bar

    becomes
    (foo x (compile/begin bar))
    """

    if peers:
        raise LayoutError(
            'block-begin does not take peers',
            get_position(source))

    header, body = source
    header.append(cons(symbol('compile/begin'), body))
    return header


layout_flat = layout_block
add_builtin('layout/flat', layout_flat)


@builtin('layout/call')
@layout(symbol('layout/call'))
def layout_call(parser, source, peers):
    """
    a breakout of a call as a block

    eg.
    layout/call foo:
      bar
      (baz)
      alpha: beta

    becomes
    (foo bar (baz) alpha: beta)
    """

    if peers:
        raise LayoutError(
            'call does not take peers',
            get_position(source))

    header, body = source
    try:
        called_by, (fn, rest) = header
    except ValueError:
        raise LayoutError(
            'call block requires a function name',
            get_position(source))

    if rest:
        raise LayoutError(
            'call block header does not accept arguments',
            get_position(source))

    corr = body
    while corr:
        head, next = corr
        if is_pair(head) and is_keyword(head.car):
            tail = cdr(head)
            if is_pair(tail) and cdr(tail) is nil:
                val = car(tail)
            else:
                val = tail
            corr.car = car(head)
            corr.cdr = cons(val, next)
        corr = next

    return cons(symbol('compile/call'), fn, body)


layout_begin = layout_block
add_builtin('layout/begin', layout_begin)


_symbol_compile_quote = symbol('compile/quote')
_symbol_compile_quasiquote = symbol('compile/quasiquote')


@builtin('layout/quote')
@layout(symbol('layout/quote'), raw=True)
def layout_quote(parser, source, peers):
    """
    Unbox a synthetic quote layout block to ``(compile/quote INNER-BLOCK)``

    INNER-BLOCK is the raw ``:block:`` from the quoted form, not layout-expanded
    """

    if peers:
        raise LayoutError(
            'quote block does not take peers',
            get_position(source))

    header, body = source

    if body is nil or not is_pair(body):
        raise LayoutError(
            'quote block requires one inner block',
            get_position(source))

    inner, rest = body
    if rest is not nil:
        raise LayoutError(
            'quote block requires one inner block',
            get_position(source))

    return cons(_symbol_compile_quote, inner, nil)


@builtin('layout/quasiquote')
@layout(symbol('layout/quasiquote'), raw=True)
def layout_quasiquote(parser, source, peers):
    """
    Unbox a synthetic quasiquote layout block to
    ``(compile/quasiquote INNER-BLOCK)``

    INNER-BLOCK is the raw ``:block:`` from the quasiquoted form
    """

    if peers:
        raise LayoutError(
            'quasiquote block does not take peers',
            get_position(source))

    header, body = source

    if body is nil or not is_pair(body):
        raise LayoutError(
            'quasiquote block requires one inner block',
            get_position(source))

    inner, rest = body
    if rest is not nil:
        raise LayoutError(
            'quasiquote block requires one inner block',
            get_position(source))

    return cons(_symbol_compile_quasiquote, inner, nil)


# The end.
