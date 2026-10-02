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
koskari.reader.layouts.loops

Layout handlers for for-each and while blocks.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

from ...bootstrap import builtin
from ...exceptions import LayoutError
from ...lib import cons, keyword, symbol
from ...types import fill_position, get_position
from .helpers import layout


_symbol_else = symbol('else')
_keyword_else = keyword('else')


def _layout_loop(reader, source, peers):
    header, body = source
    header.extend(reader.fold_lines(body))

    if peers:
        if len(peers) != 1:
            raise LayoutError(
                "loop blocks can only have one else peer",
                get_position(source))

        peer = peers[0]
        pheader, pbody = peer
        _phead, pargs = pheader

        if pargs:
            raise LayoutError(
                "else block does not take arguments",
                get_position(peer))

        clause = cons(_keyword_else, reader.fold_lines(pbody))
        fill_position(clause, get_position(peer))
        header.append(clause)

    return header


@builtin("layout/for-each")
@layout(symbol("layout/for-each"), peers=(_symbol_else,))
def layout_for_each(reader, source, peers):
    """
    Layout handler for for-each blocks

    eg.::

      for-each [x (range 0 3)]:
        (print x)
      else:
        (print "done")
    """

    return _layout_loop(reader, source, peers)


@builtin("layout/while")
@layout(symbol("layout/while"), peers=(_symbol_else,))
def layout_while(reader, source, peers):
    """
    Layout handler for while blocks

    eg.::

      while (< x 3):
        (print x)
        set! x (+ x 1)
      else:
        (print "done")
    """

    return _layout_loop(reader, source, peers)


# The end.
