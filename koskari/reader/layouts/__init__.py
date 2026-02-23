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
koskari.reader.layouts

Layout handlers for reader block syntax (layout decorator and LAYOUTS).

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude Opus 4.5 via Cursor
"""


from .helpers import LAYOUTS, layout, LayoutHandler
from .conditionals import layout_block, layout_begin, layout_if


__all__ = (
    "LAYOUTS",
    "layout",
    "LayoutHandler",

    "layout_block",
    "layout_begin",
    "layout_if",
)


# The end.
