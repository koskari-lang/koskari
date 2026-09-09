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


from ...bootstrap import builtin
from ...exceptions import LayoutError
from ...lib import cons, keyword, nil, symbol
from ...types import fill_position, get_position, is_nil, is_pair, unpack
from .helpers import block_source_from_cons, layout, match_block_source


_symbol_case = symbol('case')
_symbol_elif = symbol('elif')
_symbol_else = symbol('else')
_symbol_when = symbol('when')
_symbol_unless = symbol('unless')
_keyword_else = keyword('else')
_keyword_when = keyword('when')
_keyword_unless = keyword('unless')


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
            clause = cons(ppargs[0], pbody)
            fill_position(clause, get_position(peer))
            clauses.append(clause)

        elif phead is _symbol_else:
            if saw_else:
                raise LayoutError(
                    'multiple else peers are not allowed',
                    get_position(peer))
            if ppargs:
                raise LayoutError(
                    'else block does not take arguments',
                    get_position(peer))
            clause = cons(_keyword_else, pbody)
            fill_position(clause, get_position(peer))
            clauses.append(clause)
            saw_else = True

        else:
            raise LayoutError(
                'invalid peer for if block: %r' % (phead,),
                get_position(peer))

    return cons(symbol('cond'), *clauses, nil)


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

    return cons(symbol('switch'), value, *clauses, nil)


def _split_case_guard(cargs, child):
    """
    Split a case header tail on a when / unless marker. The tokens
    before the marker are the pattern; exactly one form after it is the
    guard expression. Returns (pattern_list, guard_keyword, guard_form),
    with the latter two None when the header carries no guard.

    :param cargs: case header tail spine
    :param child: the case block source, for error positions
    """

    pattern_parts = []
    walk = cargs
    while walk:
        element, walk = walk
        if element is _symbol_when or element is _symbol_unless:
            gkw = (_keyword_when if element is _symbol_when
                   else _keyword_unless)
            if not pattern_parts:
                raise LayoutError(
                    f'case requires a pattern before {element}',
                    get_position(child))
            gparts = list(unpack(walk))
            if len(gparts) != 1:
                raise LayoutError(
                    f'{element} guard requires exactly one expression',
                    get_position(child))
            return cons(*pattern_parts, nil), gkw, gparts[0]
        pattern_parts.append(element)
    return cargs, None, None


@builtin('layout/match')
@layout(symbol('layout/match'), raw=True)
def layout_match(ns, source, peers):
    """
    Lower a match block source into a flat match form for compilation

    eg.
    match (get-value):
      case #[a b c]:
        (use a b c)
      case x :: int:
        (use x)
      case Point[x y]:
        (use x y)
      else:
        (other)

    A raw layout: the case and else children arrive as block sources, so
    each case header (however many tokens) is structurally separate from
    its body. The whole header tail becomes the clause's pattern
    delimiter, giving

        (compile/match VALUE
          [[PATTERN...] BODY...]
          ...
          [else: BODY...])

    which is the same clause shape an s-expr author writes directly. A
    ``when`` / ``unless`` marker in a case header splits it into pattern
    and guard: ``case PATTERN when COND:`` emits
    ``[[PATTERN] when: COND BODY...]``.

    :param source: ((match VALUE) . BODY) pair with raw block children
    :param peers: must be empty
    """

    from . import apply_layouts

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

    for child in unpack(body):
        child = block_source_from_cons(child) or child
        matched = match_block_source(child)
        if matched is None:
            raise LayoutError(
                'match body may only contain case or else blocks',
                get_position(child) or get_position(source))

        chead, cargs = matched['header'].datum
        cbody = apply_layouts(matched['body'].datum, ns)

        if chead is _symbol_case:
            if saw_else:
                raise LayoutError(
                    'case cannot follow else',
                    get_position(child))
            if is_nil(cargs):
                raise LayoutError(
                    'case requires a pattern',
                    get_position(child))
            if is_nil(cbody):
                raise LayoutError(
                    'case requires a body',
                    get_position(child))
            pattern, gkw, guard = _split_case_guard(cargs, child)
            if gkw is not None:
                clauses.append(
                    cons(pattern, gkw, guard, *unpack(cbody), nil))
            else:
                clauses.append(cons(pattern, *unpack(cbody), nil))

        elif chead is _symbol_else:
            if saw_else:
                raise LayoutError(
                    'multiple else clauses are not allowed',
                    get_position(child))
            if not is_nil(cargs):
                raise LayoutError(
                    'else block does not take arguments',
                    get_position(child))
            if is_nil(cbody):
                raise LayoutError(
                    'else block requires a body',
                    get_position(child))
            clauses.append(cons(_keyword_else, *unpack(cbody), nil))
            saw_else = True

        else:
            raise LayoutError(
                f'match body may only contain case or else blocks, '
                f'got {chead!r}',
                get_position(child))

    return cons(symbol('match'), value, *clauses, nil)


# The end.
