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


def layout(head_symbol):
    """
    Decorator that registers a function as a layout handler.
    The handler must have signature (stream, head, args, outer_indent)
    and return the parsed form. The decorated function gets .read = self
    so LayoutReader.find_layout can use it.
    """

    def decorator(fn):
        LAYOUTS[head_symbol] = fn
        fn.read = fn
        return fn

    return decorator


# The end.
