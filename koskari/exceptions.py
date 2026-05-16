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
koskari.exceptions

Base exception classes for Koskari.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import linecache
from traceback import format_exception_only

from .lib import KoskariSyncViolation, car, kos_repr
from .types import is_pair


__all__ = (
    "KoskariException",
    "NotYetImplemented",
    "KoskariSyntaxError",
    "KoskariSyncViolation",
    "ReaderError",
    "ReaderSyntaxError",
    "FormatStringSyntaxError",
    "LayoutError",
)


class KoskariException(Exception):
    """
    Base class for error-driven Exceptions raised by Koskari.
    """

    pass


class NotYetImplemented(KoskariException):
    """
    Raised as a placeholder for features that have not been
    implemented yet.
    """

    pass


class KoskariSyntaxError(SyntaxError):
    """
    An error in Koskari syntax, either during read or compile time.
    """

    def __init__(self, message, location=None, filename=None, text=None):

        if filename:
            if not location:
                location = (1, 0)
            super().__init__(message, (filename, *location, text))
            self.print_file_and_line = True
        else:
            super().__init__(message)

        self.message = message
        self.location = location
        self.filename = filename
        self.text = text


    def explain(self):
        """
        User-facing error text for REPL and similar UIs
        """

        return ''.join(format_exception_only(type(self), self))


class ReaderError(KoskariSyntaxError):
    """
    Base class for reader-phase errors
    """

    pass


class ReaderSyntaxError(ReaderError):
    """
    Low-level parse errors during s-expression or layout reading
    """

    pass


class FormatStringSyntaxError(ReaderSyntaxError):
    """
    Errors in format string (f-string) syntax
    """

    pass


class LayoutError(ReaderError):
    """
    Layout block structure errors
    """

    pass


class CompilerError(KoskariException):
    """
    Base class for compile-phase errors
    """

    def __init__(self, message, source=None, position=None, filename=None):
        super().__init__(message)
        self.message = message
        self.source = source
        self.position = position if position is not None else self._position_from_source(source)
        self.line, self.column = self._normalize_position(self.position)
        self.filename = filename if filename is not None else self._filename_from_source(source)


    @staticmethod
    def _position_from_source(source):
        """
        Best-effort source-position extraction from source object
        """

        if source is None:
            return None

        if hasattr(source, "get_position"):
            try:
                return source.get_position()
            except Exception:
                return None

        return None


    @staticmethod
    def _normalize_position(position):
        """
        Return (line, column) or (None, None) from a position value
        """

        if isinstance(position, (tuple, list)) and len(position) >= 2:
            return position[0], position[1]
        return None, None


    @staticmethod
    def _filename_from_source(source):
        """
        Best-effort source-filename extraction from source object
        """

        if source is None:
            return None

        for attr in ("source_filename", "filename", "__file__"):
            if hasattr(source, attr):
                try:
                    val = getattr(source, attr)
                except Exception:
                    continue
                if isinstance(val, str) and val:
                    return val

        return None


    def _format_message(self):
        """
        Produce stable, readable message text
        """

        if isinstance(self.message, Exception):
            msg = str(self.message)
            prefix = type(self.message).__name__
            return "%s: %s" % (prefix, msg) if msg else prefix
        return str(self.message)


    def _format_source(self):
        """
        Truncated repr of source object for diagnostics
        """

        if self.source is None:
            return None

        try:
            rendered = repr(self.source)
        except Exception:
            return "<unrenderable source>"

        if len(rendered) > 180:
            rendered = rendered[:177] + "..."
        return rendered


    def _format_source_line(self):
        """
        Return a caret-annotated source-line block when available
        """

        if not self.filename or self.line is None:
            return None

        try:
            line_text = linecache.getline(self.filename, int(self.line))
        except Exception:
            return None

        if not line_text:
            return None

        line_text = line_text.rstrip("\n")
        col = int(self.column) if self.column is not None else 0
        if col < 0:
            col = 0
        caret = (" " * col) + "^"

        return "-> %s:%s\n   %s\n   %s" % (
            self.filename, self.line, line_text, caret)


    def __str__(self):
        base = self._format_message()

        details = []
        if self.line is not None and self.column is not None:
            details.append("line %s, col %s" % (self.line, self.column))

        source_repr = self._format_source()
        if source_repr is not None:
            details.append("source %s" % source_repr)

        source_line = self._format_source_line()
        if details:
            rendered = "%s (%s)" % (base, "; ".join(details))
        else:
            rendered = base

        if source_line is not None:
            return rendered + "\n" + source_line

        return rendered


    def explain(self):
        """
        User-facing error text for REPL and similar UIs
        """

        lines = ["%s: %s" % (type(self).__name__, self._format_message())]
        if self.source is not None:
            try:
                lines.append("  form: %s" % kos_repr(self.source))
            except Exception:
                lines.append("  form: <unrenderable source>")
        source_line = self._format_source_line()
        if source_line is not None:
            lines.append(source_line)
        return "\n".join(lines) + "\n"


class SpecialError(CompilerError):
    """
    Special-form validation errors
    """

    pass


def _format_accept_pattern(pattern, source):
    """
    Render one accepted-arm pattern for SyntaxMatchError.explain only
    """

    from .matcher import CALLEDBY, MATCH

    if isinstance(pattern, MATCH):
        children = pattern._children
    else:
        try:
            return kos_repr(pattern)
        except Exception:
            return "<unrenderable pattern>"

    if children and isinstance(children[0], CALLEDBY):
        try:
            if source is not None and is_pair(source):
                head = kos_repr(car(source))
            else:
                head = "(CALLEDBY)"
        except Exception:
            head = "(CALLEDBY)"
        try:
            rest = " ".join(kos_repr(c) for c in children[1:])
        except Exception:
            rest = "<unrenderable pattern>"
        if rest:
            return "(%s %s)" % (head, rest)
        return "(%s)" % head

    try:
        body = " ".join(kos_repr(c) for c in children)
    except Exception:
        return "<unrenderable pattern>"
    return "(%s)" % body


class SyntaxMatchError(CompilerError):
    """
    Syntax dispatch errors when no matcher arm matches the source form
    """

    def __init__(self, message, source=None, position=None, filename=None,
                 *, syntax=None, patterns=None):
        super().__init__(message, source, position, filename)
        self.syntax = syntax
        self.patterns = patterns


    def explain(self):
        """
        User-facing error text for REPL and similar UIs
        """

        name = self.syntax.name if self.syntax is not None else None
        if name is not None:
            head = "no matching syntax rule for %s" % (name,)
        else:
            head = self._format_message()
        lines = ["%s: %s" % (type(self).__name__, head)]
        if self.source is not None:
            try:
                lines.append("  form: %s" % kos_repr(self.source))
            except Exception:
                lines.append("  form: <unrenderable source>")
        if self.patterns:
            lines.append("  accepts:")
            for pat in self.patterns:
                lines.append("    %s" % _format_accept_pattern(pat, self.source))
        source_line = self._format_source_line()
        if source_line is not None:
            lines.append(source_line)
        return "\n".join(lines) + "\n"


class ParametersError(CompilerError):
    """
    Call-site parameter errors from
    :func:`koskari.compile.helpers.gather_parameters` validation
    """

    pass


class UncaughtCompilerError(CompilerError):
    """
    Wraps unexpected exceptions raised during compilation
    """

    pass


# The end.
