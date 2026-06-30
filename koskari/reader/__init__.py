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
koskari.reader

Koskari s-expression reader and layout block parsing.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude Opus 4.5 via Cursor
"""


from ..exceptions import ReaderEOF, ReaderSyntaxError, FormatStringSyntaxError
from . import pragma as _pragma  # noqa: F401
from .parser import (
    DEFER,
    LINE_END,
    SourceStream,
    IndentAwareStream,
    source_open,
    source_str,
    source_stream,
    Atom,
    Reader,
    LayoutReader,
    default_reader,
    READ_EOF,
)


__all__ = (
    "DEFER",
    "LINE_END",
    "Atom",
    "ReaderSyntaxError",
    "ReaderEOF",
    "FormatStringSyntaxError",
    "SourceStream",
    "IndentAwareStream",
    "source_open",
    "source_str",
    "source_stream",
    "Reader",
    "LayoutReader",
    "default_reader",
    "READ_EOF",
)


# The end.
