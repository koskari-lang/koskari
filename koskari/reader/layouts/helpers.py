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
koskari.reader.layouts.helpers

Layout decorator for reader block syntax and block-form matchers.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from ...lib import keyword
from ...matcher import BIND, LIST, LITERAL_KW, MATCH, REST, match1
from ...types import is_pair, is_symbol


_keyword_block = keyword('#block')


def is_block(form):
    """
    True when form is a ``:#block:`` cons
    """

    return form and is_pair(form) and form.car is _keyword_block


_BLOCK_SOURCE_PATTERN = MATCH(LIST(BIND('header'), REST('body')))

_BLOCK_CONS_PATTERN = MATCH(
    LIST(LITERAL_KW('#block'), BIND('header'), REST('body')))


def _valid_block_header(header):
    """
    True when header is a block head list whose car is a symbol
    """

    return is_pair(header) and is_symbol(header.car)


def match_block_source(form):
    """
    Match a pass-one block source ``((head . args) . entries...)``

    :param form: candidate form from a block body read
    :return: match result with ``header`` and ``body`` slots, or None
    """

    matched = match1(form, [_BLOCK_SOURCE_PATTERN])
    if matched is None:
        return None
    header = matched['header'].datum
    if not _valid_block_header(header):
        return None
    return matched


def match_block_cons(form):
    """
    Match a ``:#block:`` cons ``(:#block: (head . args) entry...)``

    :param form: candidate runtime or quote block form
    :return: match result with ``header`` and ``body`` slots, or None
    """

    matched = match1(form, [_BLOCK_CONS_PATTERN])
    if matched is None:
        return None
    header = matched['header'].datum
    if not _valid_block_header(header):
        return None
    return matched


def block_source_from_cons(form):
    """
    The pass-one block source of a ``:#block:`` cons, which is its cdr

    :param form: ``(:#block: (head . args) entry...)`` cons
    :return: ``((head . args) . entries...)`` or None
    """

    if match_block_cons(form) is None:
        return None
    return form.cdr


class LayoutHandler:
    """
    Wrapper for layout handlers with optional peer metadata.

    A handler is called as read_fn(reader, source, peers), where reader is
    the LayoutReader expanding the source. The source is the block cons's
    cdr, ``((head . args) . entries)``, each entry a line, the proper list
    of the elements the reader saw on it, or a nested block; the reader's
    fold_line and fold_lines apply the line rule wherever a form is wanted.
    """

    def __init__(self, head_symbol, read_fn, peers=(), raw=False):
        self.head = head_symbol
        self.read = read_fn
        self.peers = peers
        self.raw = raw


def layout(head_symbol, peers=(), raw=False):
    """
    Decorator that wraps a layout handler function in ``LayoutHandler``.

    The handler must have signature (reader, source, peers) and return
    the parsed form. Optional ``peers`` declares peer block head symbols
    that the parser should gather contiguously after this block.

    When ``raw`` is true, the reader passes child blocks through without
    eager layout expansion; the handler receives pure block source forms.
    """

    def decorator(fn):
        handler = LayoutHandler(head_symbol, fn, peers=peers, raw=raw)
        doc = fn.__doc__
        if doc:
            handler.__doc__ = doc
        return handler

    return decorator


# The end.
