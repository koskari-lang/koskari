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

from .bootstrap import add_builtin, builtin
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
    "CompilerError",
    "SpecialError",
    "FenceError",
    "SyntaxMatchError",
    "ParametersError",
    "UncaughtCompilerError",
    "KoskariContinuationError",
    "KoskariPromptNotFound",
    "KoskariPromptUnreachable",
    "CodeBoundary",
    "RUN_INFRASTRUCTURE_MODULES",
    "format_exception_with_koskari_trace",
    "format_code_boundary_exception",
    "format_run_exception",
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

        super().__init__(message)
        self.message = message
        self.location = location
        self.filename = filename
        self.text = text
        if filename:
            self.locate(filename, location, text)


    def locate(self, filename, location=None, text=None):
        """
        Give the error the file, position and source text that name
        where it happened, the way SyntaxError prints them. Fills what
        the raise site did not have: a location or text the error
        already carries is kept
        """

        location = self.location or location or (1, 0)
        text = self.text if self.text is not None else text
        # the reader's column is 0-based; SyntaxError's offset is
        # 1-based and the traceback caret is drawn at offset - 1
        line, col = location[0], location[1]
        self.args = (self.message, (filename, line, col + 1, text))
        self.filename = filename
        self.lineno = line
        self.offset = col + 1
        self.text = text
        self.location = location
        self.print_file_and_line = True


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


    def attach_position(self, position=None, filename=None):
        """
        Fill in a position and a filename the raise site did not have:
        the innermost enclosing form's position and the compiler's file.
        What the error already carries is kept
        """

        if self.position is None and position is not None:
            self.position = position
            self.line, self.column = self._normalize_position(position)
        if self.filename is None and filename is not None:
            self.filename = filename


    def _format_location(self):
        """
        The caret-annotated source line when the file can be read, else
        the file and position as text, else None
        """

        source_line = self._format_source_line()
        if source_line is not None:
            return source_line
        if self.line is None:
            return None

        where = str(self.line)
        if self.column is not None:
            where = f"{where}:{self.column}"
        if self.filename:
            return f"-> {self.filename}:{where}"
        return f"-> line {where}"


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
        location = self._format_location()
        if location is not None:
            lines.append(location)
        return "\n".join(lines) + "\n"


@builtin("SpecialError")
class SpecialError(CompilerError):
    """
    Special-form validation errors
    """

    pass


