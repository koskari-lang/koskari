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

from koskari.lib import symbol, values
from koskari.pragma import ModulePragma, lookup
from koskari.reader import IndentAwareStream, source_str
from koskari.reader.parser import READ_EOF
from koskari.types import is_nil, is_pair, is_proper, is_symbol, unpack


QUOTE = symbol('quote')


_default_reader = None


def make_layout_reader():
    """
    Build a fresh LayoutReader with default atoms, macros, and layouts.

    Read-only; no execution. Uses a copy of ``koskari.bootstrap``'s builtin
    registry as the layout namespace. Each call returns a new instance -- reads mutate per-read
    reader state, so callers that may run concurrently (e.g. an LSP server
    handling overlapping requests) must not share one reader.

    :return: a new LayoutReader instance
    """

    import koskari.compile.specials as _koskari_compile_specials  # noqa: F401
    import koskari.reader.layouts as _koskari_reader_layouts  # noqa: F401
    from koskari.bootstrap import _BUILTIN_REGISTRY
    from koskari.reader import LayoutReader
    ns = dict(_BUILTIN_REGISTRY)

    # pragma-built, not raw: token classification should see the same
    # gated surfaces (e.g. reader/decorators) a default module reads with
    return LayoutReader.from_pragma(ns)


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
    stream = IndentAwareStream.from_source(source_str(source, filename))
    return reader.read(stream)


PRAGMA_HEADS = (symbol('pragma'), symbol('package-pragma'))


def literal_value(value):
    """
    The value a pragma setting form holds when it is a literal: a
    non-symbol atom, or a quoted symbol

    :param value: a form read from a pragma setting
    :return: a one-element tuple holding the value, or None when the form
      is not a literal
    """

    if is_symbol(value):
        return None

    if not is_pair(value):
        return (value,)

    if is_proper(value):
        parts = tuple(value.unpack())
        if len(parts) == 2 and parts[0] == QUOTE and is_symbol(parts[1]):
            return (parts[1],)

    return None


def reader_pragma_changes(form):
    """
    The reader setting changes a top-level pragma form makes, as
    (name, values) pairs for a pragma rebuild

    Only reader/ settings are collected; other settings do not affect
    reading. A form that is not a pragma setting form, or that names an
    unregistered reader setting or gives one a non-literal value, yields
    None so the caller skips it.

    :param form: a top-level form
    :return: a tuple of changes, or None
    """

    if not is_proper(form):
        return None

    head, tail = form
    if head not in PRAGMA_HEADS or is_nil(tail):
        return None

    changes = []
    for setting in tail.unpack():
        if is_nil(setting) or not is_proper(setting):
            return None

        key, vals = setting
        if not is_symbol(key):
            return None

        name = str(key)
        if not name.startswith('reader/'):
            continue

        if lookup(name) is None:
            return None

        found = []
        for val in unpack(vals):
            lit = literal_value(val)
            if lit is None:
                return None
            found.extend(lit)

        changes.append((name, values(*found)))

    return tuple(changes)


def parse_all(source, filename='<string>', reader=None):
    """
    Yield each top-level form from source until EOF.

    A top-level pragma or package-pragma form whose reader settings are
    all literals applies those settings to the forms after it, as a load
    of the source would. A pragma form a load would reject is skipped.

    :param source: source string
    :param filename: filename for error reporting
    :param reader: LayoutReader to use; a fresh reader when None
    :yield: parsed s-expressions
    :raises ReaderSyntaxError: on syntax errors
    """

    if reader is None:
        reader = make_layout_reader()
    pragma = ModulePragma()
    stream = IndentAwareStream.from_source(source_str(source, filename))
    while True:
        form = reader.read(stream)
        if form is READ_EOF:
            break

        changes = reader_pragma_changes(form)
        if changes:
            try:
                pragma = pragma.rebuild(*changes)
            except (TypeError, ValueError):
                pass
            else:
                reader.reload_pragma(pragma)

        yield form


__all__ = (
    'default_layout_reader',
    'make_layout_reader',
    'parse',
    'parse_all',
    'READ_EOF',
)


# The end.
