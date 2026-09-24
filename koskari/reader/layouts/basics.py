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


from ...bootstrap import builtin
from ...exceptions import LayoutError
from ...lib import cons, nil, symbol
from ...types import (
    get_position, is_keyword, is_nil, is_pair, is_symbol, strip_mark, unpack)
from .helpers import _keyword_block, is_block, layout


_symbol_begin = symbol('begin')


@builtin('layout/block')
@layout(symbol('layout/block'))
def layout_block(reader, source, peers):
    """
    the default block layout handler. Folds each body entry and splices
    the forms onto the header.

    eg.
    foo x:
      bar

    becomes
    (foo x bar)

    A keyword names a call argument. A line that opens with one splices
    as the keyword followed by the fold of the rest of the line, and a
    sub-block whose header opens with one splices as the keyword followed
    by the block's value: a begin over the body for a bare keyword header,
    and the lowering of the block the rest of the header heads otherwise.

    eg.
    foo x:
      bar
      file: stderr
      sep: 1 2
      end::
        "eol"

    becomes
    (foo x bar file: stderr sep: (1 2) end: (begin "eol"))

    Reached as the handler of a keyword-headed block itself, it returns
    the pair of the keyword and the value, which is what any other layout
    folding such an entry receives.
    """

    header, body = source

    if peers:
        raise LayoutError(
            f"{header.car} layout does not take peers",
            get_position(source))

    if is_keyword(header.car):
        return cons(header.car, keyword_block_value(reader, header, body), nil)

    ret = header.copy()
    for entry in unpack(body):
        if is_block(entry):
            eheader, ebody = entry.cdr
            ret.append(eheader.car)
            ret.append(keyword_block_value(reader, eheader, ebody))
            continue
        head, rest = entry
        if is_keyword(head) and not is_nil(rest):
            ret.append(head)
            ret.append(reader.fold_line(rest))
        else:
            ret.append(reader.fold_line(entry))

    return ret


def keyword_block_value(reader, header, body):
    """
    The value form of a block whose header opens with a keyword

    A bare keyword header takes a begin over the body. Otherwise the
    header forms after the keyword head the value block, which is lowered
    through its own layout. A value head whose layout declares peers is
    refused: peers do not pair across a keyword header, so that block
    nests under the bare keyword header instead

    :param reader: the LayoutReader expanding the source
    :param header: (KEYWORD FORM ...) block header
    :param body: the block's entries
    :return: the value form
    :raises LayoutError: when the value head declares peers
    """

    from . import apply_layout, apply_lines

    key, rest = header
    position = get_position(header)

    if is_nil(rest):
        value = cons(_symbol_begin, reader.fold_lines(apply_lines(body, reader)))
        if position:
            value.set_position(position)
        return value

    head = strip_mark(rest.car)
    layout_obj = reader.find_layout(str(head)) if is_symbol(head) else None
    if layout_obj is not None and layout_obj.peers:
        raise LayoutError(
            f"{key}: value block headed by {head} cannot gather its"
            f" peers here; write {key}:: and nest the {head} block",
            position)

    block = cons(_keyword_block, rest, *unpack(body), nil)
    if position:
        block.set_position(position)
    return apply_layout(block, reader)


@builtin('layout/block-begin')
@layout(symbol('layout/block-begin'))
def layout_block_begin(reader, source, peers):
    """
    block layout handler that wraps the folded body in compile/begin

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

    header.append(cons(symbol('begin'), reader.fold_lines(body)))
    return header


@builtin('layout/inline-block')
@layout(symbol('layout/inline-block'))
def layout_inline_block(reader, source, peers):
    """
    inlining layout handler which splices the elements of every body
    line onto the header as to produce a single list, folding nothing.
    Used in cases where layout blocks are only meant to break up long
    lines, rather than to express sub-expressions. A sub-block whose
    header opens with a keyword splices as the keyword and its value,
    as under layout/block.

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
    for entry in unpack(body):
        if is_block(entry):
            eheader, ebody = entry.cdr
            ret.append(eheader.car)
            ret.append(keyword_block_value(reader, eheader, ebody))
        else:
            ret.splice(entry.copy())

    return ret


@builtin('layout/import-from')
@layout(symbol('layout/import-from'))
def layout_import_from(reader, source, peers):
    """
    Inlining layout handler for long import lists.

    eg.::

      import-from itertools:
        count cycle permutations product repeat chain
        combinations combinations_with_replacement
        groupby tee
    """

    return layout_inline_block.read(reader, source, peers)


_symbol_quote = symbol('quote')
_symbol_quasiquote = symbol('quasiquote')
_symbol_quote_syntax = symbol('quote-syntax')
_symbol_quasiquote_syntax = symbol('quasiquote-syntax')


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
    if not is_nil(rest) or not is_block(inner):
        raise LayoutError(
            f"{label} layout requires one inner block",
            get_position(source))

    return cons(compile_sym, inner, nil)


@builtin('layout/quote')
@layout(symbol('layout/quote'), raw=True)
def layout_quote(reader, source, peers):
    """
    Unbox a synthetic quote layout block to ``(compile/quote INNER-BLOCK)``

    INNER-BLOCK is the raw ``:block:`` from the quoted form, not layout-expanded
    """

    return _unbox_compile_layout(
        source, peers, _symbol_quote, 'quote')


@builtin('layout/quasiquote')
@layout(symbol('layout/quasiquote'), raw=True)
def layout_quasiquote(reader, source, peers):
    """
    Unbox a synthetic quasiquote layout block to
    ``(compile/quasiquote INNER-BLOCK)``

    INNER-BLOCK is the raw ``:block:`` from the quasiquoted form
    """

    return _unbox_compile_layout(
        source, peers, _symbol_quasiquote, 'quasiquote')


@builtin('layout/quote-syntax')
@layout(symbol('layout/quote-syntax'), raw=True)
def layout_quote_syntax(reader, source, peers):
    """
    Unbox a synthetic quote-syntax layout block, read from a ``#'``
    block, to ``(compile/quote-syntax INNER-BLOCK)``
    """

    return _unbox_compile_layout(
        source, peers, _symbol_quote_syntax, 'quote-syntax')


@builtin('layout/quasiquote-syntax')
@layout(symbol('layout/quasiquote-syntax'), raw=True)
def layout_quasiquote_syntax(reader, source, peers):
    """
    Unbox a synthetic quasiquote-syntax layout block, read from a ``#```
    block, to ``(compile/quasiquote-syntax INNER-BLOCK)``
    """

    return _unbox_compile_layout(
        source, peers, _symbol_quasiquote_syntax, 'quasiquote-syntax')


# The end.
