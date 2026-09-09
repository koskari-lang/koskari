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
koskari.reader.layouts.basics

Layout handlers for basic block syntax.
"""


from ...bootstrap import add_builtin, builtin
from ...exceptions import LayoutError
from ...lib import cons, nil, symbol
from ...types import get_position, is_keyword, is_nil, is_pair
from .helpers import layout


@builtin('layout/block')
@layout(symbol('layout/block'))
def layout_block(ns, source, peers):
    """
    the default block layout handler. Simply splices the applied layout of
    its body onto the header.

    eg.
    foo x:
      bar

    becomes
    (foo x bar)
    """

    header, body = source

    if peers:
        raise LayoutError(
            f"{header.car} layout does not take peers",
            get_position(source))

    header.extend(body)
    return header


@builtin('layout/block-begin')
@layout(symbol('layout/block-begin'))
def layout_block_begin(ns, source, peers):
    """
    block layout handler that wraps the body in compile/begin

    eg.
    foo x:
      bar

    becomes
    (foo x (compile/begin bar))
    """

    header, body = source

    if peers:
        raise LayoutError(
            f"{header.car} layout does not take peers",
            get_position(source))

    header.append(cons(symbol('begin'), body))
    return header


@builtin('layout/keyword-block')
@layout(symbol('layout/keyword-block'))
def layout_keyword_block(ns, source, peers):
    """
    keyword block layout handler which splices indented body onto the header as
    to produce a single list. Lines starting with keywords are inlined directly
    rather than being implicit calls.
    """

    header, body = source
    called_by = header.car

    if peers:
        raise LayoutError(
            f"{called_by} layout does not take peers",
            get_position(source))

    corr = body = body.copy()
    while corr:
        head, next = corr
        if is_pair(head) and is_keyword(head.car):
            tail = head.cdr
            if is_pair(tail) and is_nil(tail.cdr):
                val = tail.car
            else:
                val = tail
            corr.car = head.car
            corr.cdr = cons(val, next)
        corr = next

    ret = header.copy()
    ret.splice(body)

    return ret


layout_call = layout_keyword_block
add_builtin('layout/call', layout_call)


@builtin('layout/inline-block')
@layout(symbol('layout/inline-block'))
def layout_inline_block(ns, source, peers):
    """
    inlining layout handler which splices indented body onto the header as
    to produce a single list, without applying implicit calls to the
    individual lines. Used in cases where layout blocks are only meant
    to break up long lines, rather than to express sub-expressions.

    eg.
    X foo:
      bar (baz)
      qux
      alpha: beta

    becomes
    (X foo bar (baz) qux alpha: beta)
    """

    header, body = source
    called_by = header.car

    if peers:
        raise LayoutError(
            f"{called_by} layout does not take peers",
            get_position(source))

    ret = header.copy()

    elem, rest = body
    while elem:
        if is_pair(elem):
            ret.splice(elem)
        else:
            ret.append(elem)
        if is_nil(rest):
            break
        elem, rest = rest

    return ret


@builtin('layout/import-from')
@layout(symbol('layout/import-from'))
def layout_import_from(ns, source, peers):
    """
    Inlining layout handler for long import lists.

    eg.
    import-from itertools:
      count cycle permutations product repeat chain
      combinations combinations_with_replacement
      groupby tee
    """

    return layout_inline_block.read(ns, source, peers)


_symbol_quote = symbol('quote')
_symbol_quasiquote = symbol('quasiquote')


def _unbox_compile_layout(source, peers, compile_sym, label):
    if peers:
        raise LayoutError(
            f"{label} layout does not take peers",
            get_position(source))

    _header, body = source

    if is_nil(body) or not is_pair(body):
        raise LayoutError(
            f"{label} layout requires one inner block",
            get_position(source))

    inner, rest = body
    if not is_nil(rest):
        raise LayoutError(
            f"{label} layout requires one inner block",
            get_position(source))

    return cons(compile_sym, inner, nil)


@builtin('layout/quote')
@layout(symbol('layout/quote'), raw=True)
def layout_quote(ns, source, peers):
    """
    Unbox a synthetic quote layout block to ``(compile/quote INNER-BLOCK)``

    INNER-BLOCK is the raw ``:block:`` from the quoted form, not layout-expanded
    """

    return _unbox_compile_layout(
        source, peers, _symbol_quote, 'quote')


@builtin('layout/quasiquote')
@layout(symbol('layout/quasiquote'), raw=True)
def layout_quasiquote(ns, source, peers):
    """
    Unbox a synthetic quasiquote layout block to
    ``(compile/quasiquote INNER-BLOCK)``

    INNER-BLOCK is the raw ``:block:`` from the quasiquoted form
    """

    return _unbox_compile_layout(
        source, peers, _symbol_quasiquote, 'quasiquote')


# The end.
