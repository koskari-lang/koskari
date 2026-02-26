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


from ...exceptions import LayoutError
from ...compile.helpers import helper_binding
from ...types import car, cdr, cons, keyword, nil, symbol, unpack
from .helpers import layout


_symbol_except = symbol('except')
_symbol_else = symbol('else')
_symbol_finally = symbol('finally')
_keyword_else = keyword('else')
_keyword_finally = keyword('finally')


@layout(symbol('layout/try'), peers=(_symbol_except, _symbol_else, _symbol_finally))
def layout_try(reader, source, peers):
    """
    Lower try/except/else/finally block sources to (try ...) for compilation.

    :param reader: active LayoutReader
    :param source: ((try) . BODY) pair
    :param peers: list of peer (header . body) pairs
    """

    header, body = source
    stream = reader._layout_stream

    has_except = False
    act_else = None
    act_finally = None
    except_clauses = []

    for peer in peers:
        pheader, pbody = peer
        phead = car(pheader)
        ppargs = list(unpack(cdr(pheader)))

        if phead is _symbol_except:
            has_except = True
            if not ppargs:
                raise LayoutError(
                    'except block requires an exception type',
                    stream.position(),
                    filename=getattr(stream, 'filename', None))
            try:
                bind_source = cons(ppargs[0], nil)
                if len(ppargs) > 1:
                    bind_source = cons(ppargs[0], cons(ppargs[1], nil) if len(ppargs) == 2 else cons(*ppargs, nil))
                binding, ex_type = helper_binding(bind_source, allow_unnamed=True)
            except Exception as e:
                raise LayoutError(
                    'invalid except binding: %s' % e,
                    stream.position(),
                    filename=getattr(stream, 'filename', None))
            if binding is not None:
                except_clauses.append(cons(cons(binding, ex_type), pbody))
            else:
                except_clauses.append(cons(cons(keyword('except'), ex_type), pbody))

        elif phead is _symbol_else:
            if act_else is not None:
                raise LayoutError(
                    'multiple else peers are not allowed',
                    stream.position(),
                    filename=getattr(stream, 'filename', None))
            if ppargs:
                raise LayoutError(
                    'else block does not take arguments',
                    stream.position(),
                    filename=getattr(stream, 'filename', None))
            act_else = pbody

        elif phead is _symbol_finally:
            if act_finally is not None:
                raise LayoutError(
                    'multiple finally peers are not allowed',
                    stream.position(),
                    filename=getattr(stream, 'filename', None))
            if ppargs:
                raise LayoutError(
                    'finally block does not take arguments',
                    stream.position(),
                    filename=getattr(stream, 'filename', None))
            act_finally = pbody

        else:
            raise LayoutError(
                'invalid peer for try block: %r' % (phead,),
                stream.position(),
                filename=getattr(stream, 'filename', None))

    if not has_except and act_finally is None:
        raise LayoutError(
            'try block requires at least one except or one finally',
            stream.position(),
            filename=getattr(stream, 'filename', None))

    try_body = cons(symbol('compile/begin'), body) if body is not nil else cons(symbol('compile/begin'), nil)
    catches = []
    for clause in except_clauses:
        catches.append(clause)
    if act_else is not None:
        catches.append(cons(_keyword_else, act_else))
    if act_finally is not None:
        catches.append(cons(_keyword_finally, act_finally))

    catches_list = cons(*catches, nil) if catches else nil
    return cons(symbol('try'), cons(try_body, catches_list))


# The end.
