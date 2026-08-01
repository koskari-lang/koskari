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
from ...lib import cons, keyword, nil, symbol
from ...bootstrap import builtin
from ...types import get_position, unpack, is_pair
from .helpers import layout


_symbol_case = symbol('case')
_symbol_elif = symbol('elif')
_symbol_else = symbol('else')
_keyword_else = keyword('else')


@builtin('layout/if')
@layout(symbol('layout/if'), peers=(_symbol_elif, _symbol_else))
def layout_if(ns, source, peers):
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
        phead = pheader.car
        ppargs = list(unpack(pheader.cdr))

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


@builtin('layout/switch')
@layout(symbol('layout/switch'))
def layout_switch(ns, source, peers):
    """
    Lower a switch block source into a flat switch form for compilation

    eg.
    switch (get-value):
      case [1]:
        (print "one")
      case [2]:
        (print "two")
      case [3 5]:
        (print "three")
      else:
        (print "other")

    The inner case and else children are read by the default layout/block,
    which splices body forms after the header. This handler walks the switch
    body, validates each child shape, and emits

        (switch VALUE
          ([COMP...]  BODY...)
          ...
          (else: BODY...))

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
            if len(rest_parts) < 2:
                raise LayoutError(
                    'case requires condition list and body',
                    get_position(form))
            if not is_pair(rest_parts[0]):
                raise LayoutError(
                    'case condition must be a bracketed list of values',
                    get_position(form))
            clauses.append(cons(*rest_parts, nil))

        elif head is _symbol_else:
            if saw_else:
                raise LayoutError(
                    'multiple else clauses are not allowed',
                    get_position(form))
            if not rest_parts:
                raise LayoutError(
                    'else block requires a body',
                    get_position(form))
            clauses.append(cons(_keyword_else, *rest_parts, nil))
            saw_else = True

        else:
            raise LayoutError(
                'switch body may only contain case or else clauses, '
                'got %r' % (head,),
                get_position(form))

    return cons(symbol('compile/switch'), value, *clauses, nil)


@builtin('layout/match')
@layout(symbol('layout/match'))
def layout_match(ns, source, peers):
    """
    Lower a match block source into a flat match form for compilation

    eg.
    match (get-value):
      case [a b c]:
        (use a b c)
      case (x :: int):
        (use x)
      case 5:
        (small)
      else:
        (other)

    The inner case and else children are read by the default layout/block,
    which splices body forms after the header. Each case takes a single
    pattern form (use a [ ... ] list for a sequence pattern); this handler
    walks the match body, validates each child shape, and emits

        (compile/match VALUE
          (PATTERN BODY...)
          ...
          (else: BODY...))

    :param source: ((match VALUE) . BODY) pair
    :param peers: must be empty
    """

    if peers:
        raise LayoutError(
            'match does not take peers',
            get_position(source))

    header, body = source
    header_parts = list(unpack(header))
    pargs = header_parts[1:]

    if len(pargs) != 1:
        raise LayoutError(
            'match block requires exactly one value expression',
            get_position(source))

    value = pargs[0]
    clauses = []
    saw_else = False

    for form in unpack(body):
        if not is_pair(form):
            raise LayoutError(
                'match body may only contain case or else clauses',
                get_position(form) or get_position(source))

        head = form.car
        rest_parts = list(unpack(form.cdr))

        if head is _symbol_case:
            if saw_else:
                raise LayoutError(
                    'case cannot follow else',
                    get_position(form))
            if len(rest_parts) < 2:
                raise LayoutError(
                    'case requires a pattern and body',
                    get_position(form))
            clauses.append(cons(*rest_parts, nil))

        elif head is _symbol_else:
            if saw_else:
                raise LayoutError(
                    'multiple else clauses are not allowed',
                    get_position(form))
            if not rest_parts:
                raise LayoutError(
                    'else block requires a body',
                    get_position(form))
            clauses.append(cons(_keyword_else, *rest_parts, nil))
            saw_else = True

        else:
            raise LayoutError(
                'match body may only contain case or else clauses, '
                'got %r' % (head,),
                get_position(form))

    return cons(symbol('compile/match'), value, *clauses, nil)


# The end.
