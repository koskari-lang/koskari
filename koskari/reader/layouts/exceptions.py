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
koskari.reader.layouts.exceptions

Layout handlers for try/except/else/finally block syntax.
"""

from ...compile.helpers import helper_using
from ...exceptions import LayoutError
from ...lib import cons, keyword, nil, symbol
from ...bootstrap import builtin
from ...types import fill_position, get_position, is_pair, unpack
from .helpers import layout


_symbol_except = symbol('except')
_symbol_else = symbol('else')
_symbol_finally = symbol('finally')
_keyword_except = keyword('except')
_keyword_else = keyword('else')
_keyword_finally = keyword('finally')


@builtin('layout/try')
@layout(symbol('layout/try'), peers=(_symbol_except, _symbol_else, _symbol_finally))
def layout_try(ns, source, peers):
    """
    Lower try/except/else/finally block sources to (try ...) for compilation.

    :param source: ((try) . BODY) pair
    :param peers: list of peer (header . body) pairs
    """

    header, body = source

    has_except = False
    act_else = None
    act_finally = None
    except_clauses = []

    for peer in peers:
        pheader, pbody = peer
        phead, prest = pheader
        ppargs = list(unpack(prest))

        if phead is _symbol_except:
            has_except = True
            if not ppargs:
                clause = cons(_keyword_except, pbody)
                fill_position(clause, get_position(peer))
                except_clauses.append(clause)
            else:
                if len(ppargs) != 1:
                    raise LayoutError(
                        'except requires exactly one bracketed USE form, '
                        'e.g. [Exception] or [Exception as: e]',
                        get_position(peer))
                if not is_pair(ppargs[0]):
                    raise LayoutError(
                        'except requires a bracketed USE form, '
                        'e.g. [Exception] or [Exception as: e], not %r'
                        % ppargs[0],
                        get_position(peer))
                try:
                    helper_using(prest.car)
                except Exception as e:
                    raise LayoutError(
                        'invalid except USE form: %s' % e,
                        get_position(peer))
                clause = cons(prest.car, pbody)
                fill_position(clause, get_position(peer))
                except_clauses.append(clause)

        elif phead is _symbol_else:
            if act_else is not None:
                raise LayoutError(
                    'multiple else peers are not allowed',
                    get_position(peer))
            if ppargs:
                raise LayoutError(
                    'else block does not take arguments',
                    get_position(peer))
            act_else = pbody

        elif phead is _symbol_finally:
            if act_finally is not None:
                raise LayoutError(
                    'multiple finally peers are not allowed',
                    get_position(peer))
            if ppargs:
                raise LayoutError(
                    'finally block does not take arguments',
                    get_position(peer))
            act_finally = pbody

        else:
            raise LayoutError(
                'invalid peer for try block: %r' % (phead,),
                get_position(peer))

    if not has_except and act_finally is None:
        raise LayoutError(
            'try block requires at least one except or one finally',
            get_position(source))

    try_body = cons(symbol('begin'), body)
    catches = []
    for clause in except_clauses:
        catches.append(clause)
    if act_else is not None:
        catches.append(cons(_keyword_else, act_else))
    if act_finally is not None:
        catches.append(cons(_keyword_finally, act_finally))

    catches_list = cons(*catches, nil) if catches else nil

    result = cons(symbol('try'), try_body, catches_list)
    if get_position(source):
        fill_position(result, get_position(source))
    return result


# The end.
