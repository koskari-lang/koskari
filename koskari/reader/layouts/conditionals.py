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


from ...types import car, cdr, cons, keyword, nil, symbol, unpack
from .helpers import layout


_symbol_elif = symbol('elif')
_symbol_else = symbol('else')
_keyword_else = keyword('else')


@layout(symbol('layout/block'))
def layout_block(reader, source, peers):
    """
    the default block layout handler

    eg.
    layout/block foo x:
      bar

    becomes
    (foo x (compile/begin bar))
    """

    if peers:
        raise reader._layout_stream.error('block does not take peers')

    header, body = source
    _called_by, hdr = header

    hdr.append(cons(symbol('compile/begin'), body))
    return hdr


@layout(symbol('layout/begin'))
def layout_begin(reader, source, peers):
    """
    lowers into a compile/begin

    eg.
    layout/begin:
      (bar)

    becomes
    (compile/begin bar)
    """

    if peers:
        raise reader._layout_stream.error('block does not take peers')

    called_by, exprs = source
    return cons(symbol('compile/begin'), exprs)


@layout(symbol('layout/if'), peers=(_symbol_elif, _symbol_else))
def layout_if(reader, source, peers):
    """
    Lower if/elif/else block sources to compile/cond for compilation.

    :param reader: active LayoutReader
    :param source: ((if COND) . BODY) pair
    :param peers: list of peer (header . body) pairs
    """

    header, body = source
    header_parts = list(unpack(header))
    pargs = header_parts[1:]

    if len(pargs) != 1:
        raise reader._layout_stream.error('if block requires exactly one condition')

    clauses = [cons(pargs[0], body)]
    saw_else = False

    for peer in peers:
        pheader, pbody = car(peer), cdr(peer)
        phead = car(pheader)
        ppargs = list(unpack(cdr(pheader)))

        if phead is _symbol_elif:
            if saw_else:
                raise reader._layout_stream.error('elif cannot follow else')
            if len(ppargs) != 1:
                raise reader._layout_stream.error('elif block requires exactly one condition')
            clauses.append(cons(ppargs[0], pbody))

        elif phead is _symbol_else:
            if saw_else:
                raise reader._layout_stream.error('multiple else peers are not allowed')
            if ppargs:
                raise reader._layout_stream.error('else block does not take arguments')
            clauses.append(cons(_keyword_else, pbody))
            saw_else = True

        else:
            raise reader._layout_stream.error('invalid peer for if block: %r' % (phead,))

    return cons(symbol('compile/cond'), *clauses, nil)


# The end.
