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

Layout decorator and LAYOUTS collector for reader block syntax.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude Opus 4.5 via Cursor
"""


LAYOUTS = {}


class LayoutHandler(object):
    """
    Wrapper for layout handlers with optional peer metadata.

    A handler is called as read_fn(source, peers).
    """

    def __init__(self, head_symbol, read_fn, peers=()):
        self.head = head_symbol
        self.read = read_fn
        self.peers = peers


def layout(head_symbol, peers=()):
    """
    Decorator that registers a function as a layout handler.

    The handler must have signature (source, peers) and return
    the parsed form. Optional ``peers`` declares peer block head symbols
    that the parser should gather contiguously after this block.
    """

    def decorator(fn):
        handler = LayoutHandler(head_symbol, fn, peers=peers)
        LAYOUTS[head_symbol] = handler
        return handler

    return decorator


# The end.
