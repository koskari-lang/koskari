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
from ...types import get_position, unpack, is_pair
from .helpers import layout


_symbol_case = symbol('case')
_symbol_elif = symbol('elif')
_symbol_else = symbol('else')
_keyword_else = keyword('else')


@layout(symbol('layout/if'), peers=(_symbol_elif, _symbol_else))
def layout_if(source, peers):
    """
    Lower if/elif/else block sources to compile/cond for compilation.

    eg.
    if x:
      (print "x")
    elif y:
      (print "y")
    else:
      (print "other")

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


@layout(symbol('layout/switch'))
def layout_switch(source, peers):
    """
    Lower a switch block source into a flat switch form for compilation

    eg.
    switch (get-value):
      case 1:
        (print "one")
      case 2:
        (print "two")
      case 3:
        (print "three")
      else:
        (print "other")

    The inner case and else children are read by the default layout/block,
    which produces forms shaped (case COMPARE (compile/begin BODY...)) and
    (else (compile/begin BODY...)). This handler walks the switch body,
    validates each child shape, and emits

        (switch VALUE
          (COMPARE  (compile/begin BODY...))
          ...
          (else: (compile/begin BODY...)))

    :param source: ((switch VALUE) . BODY) pair
    :param peers: must be empty
    """

    if peers:
        raise LayoutError(
            'switch does not take peers',
            get_position(source))

    header, body = source
    header_parts = list(unpack(header))
    pargs = header_parts[1:]

    if len(pargs) != 1:
        raise LayoutError(
            'switch block requires exactly one value expression',
            get_position(source))

    value = pargs[0]
    clauses = []
    saw_else = False

    for form in unpack(body):
        if not is_pair(form):
            raise LayoutError(
                'switch body may only contain case or else clauses',
                get_position(form) or get_position(source))

        head = form.car
        rest_parts = list(unpack(form.cdr))

        if head is _symbol_case:
            if saw_else:
                raise LayoutError(
                    'case cannot follow else',
                    get_position(form))
            if len(rest_parts) != 2:
                raise LayoutError(
                    'case requires exactly one condition',
                    get_position(form))
            clauses.append(form.cdr)

        elif head is _symbol_else:
            if saw_else:
                raise LayoutError(
                    'multiple else clauses are not allowed',
                    get_position(form))
            if len(rest_parts) != 1:
                raise LayoutError(
                    'else block does not take arguments',
                    get_position(form))
            clauses.append(cons(_keyword_else, rest_parts[0], nil))
            saw_else = True

        else:
            raise LayoutError(
                'switch body may only contain case or else clauses, '
                'got %r' % (head,),
                get_position(form))

    return cons(symbol('compile/switch'), value, *clauses, nil)


# The end.