@builtin("FenceError")
class FenceError(CompilerError):
    """
    Fence expansion and foreign-source parse errors
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
        location = self._format_location()
        if location is not None:
            lines.append(location)
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


@builtin("KoskariContinuationError")
class KoskariContinuationError(KoskariException):
    """
    Base class for continuation capture and transfer errors.

    The catch target for the whole category. Deliberately not a
    RuntimeError subclass: an overbroad except RuntimeError around a
    continuation-using call must not silently swallow these
    """

    def __init__(self, message, tag=None, operation=None):
        super().__init__(message)
        self.message = message
        self.tag = tag
        self.operation = operation


@builtin("KoskariPromptNotFound")
class KoskariPromptNotFound(KoskariContinuationError):
    """
    No prompt with the requested tag exists on the dynamic chain.

    The construct in operation (shift) has no enclosing delimiter;
    establish one. tag is the prompt tag the capture was seeking,
    compared by identity, letting a library recognize a failure against
    its own delimiter and leave foreign failures alone
    """

    pass


@builtin("KoskariPromptUnreachable")
class KoskariPromptUnreachable(KoskariContinuationError):
    """
    A matching prompt exists but a crossed node denied capture.

    Adding a delimiter will not help; the region between the capture
    point and the delimiter must be restructured. boundary describes
    the denying node (for example a generator boundary)
    """

    def __init__(self, message, tag=None, operation=None,
                 boundary=None):
        super().__init__(message, tag, operation)
        self.boundary = boundary


class CodeBoundary(Exception):
    """
    Marks a boundary between infrastructure and evaluated user code.

    The cause holds the user-visible exception. Display formatters unwrap
    the boundary and omit infrastructure frames unless nofold is requested.
    """

    pass


# traceback.FrameSummary gained the PEP 657 column parameters (colno,
# end_colno, end_lineno) in Python 3.11
_FRAME_SUMMARY_HAS_COLUMNS = sys.version_info >= (3, 11)


def _koskari_frame_summary(entry):
    """
    The FrameSummary for a synthetic koskari native frame, from the
    trace data its globals carry as __koskari_trace__

    The summary renders through StackSummary.format, which reads the
    source line via linecache and draws the PEP 657 caret from the
    column data.  Column data is only passed on Python 3.11+ (see
    _FRAME_SUMMARY_HAS_COLUMNS); earlier versions still render the
    source line, without the caret

    :param entry: dict of frame metadata (filename, name, line, col)
    :return: traceback.FrameSummary
    """

    filename = entry.get('filename') or '<unknown>'
    name = entry.get('name') or '<anonymous>'
    line = entry.get('line') or None

    if not _FRAME_SUMMARY_HAS_COLUMNS:
        return traceback.FrameSummary(filename, line or 0, name)

    col = entry.get('col') if line is not None else None
    end_line = entry.get('end_line')
    end_col = entry.get('end_col')

    # Koskari positions are start-oriented: a form records where it
    # opens, not where it closes.  Without a recorded end, point a
    # single-column caret at the start.  end_lineno must accompany
    # end_colno or traceback renders the caret one column too wide.
    if col is not None:
        if end_col is None:
            end_col = col + 1
        if end_line is None:
            end_line = line
    else:
        end_line = end_col = None

    return traceback.FrameSummary(
        filename, line, name,
        colno=col, end_lineno=end_line, end_colno=end_col)


def _filter_infrastructure_tb(tb, infrastructure_modules, infrastructure_files=()):
    """
    Remove infrastructure frames from a traceback

    Walks the chain, drops frames whose __name__ is in
    infrastructure_modules, and relinks.  Returns the filtered head.

    Mutates the traceback chain in place by rewriting tb_next on existing
    traceback objects.

    Returns None when the input traceback is None and, distinctly, when every
    frame was infrastructure.  The caller renders a fully-filtered chain as
    the exception line alone rather than silently restoring the unfiltered
    traceback.

    :param tb: traceback object (root of chain)
    :param infrastructure_modules: frozenset of __name__ values to drop
    :param infrastructure_files: code filenames to drop
    :return: filtered traceback, or None when nothing survives filtering
    """

    if tb is None:
        return tb
    filtered = []
    current = tb
    while current is not None:
        frame = current.tb_frame
        mod = frame.f_globals.get('__name__', '')
        if mod not in infrastructure_modules \
                and frame.f_code.co_filename not in infrastructure_files:
            filtered.append(current)
        current = current.tb_next
    if not filtered:
        return None
    for i in range(len(filtered) - 1):
        filtered[i].tb_next = filtered[i + 1]
    filtered[-1].tb_next = None
    return filtered[0]


def format_exception_with_koskari_trace(exc_type, exc_val, tb):
    """
    Format an exception traceback, rendering the koskari native frames
    where they sit

    A synthetic native frame carries its own trace data as
    __koskari_trace__, so one walk of the traceback renders every frame
    in order: a native frame from its entry, with the source line and
    column caret, any other frame as the standard traceback does.  The
    frames of a raise that crossed Koskari, Python and Koskari again
    therefore print in their true nesting

    :return: list[str], matching traceback.format_exception style
    """

    if tb is None:
        return list(format_exception_only(exc_type, exc_val))

    walked = list(traceback.walk_tb(tb))
    if not any(isinstance(frame.f_globals.get('__koskari_trace__'), dict)
               for frame, _lineno in walked):
        return traceback.format_exception(exc_type, exc_val, tb)

    summaries = []
    for summary, (frame, _lineno) in zip(traceback.extract_tb(tb), walked):
        entry = frame.f_globals.get('__koskari_trace__')
        if isinstance(entry, dict):
            summaries.append(_koskari_frame_summary(entry))
        else:
            summaries.append(summary)

    out = ["Traceback (most recent call last):\n"]
    out.extend(traceback.StackSummary.from_list(summaries).format())
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
            exc_type, exc_val, tb))
        return lines, exc_type, exc_val, tb

    if infrastructure_modules:
        # None when every frame was infrastructure: the traceback is
        # dropped so only the exception line is shown, since restoring
        # the unfiltered one would leak the eval boundary frame
        tb = _filter_infrastructure_tb(tb, infrastructure_modules)

    lines = format_exception_with_koskari_trace(exc_type, exc_val, tb)
    return lines, exc_type, exc_val, tb


RUN_INFRASTRUCTURE_MODULES = frozenset((
    'koskari.cli',
    'koskari.importer',
    'koskari.launcher',
    'koskari.module',
    'koskari.serialize.incremental',
    'asyncio',
    'asyncio.base_events',
    'asyncio.events',
    'asyncio.runners',
    'concurrent.futures',
    'concurrent.futures._base',
))


def format_run_exception(
        exc_val,
        *,
        full_trace=False,
        infrastructure_modules=RUN_INFRASTRUCTURE_MODULES):
    """
    Format an uncaught exception from a non-interactive CLI run

    Mirrors the boundary cleaning the REPL applies per form, but for a
    single top-level run (a file, ``-c`` command, or ``.kio``).  Reader and
    compile errors render their own source-annotated explanation; runtime
    errors have infrastructure frames stripped (per infrastructure_modules)
    and their koskari frames rendered where they sit

    When full_trace is True the raw traceback is kept and no filtering is
    applied, for debugging the runtime itself

    :param exc_val: the uncaught exception instance
    :param full_trace: when True, keep infrastructure frames
    :param infrastructure_modules: module __name__ values to strip
    :return: (lines, tb) where lines is list[str] of traceback text and tb
        is the traceback to record via apply_sys_last_exc
    """

    tb = exc_val.__traceback__

    # Reader and compile errors carry a source-annotated explanation and
    # arise before any eval dynchain exists; render that directly.
    if isinstance(exc_val, (CompilerError, KoskariSyntaxError)) \
            and not isinstance(exc_val, UncaughtCompilerError):
        return [exc_val.explain()], None

    if not full_trace:
        # the launchpad is Koskari code, so its synthetic frame is
        # dropped by file, not by module name; every surviving frame
        # being infrastructure leaves the exception line alone
        tb = _filter_infrastructure_tb(
            tb, infrastructure_modules, _launchpad_files())

    lines = format_exception_with_koskari_trace(type(exc_val), exc_val, tb)
    return lines, tb


def _launchpad_files():
    """
    The file of the CLI launchpad module, when it is loaded
    """

    launch = sys.modules.get('koskari.cli.launch')
    path = getattr(launch, '__file__', None)
    return (path,) if path else ()


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
