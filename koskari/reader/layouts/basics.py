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
from ...stdlib import add_builtin, builtin
from ...types import get_position, is_pair, is_keyword
from .helpers import layout


@builtin('layout/block')
@layout(symbol('layout/block'))
def layout_block(source, peers):
    """
    the default block layout handler

    eg.
    foo x:
      bar

    becomes
    (foo x (compile/begin bar))
    """

    if peers:
        raise LayoutError(
            'block does not take peers',
            get_position(source))

    header, body = source
    header.append(cons(symbol('compile/begin'), body))
    return header


@builtin('layout/flat')
@layout(symbol('layout/flat'))
def layout_flat(source, peers):
    """
    a flat block layout handler. This is a basic block layout
    that can be copied to other names to give that form a
    flat block layout.

    eg.
    foo x:
      bar

    becomes
    (foo x bar)
    """

    if peers:
        raise LayoutError(
            'flat does not take peers',
            get_position(source))

    header, body = source
    header.extend(body)
    return header


@builtin('layout/call')
@layout(symbol('layout/call'))
def layout_call(source, peers):
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


layout_begin = layout_flat
add_builtin('layout/begin', layout_begin)


# The end.
