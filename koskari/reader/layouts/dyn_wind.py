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
from ...types import car, cdr, cons, get_position, nil, symbol, unpack
from .helpers import layout


_symbol_before = symbol('before')
_symbol_body = symbol('body')
_symbol_after = symbol('after')


@layout(
    symbol('layout/dyn-wind'),
    peers=(_symbol_before, _symbol_body, _symbol_after))
def layout_dyn_wind(source, peers):
    """
    Lower layout/dyn-wind block with before/body/after peers to
    (compile/dyn-wind [before] [body] [after]) for compilation.

    body is required; at least one of before or after is required.
    """

    before_form = nil
    body_form = nil
    after_form = nil

    for peer in peers:
        pheader, pbody = peer
        phead = car(pheader)
        ppargs = list(unpack(cdr(pheader)))
        if phead is _symbol_before:
            if ppargs:
                raise LayoutError(
                    'before block does not take arguments',
                    get_position(peer))
            before_form = cons(symbol('compile/begin'), pbody if pbody is not nil else nil)
        elif phead is _symbol_body:
            if ppargs:
                raise LayoutError(
                    'body block does not take arguments',
                    get_position(peer))
            body_form = cons(symbol('compile/begin'), pbody if pbody is not nil else nil)
        elif phead is _symbol_after:
            if ppargs:
                raise LayoutError(
                    'after block does not take arguments',
                    get_position(peer))
            after_form = cons(symbol('compile/begin'), pbody if pbody is not nil else nil)
        else:
            raise LayoutError(
                'invalid peer for dyn-wind block: %r' % (phead,),
                get_position(peer))

    if body_form is nil:
        raise LayoutError(
            'dyn-wind requires a body block',
            get_position(source))
    if before_form is nil and after_form is nil:
        raise LayoutError(
            'dyn-wind requires at least one of before or after block',
            get_position(source))

    return cons(
        symbol('compile/dyn-wind'),
        cons(before_form, cons(body_form, cons(after_form, nil))))


# The end.
