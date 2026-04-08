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
koskari.reader.layouts.conditionals

Layout handlers for conditional block syntax.
"""


from ...exceptions import LayoutError
from ...lib import car, cdr, cons, keyword, nil, symbol
from ...types import get_position, unpack
from .helpers import layout


_symbol_elif = symbol('elif')
_symbol_else = symbol('else')
_keyword_else = keyword('else')


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


@layout(symbol('layout/if'), peers=(_symbol_elif, _symbol_else))
def layout_if(source, peers):
    """
    Lower if/elif/else block sources to compile/cond for compilation.

    :param source: ((if COND) . BODY) pair
    :param peers: list of peer (header . body) pairs
    """

    header, body = source
    header_parts = list(unpack(header))
    pargs = header_parts[1:]

    if len(pargs) != 1:
        raise LayoutError(
            'if block requires exactly one condition',
            get_position(source))

    clauses = [cons(pargs[0], body)]
    saw_else = False

    for peer in peers:
        pheader, pbody = peer
        phead = car(pheader)
        ppargs = list(unpack(cdr(pheader)))

        if phead is _symbol_elif:
            if saw_else:
                raise LayoutError(
                    'elif cannot follow else',
                    get_position(peer))
            if len(ppargs) != 1:
                raise LayoutError(
                    'elif block requires exactly one condition',
                    get_position(peer))
            clauses.append(cons(ppargs[0], pbody))

        elif phead is _symbol_else:
            if saw_else:
                raise LayoutError(
                    'multiple else peers are not allowed',
                    get_position(peer))
            if ppargs:
                raise LayoutError(
                    'else block does not take arguments',
                    get_position(peer))
            clauses.append(cons(_keyword_else, pbody))
            saw_else = True

        else:
            raise LayoutError(
                'invalid peer for if block: %r' % (phead,),
                get_position(peer))

    return cons(symbol('compile/cond'), *clauses, nil)


# The end.
