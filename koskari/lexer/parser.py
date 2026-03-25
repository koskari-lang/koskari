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
koskari.lexer.parser

Lexical-only parser: default layout reader and parse helpers.

Provides default_layout_reader, parse, and parse_all for reading source
into s-expressions without compilation or evaluation.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude Opus 4.5 via Cursor
"""

# This file was entirely generated via prompt, as the basis for the
# prompt_toolkit REPL enhancements.


_default_reader = None


def default_layout_reader():
    """
    Return a LayoutReader with default atoms, macros, and layouts.

    Read-only; no execution. Uses a static namespace from LAYOUTS.
    The reader is cached and reused.

    :return: LayoutReader instance
    """

    global _default_reader
    if _default_reader is None:
        from koskari.reader import LayoutReader
        from koskari.reader.layouts import LAYOUTS
        ns = {str(k): v for k, v in LAYOUTS.items()}
        _default_reader = LayoutReader(ns)
    return _default_reader


def parse(source, filename='<string>'):
    """
    Parse a single top-level form from source.

    Uses default_layout_reader and source_str. Returns the parsed
    s-expression or None at EOF.

    :param source: source string
    :param filename: filename for error reporting
    :return: parsed form or None at EOF
    :raises ReaderSyntaxError: on syntax errors
    """

    from koskari.reader import source_str

    reader = default_layout_reader()
    stream = source_str(source, filename)
    return reader.read(stream)


def parse_all(source, filename='<string>'):
    """
    Yield each top-level form from source until EOF.

    :param source: source string
    :param filename: filename for error reporting
    :yield: parsed s-expressions
    :raises ReaderSyntaxError: on syntax errors
    """

    from koskari.reader import source_str

    reader = default_layout_reader()
    stream = source_str(source, filename)
    while True:
        form = reader.read(stream)
        if form is None:
            break
        yield form


__all__ = (
    'default_layout_reader',
    'parse',
    'parse_all',
)


# The end.
