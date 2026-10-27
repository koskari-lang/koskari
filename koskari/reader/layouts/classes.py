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
koskari.reader.layouts.classes

Layout handlers for class block syntax.
"""


from ...bootstrap import builtin
from ...exceptions import LayoutError
from ...lib import cons, nil, symbol
from ...types import get_position, unpack
from .helpers import layout


def _class_block(reader, source, peers, head):
    """
    Lower a class-shaped block to (HEAD NAME SUPERS (begin BODY)).
    SUPERS may be omitted; when missing, use nil.
    """

    if peers:
        raise LayoutError(
            f'{head} block does not take peers',
            get_position(source))

    header, body = source
    header_parts = list(unpack(header))

    if len(header_parts) < 2:
        raise LayoutError(
            f'{head} block requires a name',
            get_position(source))

    if len(header_parts) > 3:
        raise LayoutError(
            f'{head} block takes at most name and supers',
            get_position(source))

    name = header_parts[1]
    supers = nil if len(header_parts) == 2 else header_parts[2]

    return cons(
        symbol(head),
        name,
        supers,
        cons(symbol('begin'), reader.fold_lines(body)),
        nil)


@builtin('layout/class')
@layout(symbol('layout/class'))
def layout_class(reader, source, peers):
    """
    Lower class block to (class NAME SUPERS (begin BODY)).
    SUPERS may be omitted; when missing, use nil.
    """

    return _class_block(reader, source, peers, 'class')


@builtin('layout/build-class')
@layout(symbol('layout/build-class'))
def layout_build_class(reader, source, peers):
    """
    Lower build-class block to (build-class NAME SUPERS (begin BODY)).
    SUPERS may be omitted; when missing, use nil.
    """

    return _class_block(reader, source, peers, 'build-class')


# The end.
