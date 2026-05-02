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
from ...types import get_position, is_pair, is_keyword
from .helpers import layout


@layout(symbol('layout/block'))
def layout_block(source, peers):
    """
    the default block layout handler

    eg.
    layout/block foo x:
      bar

    becomes
    (foo x (compile/begin bar))
    """

    if peers:
        raise LayoutError(
            'block does not take peers',
            get_position(source))

    header, body = source
    _called_by, hdr = header

    hdr.append(cons(symbol('compile/begin'), body))
    return hdr


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


@layout(symbol('layout/begin'))
def layout_begin(source, peers):
    """
    lowers into a compile/begin

    eg.
    layout/begin:
      (bar)

    becomes
    (compile/begin bar)
    """

    if peers:
        raise LayoutError(
            'block does not take peers',
            get_position(source))

    called_by, exprs = source
    return cons(symbol('compile/begin'), exprs)


# The end.
