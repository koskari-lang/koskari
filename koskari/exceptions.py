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
import sys
import traceback
from traceback import format_exception_only

from .bootstrap import builtin, add_builtin
from .lib import KoskariSyncViolation, car, kos_repr
from .types import is_pair


__all__ = (
    "KoskariException",
    "NotYetImplemented",
    "KoskariSyntaxError",
    "KoskariSyncViolation",
    "ReaderError",
    "ReaderSyntaxError",
    "ReaderEOF",
    "FormatStringSyntaxError",
    "LayoutError",
    "CodeBoundary",
    "format_exception_with_koskari_trace",
    "format_code_boundary_exception",
    "apply_sys_last_exc",
)


add_builtin("KoskariSyncViolation", KoskariSyncViolation)


@builtin("KoskariException")
class KoskariException(Exception):
    """
    Base class for error-driven Exceptions raised by Koskari.
    """

    pass


@builtin("NotYetImplemented")
class NotYetImplemented(KoskariException):
    """
    Raised as a placeholder for features that have not been
    implemented yet.
    """

    pass


@builtin("KoskariSyntaxError")
class KoskariSyntaxError(SyntaxError, KoskariException):
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


@builtin("ReaderError")
class ReaderError(KoskariSyntaxError):
    """
    Base class for reader-phase errors
    """

    pass


@builtin("ReaderSyntaxError")
class ReaderSyntaxError(ReaderError):
    """
    Low-level parse errors during s-expression or layout reading
    """

    pass


@builtin("ReaderEOF")
class ReaderEOF(ReaderError):
    """
    Stream ended before a delimited or quoted form finished reading.

    Distinct from top-level READ_EOF (no form started). REPL callers may
    treat this as incomplete input; file loaders should surface it as an error.
    """

    pass


@builtin("FormatStringSyntaxError")
class FormatStringSyntaxError(ReaderSyntaxError):
    """
    Errors in format string (f-string) syntax
    """

    pass


@builtin("LayoutError")
class LayoutError(ReaderError):
    """
    Layout block structure errors
    """

    pass


@builtin("CompilerError")
class CompilerError(KoskariException):
    """
    Base class for compile-phase errors
    """

    def __init__(self, message, source=None, position=None, filename=None):
        super().__init__(message)
        self.message = message
        self.source = source
        self.position = position or self._position_from_source(source)
        self.line, self.column = self._normalize_position(self.position)
        self.filename = filename or self._filename_from_source(source)


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
            return f"{prefix}: {msg}" if msg else prefix
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

        return f"-> {self.filename}:{self.line}\n   {line_text}\n   {caret}"


    def __str__(self):
        base = self._format_message()

        details = []
        if self.line is not None and self.column is not None:
            details.append(f"line {self.line}, col {self.column}")

        source_repr = self._format_source()
        if source_repr is not None:
            details.append(f"source {source_repr}")

        source_line = self._format_source_line()
        if details:
            rendered = f"{base} ({'; '.join(details)})"
        else:
            rendered = base

        if source_line is not None:
            return rendered + "\n" + source_line

        return rendered


    def explain(self):
        """
        User-facing error text for REPL and similar UIs
        """

        lines = [f"{type(self).__name__}: {self._format_message()}"]
        if self.source is not None:
            try:
                lines.append(f"  form: {kos_repr(self.source)}")
            except Exception:
                lines.append("  form: <unrenderable source>")
        source_line = self._format_source_line()
        if source_line is not None:
            lines.append(source_line)
        return "\n".join(lines) + "\n"


@builtin("SpecialError")
class SpecialError(CompilerError):
    """
    Special-form validation errors
    """

    pass


