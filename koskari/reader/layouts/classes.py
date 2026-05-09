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


from ...exceptions import LayoutError
from ...lib import cons, nil, symbol
from ...types import get_position, unpack
from .helpers import layout


@layout(symbol('layout/class'))
def layout_class(source, peers):
    """
    Lower class block to (compile/class NAME SUPERS (compile/begin BODY)).
    SUPERS may be omitted; when missing, use nil.
    """

    if peers:
        raise LayoutError(
            'class block does not take peers',
            get_position(source))

    header, body = source
    header_parts = list(unpack(header))

    if len(header_parts) < 2:
        raise LayoutError(
            'class block requires a name',
            get_position(source))

    if len(header_parts) > 3:
        raise LayoutError(
            'class block takes at most name and supers',
            get_position(source))

    name = header_parts[1]
    supers = nil if len(header_parts) == 2 else header_parts[2]

    return cons(
        symbol('compile/class'),
        name,
        supers,
        cons(symbol('compile/begin'), body),
        nil)


@layout(symbol('layout/define-class'))
def layout_define_class(source, peers):
    """
    Lower define-class block to (compile/define-class NAME SUPERS (compile/begin BODY)).
    SUPERS may be omitted; when missing, use nil.
    """

    if peers:
        raise LayoutError(
            'define-class block does not take peers',
            get_position(source))

    header, body = source
    header_parts = list(unpack(header))

    if len(header_parts) < 2:
        raise LayoutError(
            'define-class block requires a name',
            get_position(source))

    if len(header_parts) > 3:
        raise LayoutError(
            'define-class block takes at most name and supers',
            get_position(source))

    name = header_parts[1]
    supers = nil if len(header_parts) == 2 else header_parts[2]

    return cons(
        symbol('compile/define-class'),
        name,
        supers,
        cons(symbol('compile/begin'), body),
        nil)


# The end.
