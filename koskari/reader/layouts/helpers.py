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


from ...compile.helpers import match1
from ...lib import cons, keyword, nil
from ...matcher import BIND, LIST, LITERAL_KW, MATCH, REST
from ...types import is_nil, is_pair, is_symbol, unpack


_keyword_block = keyword(':block:')


_BLOCK_SOURCE_PATTERN = MATCH(LIST(BIND('header'), REST('body')))

_BLOCK_CONS_PATTERN = MATCH(
    LIST(LITERAL_KW(':block:'), BIND('header'), BIND('body_list')))


def _valid_block_header(header):
    """
    True when header is a block head list whose car is a symbol
    """

    return is_pair(header) and is_symbol(header.car)


def match_block_source(form):
    """
    Match a pass-one block source ``((head . args) . body-forms...)``

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
    Match a ``:block:`` cons ``(:block: (head . args) (body...))``

    :param form: candidate runtime or quote block form
    :return: match result with ``header`` and ``body_list`` slots, or None
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
    Convert a ``:block:`` cons to a pass-one block source pair

    :param form: ``(:block: (head . args) (body...))`` cons
    :return: ``((head . args) . body-forms...)`` or None
    """

    matched = match_block_cons(form)
    if matched is None:
        return None
    header = matched['header'].datum
    body_list = matched['body_list'].datum
    if is_nil(body_list):
        return cons(header, nil)
    return cons(header, *unpack(body_list), nil)


class LayoutHandler:
    """
    Wrapper for layout handlers with optional peer metadata.

    A handler is called as read_fn(ns, source, peers).
    """

    def __init__(self, head_symbol, read_fn, peers=(), raw=False):
        self.head = head_symbol
        self.read = read_fn
        self.peers = peers
        self.raw = raw


def layout(head_symbol, peers=(), raw=False):
    """
    Decorator that wraps a layout handler function in ``LayoutHandler``.

    The handler must have signature (ns, source, peers) and return
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
