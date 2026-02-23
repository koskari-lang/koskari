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


from ...types import cons, keyword, nil, symbol, unpack
from .helpers import layout


_symbol_layout_if = symbol('layout/if')
_symbol_if = symbol('if')
_symbol_elif = symbol('elif')
_symbol_else = symbol('else')
_keyword_else = keyword('else')


@layout(_symbol_if, peers=(_symbol_elif, _symbol_else))
def layout_if(reader, source, peers):
    """
    Build a layout/if form from if/elif/else block sources.

    :param reader: active LayoutReader
    :param source: (if COND BODY)
    :param peers: list of peer sources (elif/else blocks)
    """

    parts = list(unpack(source))
    args = parts[1:-1]
    body = parts[-1]

    if len(args) != 1:
        raise reader._layout_stream.error('if block requires exactly one condition')

    clauses = [cons(args[0], body, nil)]
    saw_else = False

    for peer in peers:
        pparts = list(unpack(peer))
        phead = pparts[0]
        pargs = pparts[1:-1]
        pbody = pparts[-1]

        if phead is _symbol_elif:
            if saw_else:
                raise reader._layout_stream.error('elif cannot follow else')
            if len(pargs) != 1:
                raise reader._layout_stream.error('elif block requires exactly one condition')
            clauses.append(cons(pargs[0], pbody, nil))

        elif phead is _symbol_else:
            if saw_else:
                raise reader._layout_stream.error('multiple else peers are not allowed')
            if pargs:
                raise reader._layout_stream.error('else block does not take arguments')
            clauses.append(cons(_keyword_else, pbody, nil))
            saw_else = True

        else:
            raise reader._layout_stream.error('invalid peer for if block: %r' % (phead,))

    return cons(_symbol_layout_if, *clauses, nil)


# The end.
