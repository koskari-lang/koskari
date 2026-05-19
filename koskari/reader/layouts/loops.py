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
koskari.reader.layouts.helpers

Layout decorator for reader block syntax.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

from ...bootstrap import add_builtin
from ...lib import cons, keyword, symbol
from ...exceptions import LayoutError
from ...types import get_position
from .helpers import layout


_symbol_else = symbol('else')
_keyword_else = keyword('else')


@layout(symbol("layout/loop"), peers=(_symbol_else,))
def layout_loop(source, peers):
    """
    Layout handler for loop blocks.
    """

    header, body = source
    header.extend(body)

    if peers:
        if len(peers) != 1:
            raise LayoutError(
                "loop blocks can only have one else peer",
                get_position(source))

        peer = peers[0]
        pheader, pbody = peer
        phead, pargs = pheader

        if phead != _symbol_else:
            raise LayoutError(
                "loop blocks can only have an else peer",
                get_position(peers))

        if pargs:
            raise LayoutError(
                "else block does not take arguments",
                get_position(peers))

        else_body = pbody
        header.append(cons(_keyword_else, else_body))

    return header


add_builtin("layout/for-each", layout_loop)
add_builtin("layout/while", layout_loop)


# The end.