def format_accept_pattern(pattern, source=None):
    """
    Render one accepted-arm pattern for help text and SyntaxMatchError
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
            return f"({head} {rest})"
        return f"({head})"

    try:
        body = " ".join(kos_repr(c) for c in children)
    except Exception:
        return "<unrenderable pattern>"
    return f"({body})"


@builtin("SyntaxMatchError")
class SyntaxMatchError(SpecialError):
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
            head = f"no matching syntax rule for {name}"
        else:
            head = self._format_message()
        lines = [f"{type(self).__name__}: {head}"]
        if self.source is not None:
            try:
                lines.append(f"  form: {kos_repr(self.source)}")
            except Exception:
                lines.append("  form: <unrenderable source>")
        if self.patterns:
            lines.append("  accepts:")
            for pat in self.patterns:
                lines.append(f"    {format_accept_pattern(pat, self.source)}")
        source_line = self._format_source_line()
        if source_line is not None:
            lines.append(source_line)
        return "\n".join(lines) + "\n"


@builtin("ParametersError")
class ParametersError(CompilerError):
    """
    Call-site parameter errors from
    :func:`koskari.compile.helpers.gather_parameters` validation
    """

    pass


@builtin("UncaughtCompilerError")
class UncaughtCompilerError(CompilerError):
    """
    Wraps unexpected exceptions raised during compilation
    """

    pass


class CodeBoundary(Exception):
    """
    Marks a boundary between infrastructure and evaluated user code.

    The cause holds the user-visible exception. Display formatters unwrap
    the boundary and omit infrastructure frames unless nofold is requested.
    """

    pass


def _render_koskari_dynchain(trace_data):
    """
    Render dynchain metadata attached to synthetic koskari native frames

    trace_data is built innermost-first by the C runtime; it is reversed
    before rendering so output matches Python's root-first traceback order.
    Non-dict entries in trace_data are silently skipped.

    :param trace_data: iterable of dict-like frame metadata
    :return: list[str] of traceback-formatted lines
    """

    if not isinstance(trace_data, (list, tuple)) or not trace_data:
        return []

    frames = list(trace_data)
    frames.reverse()
    lines = ["  Koskari dynchain (native eval):\n"]
    for frame in frames:
        if not isinstance(frame, dict):
            continue

        name = frame.get('name', '<anonymous>')
        filename = frame.get('filename', '<unknown>')
        line = frame.get('line', 0)
        col = frame.get('col', 0)
        pc = frame.get('pc', 0)
        lines.append(f'    at {name} ({filename}:{line}:{col}) [pc={pc}]\n')

    return lines


def _find_dynchain_in_chain(exc_val):
    """
    Walk the exception chain and return __koskari_dynchain__ if present

    :param exc_val: exception value to start from
    :return: dynchain data or None
    """

    seen = set()
    to_visit = [exc_val]
    while to_visit:
        current = to_visit.pop()
        if current is None or id(current) in seen:
            continue
        seen.add(id(current))
        if hasattr(current, '__koskari_dynchain__'):
            trace_data = getattr(current, '__koskari_dynchain__', None)
            if trace_data is not None:
                return trace_data
        for attr in ('__cause__', '__context__'):
            link = getattr(current, attr, None)
            if link is not None:
                to_visit.append(link)
    return None


def _filter_infrastructure_tb(tb, infrastructure_modules):
    """
    Remove infrastructure frames from a traceback

    Walks the chain, drops frames whose __name__ is in
    infrastructure_modules, and relinks.  Returns the filtered head.

    Mutates the traceback chain in place by rewriting tb_next on existing
    traceback objects.

    Returns None when the input traceback is None and, distinctly, when every
    frame was infrastructure.  The caller is responsible for deciding how to
    render a fully-filtered chain (for example, falling back to native dynchain
    metadata) rather than silently restoring the unfiltered traceback.

    :param tb: traceback object (root of chain)
    :param infrastructure_modules: frozenset of __name__ values to drop
    :return: filtered traceback, or None when nothing survives filtering
    """

    if tb is None:
        return tb
    filtered = []
    current = tb
    while current is not None:
        frame = current.tb_frame
        mod = frame.f_globals.get('__name__', '')
        if mod not in infrastructure_modules:
            filtered.append(current)
        current = current.tb_next
    if not filtered:
        return None
    for i in range(len(filtered) - 1):
        filtered[i].tb_next = filtered[i + 1]
    filtered[-1].tb_next = None
    return filtered[0]


def format_exception_with_koskari_trace(exc_type, exc_val, tb, chain_root=None):
    """
    Format an exception traceback, interleaving koskari native dynchain data

    Interleaves after synthetic native frames that carry __koskari_trace__ in
    globals.  Falls back to exc_val.__koskari_dynchain__ when no such frame
    exists.  When chain_root is given, searches that exception chain for
    dynchain.

    :param chain_root: optional exception to walk for dynchain
    :return: list[str], matching traceback.format_exception style
    """

    if tb is None:
        fallback = None
        trace_data = _find_dynchain_in_chain(
            chain_root if chain_root is not None else exc_val)
        if trace_data is not None:
            fallback = _render_koskari_dynchain(trace_data)
        if fallback:
            out = ["Traceback (most recent call last):\n"]
            out.extend(fallback)
            out.extend(format_exception_only(exc_type, exc_val))
            return out
        return list(format_exception_only(exc_type, exc_val))

    found_dynchain = False
    for frame, _lineno in traceback.walk_tb(tb):
        if frame.f_globals.get('__koskari_trace__') is not None:
            found_dynchain = True
            break

    fallback_dynchain = None
    trace_data = _find_dynchain_in_chain(
        chain_root if chain_root is not None else exc_val)
    if not found_dynchain and trace_data is not None:
        fallback_dynchain = _render_koskari_dynchain(trace_data)

    if not found_dynchain and not fallback_dynchain:
        return traceback.format_exception(exc_type, exc_val, tb)

    out = ["Traceback (most recent call last):\n"]
    extracted = traceback.extract_tb(tb)
    walked = list(traceback.walk_tb(tb))

    for summary, (frame, _lineno) in zip(extracted, walked):
        out.extend(traceback.format_list([summary]))
        trace_data = frame.f_globals.get('__koskari_trace__')
        out.extend(_render_koskari_dynchain(trace_data))

    if fallback_dynchain:
        out.extend(fallback_dynchain)

    out.extend(format_exception_only(exc_type, exc_val))
    return out


def format_code_boundary_exception(
        boundary,
        *,
        nofold=False,
        infrastructure_modules=None):
    """
    Format an exception behind a CodeBoundary wrapper

    Unwraps boundary.__cause__ for display.  Unless nofold is True, omits
    infrastructure frames from the cause traceback per infrastructure_modules.
    When nofold is True, includes the boundary wrapper traceback before the
    cause.

    :param boundary: CodeBoundary instance (e.g. REPLCodeBoundary)
    :param nofold: when True, inline the boundary wrapper traceback
    :param infrastructure_modules: module __name__ values to strip from cause tb
    :return: (lines, exc_type, exc_val, tb) for sys.last_* and printing
    """

    cause = boundary.__cause__
    if cause is None:
        exc_type = type(boundary)
        exc_val = boundary
        tb = boundary.__traceback__
        lines = format_exception_with_koskari_trace(exc_type, exc_val, tb)
        return lines, exc_type, exc_val, tb

    exc_type = type(cause)
    exc_val = cause
    tb = cause.__traceback__

    if nofold:
        lines = []
        if boundary.__traceback__ is not None:
            lines.extend(traceback.format_exception(
                type(boundary), boundary, boundary.__traceback__))
        lines.extend(format_exception_with_koskari_trace(
            exc_type, exc_val, tb, chain_root=boundary))
        return lines, exc_type, exc_val, tb

    tb_for_format = tb
    tb_for_last = tb
    if infrastructure_modules:
        tb_filtered = _filter_infrastructure_tb(tb, infrastructure_modules)
        if tb_filtered is not None:
            tb_for_format = tb_filtered
            tb_for_last = tb_filtered
        elif _find_dynchain_in_chain(boundary) is not None:
            # Every surviving frame was infrastructure but native dynchain
            # metadata remains: drop the traceback entirely so only the
            # user-visible dynchain and exception line are shown.  Restoring
            # the unfiltered traceback here would leak the eval boundary frame.
            tb_for_format = None
            tb_for_last = None

    lines = format_exception_with_koskari_trace(
        exc_type, exc_val, tb_for_format, chain_root=boundary)
    return lines, exc_type, exc_val, tb_for_last


def apply_sys_last_exc(exc_type, exc_val, tb=None):
    """
    Set sys.last_type, sys.last_value, and sys.last_traceback for REPL/debug

    :param exc_type: exception type
    :param exc_val: exception instance
    :param tb: traceback object or None
    """

    sys.last_type = exc_type
    sys.last_value = exc_val
    sys.last_traceback = tb


# The end.
