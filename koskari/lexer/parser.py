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
"""

# This file was entirely generated via prompt, as the basis for the
# prompt_toolkit REPL enhancements.

from koskari.reader.parser import READ_EOF
from koskari.reader import source_str


_default_reader = None


def make_layout_reader():
    """
    Build a fresh LayoutReader with default atoms, macros, and layouts.

    Read-only; no execution. Uses ``koskari.stdlib.BUILTINS`` as the layout
    namespace. Each call returns a new instance -- reads mutate per-read
    reader state, so callers that may run concurrently (e.g. an LSP server
    handling overlapping requests) must not share one reader.

    :return: a new LayoutReader instance
    """

    import koskari.compile.specials as _koskari_compile_specials  # noqa: F401
    import koskari.reader.layouts as _koskari_reader_layouts  # noqa: F401
    from koskari.reader import LayoutReader
    from koskari.bootstrap import _BUILTIN_REGISTRY
    ns = dict(_BUILTIN_REGISTRY)
    return LayoutReader(ns)


def default_layout_reader():
    """
    Return a process-wide cached LayoutReader.

    Suitable for a single-threaded consumer such as the REPL. Consumers that
    may issue overlapping reads must build their own with
    :func:`make_layout_reader` instead.

    :return: the shared LayoutReader instance
    """

    global _default_reader
    if _default_reader is None:
        _default_reader = make_layout_reader()
    return _default_reader


def parse(source, filename='<string>', reader=None):
    """
    Parse a single top-level form from source.

    Returns the parsed s-expression or READ_EOF when there is no form (empty
    or whitespace only).

    :param source: source string
    :param filename: filename for error reporting
    :param reader: LayoutReader to use; the shared cached reader when None
    :return: parsed form or READ_EOF at EOF
    :raises ReaderSyntaxError: on syntax errors
    """

    if reader is None:
        reader = default_layout_reader()
    stream = source_str(source, filename)
    return reader.read(stream)


def parse_all(source, filename='<string>', reader=None):
    """
    Yield each top-level form from source until EOF.

    :param source: source string
    :param filename: filename for error reporting
    :param reader: LayoutReader to use; the shared cached reader when None
    :yield: parsed s-expressions
    :raises ReaderSyntaxError: on syntax errors
    """

    if reader is None:
        reader = default_layout_reader()
    stream = source_str(source, filename)
    while True:
        form = reader.read(stream)
        if form is READ_EOF:
            break
        yield form


__all__ = (
    'default_layout_reader',
    'make_layout_reader',
    'parse',
    'parse_all',
    'READ_EOF',
)


# The end.
