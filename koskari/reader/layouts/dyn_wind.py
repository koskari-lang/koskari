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
koskari.reader.layouts.dyn_wind

Layout handler for layout/dyn-wind block syntax.
"""

from ...exceptions import LayoutError
from ...lib import cons, keyword, nil, symbol
from ...bootstrap import builtin
from ...types import is_nil, get_position
from .helpers import layout


_symbol_before = symbol('before')
_symbol_after = symbol('after')
_symbol_begin = symbol('begin')


@builtin('layout/dyn-wind')
@layout(symbol('layout/dyn-wind'), peers=(_symbol_before, _symbol_after))
def layout_dyn_wind(ns, source, peers):
    """
    Lower layout/dyn-wind block with before/body/after peers to
    (compile/dyn-wind [before] [body] [after]) for compilation.

    body is required; at least one of before or after is required.

    Usage:

    ```
    dyn-wind:
      body-exprs ...
    before:
      before-exprs ...
    after:
      after-exprs ...
    ```
    """

    (blk, (head, (body, _rest))) = source
    if blk is not keyword("block") or _rest:
        raise LayoutError(
            'malformed dyn-wind block',
            get_position(source))

    before_form = nil
    after_form = nil

    _called_by, hrest = head
    if hrest:
        raise LayoutError(
            'dyn-wind block header does not accept arguments',
            get_position(head))

    for peer in peers:
        pheader, pbody = peer
        phead, ppargs = pheader

        if phead is _symbol_before:
            if not is_nil(before_form):
                raise LayoutError(
                    'before block appears twice',
                    get_position(peer))
            if ppargs:
                raise LayoutError(
                    'before block does not take arguments',
                    get_position(peer))
            before_form = pbody and cons(_symbol_begin, pbody)

        elif phead is _symbol_after:
            if not is_nil(after_form):
                raise LayoutError(
                    'after block appears twice',
                    get_position(peer))
            if ppargs:
                raise LayoutError(
                    'after block does not take arguments',
                    get_position(peer))
            after_form = pbody and cons(_symbol_begin, pbody)

    return cons(
        symbol('compile/dyn-wind'),
        before_form,
        cons(_symbol_begin, body),
        after_form,
        nil)


# The end.
