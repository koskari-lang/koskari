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
koskari.reader.stream

The source stream layers under the reader: BaseSourceStream (C native)
does hot-path I/O with Unicode position tracking, SourceStream adds
error reporting helpers, and IndentAwareStream adds indent-bounded
whitespace skipping with a dedented flag for layout reads. The factory
helpers open a file, wrap a string, or wrap an existing stream.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from contextlib import contextmanager
from functools import partial
from io import StringIO

from ..exceptions import ReaderEOF, ReaderSyntaxError
from ..lib import BaseSourceStream


__all__ = (
    "SourceStream",
    "IndentAwareStream",
    "source_open",
    "source_str",
    "source_stream",
)


# ===========================================================================
# Stream layer
# ===========================================================================


class SourceStream(BaseSourceStream):
    """
    Source stream with error reporting helpers

    Inherits from the native BaseSourceStream for efficient character-level
    I/O with position tracking. Adds error reporting and the helper methods
    used by the reader (skip_exec, suspend_indent).
    """

    # a plain stream has no indent threshold, so a whitespace skip never
    # stops on a dedent. Declared here so every stream carries the
    # attribute and readers can consult it without a getattr guard;
    # IndentAwareStream shadows it per instance.
    dedented = False


    def __init__(
            self,
            stream,
            filename=None,
            auto_skip_exec=True,
            line_no=1,
            col_no=0):
        """
        Initialize a source stream

        :param stream: the underlying seekable stream
        :param filename: the filename for error reporting
        :param auto_skip_exec: if True, skip any #! line at the start
        :param line_no: starting line number (default 1)
        :param col_no: starting column number (default 0)
        :raises TypeError: if stream is not seekable
        """

        if not stream.seekable():
            raise TypeError("SourceStream's stream argument must be seekable")

        super().__init__(stream, filename, line=line_no, col=col_no)

        # where the character that triggered the running event macro
        # stood; set by Reader._read before each dispatch, read by macros
        # that report the opener of an unfinished form
        self.event_position = None

        if auto_skip_exec:
            self.skip_exec()


    def _source_line(self, line_no):
        """
        Return the text of 1-based ``line_no`` from the underlying stream

        Reads from the stream rather than re-opening the file, so the snippet
        matches what is being parsed (including string sources) and stays
        correct if the file has changed on disk. The underlying stream
        position is restored.

        :param line_no: 1-based line number
        :return: the line text (with newline) or None if unavailable
        """

        underlying = self.stream
        try:
            saved = underlying.tell()
        except (OSError, ValueError, AttributeError):
            return None

        try:
            underlying.seek(0)
            text = None
            for _ in range(line_no):
                line = underlying.readline()
                if not line:
                    break
                text = line
            return text
        finally:
            underlying.seek(saved)


    def error(self, message, position=None):
        """
        Create a ReaderSyntaxError for the given or current position

        :param message: the error message
        :param position: optional (line, col) tuple; uses current if None
        :return: a ReaderSyntaxError instance
        """

        if position is None:
            position = self.lin, self.col

        text = self._source_line(position[0])

        return ReaderSyntaxError(message, position,
                                 text=text, filename=self.filename)


    def reader_eof(self, position=None, *, context=None, opened_at=None):
        """
        Build a ReaderEOF for stream end inside an unfinished form

        Does not read the source file for a source line snippet; callers
        raise this immediately and REPL parse paths map it to incomplete
        input without displaying the exception.

        When the unfinished form has an opener, a quote, a bracket, a
        field brace, the error is located at that opener and the message
        names it, so a reader of the error is pointed at what was never
        closed rather than at the end of the input.

        :param position: optional (line, col) tuple; uses current if None
        :param context: optional phrase naming the unfinished form
        :param opened_at: (line, col) of the form's opener; when given it
            becomes the error location and is named in the message
        :return: a ReaderEOF instance (caller should raise it)
        """

        if opened_at is not None:
            line, col = opened_at
            what = context or "form"
            message = (f"unexpected EOF in {what} opened at line {line},"
                       f" column {col}")
            return ReaderEOF(message, opened_at,
                             text=None, filename=self.filename)

        if position is None:
            position = self.lin, self.col

        message = "unexpected EOF"
        if context:
            message = f"{message} in {context}"

        return ReaderEOF(message, position,
                         text=None, filename=self.filename)


    def __iter__(self):
        """
        Iterate over characters in the stream
        """

        return iter(partial(self.read, 1), '')


    @contextmanager
    def speculate(self):
        """
        Mark the stream on entry and rewind to that mark on exit

        For pure lookahead: read freely inside the block to inspect upcoming
        input; the stream (position and line/col) is restored to its
        pre-block state on exit, whether the block returns or raises. Unlike
        a fixed-size peek, a speculative scan sees the whole remaining input.
        """

        checkpoint = self.mark()
        try:
            yield
        finally:
            self.rewind(checkpoint)


    def peek_line(self):
        """
        Peek the rest of the current physical line without consuming it

        Returns the text from the current position up to (not including) the
        next newline or carriage return, or to end of stream. Unbounded: the
        result is the whole line regardless of length.

        :return: the remaining text on the current physical line
        """

        with self.speculate():
            return self.read_until_chars("\r\n")


    def skip_exec(self):
        """
        Skip a #! line at the start of the stream
        """

        if self.peek(2) == "#!":
            self.readline()


    @contextmanager
    def suspend_indent(self):
        """
        No-op indent suspension for streams without indent tracking

        Streams that do not bound whitespace skipping by indentation have
        nothing to suspend, so this simply yields.
        """

        yield


class IndentAwareStream(SourceStream):
    """
    Source stream with indent-bounded skip_whitespace

    Extends SourceStream with a max_indent threshold. When max_indent is set,
    skip_whitespace inspects the indent of the next content line without
    consuming it. If that indent is at or below max_indent, skip_whitespace
    sets self.dedented = True and returns without consuming any characters,
    leaving the stream positioned at the newline so the outer reader can
    handle the boundary.

    When max_indent is None, skip_whitespace delegates to the native C
    implementation (BaseSourceStream.skip_whitespace) for maximum speed.

    The dedented attribute is reset to False at the start of each
    skip_whitespace call. Callers read it immediately after skip_whitespace
    returns; it is never guarded by getattr.
    """

    def __init__(self, stream, filename=None, auto_skip_exec=True,
                 line_no=1, col_no=0, max_indent=None):
        """
        Initialize an indent-aware source stream

        :param stream: the underlying seekable stream
        :param filename: the filename for error reporting
        :param auto_skip_exec: if True, skip any #! line at the start
        :param line_no: starting line number (default 1)
        :param col_no: starting column number (default 0)
        :param max_indent: column value; skip_whitespace stops without
            consuming when a content line's indent is <= this value
        """

        super().__init__(stream, filename, auto_skip_exec=auto_skip_exec,
                         line_no=line_no, col_no=col_no)
        self._max_indent = max_indent
        self.dedented = False


    @classmethod
    def from_source(cls, source_stream, max_indent=None):
        """
        The layout stream over a plain SourceStream, continuing from its
        current position. Made once where a stream is created for layout
        reading; the plain stream itself is not read afterwards

        :param source_stream: a SourceStream, its ``#!`` line already
            handled
        :param max_indent: as for the constructor
        """

        return cls(
            source_stream.stream,
            source_stream.filename,
            auto_skip_exec=False,
            line_no=source_stream.lin,
            col_no=source_stream.col,
            max_indent=max_indent)


    def skip_whitespace(self):
        """
        Skip whitespace, stopping at dedent when max_indent is set

        When max_indent is None, delegates to the native C skip_whitespace.

        Otherwise, speculatively scans the upcoming whitespace and blank
        lines to find the next content character. Blank lines are skipped,
        and a line holding only whitespace and a comment is a blank line:
        a comment is never content and its column is never an indent. The
        first non-blank content line is measured:

        - If the content is on the current line (no newline was crossed),
          the leading horizontal whitespace is consumed and the stream is
          left at the content.

        - If a newline was crossed and the content line's indent is
          <= max_indent, sets self.dedented = True and consumes nothing,
          leaving the stream at the boundary.

        - If a newline was crossed and the indent is > max_indent, consumes
          the newline(s) and indent, leaving the stream at the content.

        The scan is speculative (mark/rewind) so its lookahead is unbounded
        regardless of how many blank lines separate the content.
        """

        if self._max_indent is None:
            self.dedented = False
            return super().skip_whitespace()

        self.dedented = False
        max_indent = self._max_indent

        # Being at the start of a line (col 0) means the current line's own
        # indent is the dedent candidate, exactly as if a newline had just
        # been crossed.
        saw_newline = (self.col == 0)
        indent = 0
        consume = 0

        with self.speculate():
            while True:
                c = self.read(1)
                if not c:
                    # EOF within whitespace: dedent, consume nothing.
                    consume = None
                    break
                if c == '\n':
                    saw_newline = True
                    indent = 0
                    consume += 1
                elif c in ' \t':
                    indent += 1
                    consume += 1
                elif c == ';':
                    # a comment runs to the end of its line and counts as
                    # blank: swallow it and let the newline be measured
                    rest = self.read_until_chars('\n')
                    consume += 1 + len(rest)
                elif not saw_newline:
                    # Content on the current line: keep the horizontal
                    # whitespace already scanned.
                    break
                elif indent <= max_indent:
                    consume = None
                    break
                else:
                    break

        if consume is None:
            self.dedented = True
            return ''
        if consume:
            self.read(consume)
        return ''


    @contextmanager
    def suspend_indent(self):
        """
        Temporarily disable indent-bounded whitespace skipping

        Indentation has no meaning inside a balanced delimited form, so a
        pair read suspends the indent guard for the duration of its body.
        While suspended, max_indent is None and skip_whitespace consumes
        vertical whitespace freely (native behavior), letting a form span
        dedented lines up to its closing delimiter. The previous max_indent
        is restored on exit.
        """

        old = self._max_indent
        self._max_indent = None
        try:
            yield
        finally:
            self._max_indent = old


    @contextmanager
    def indent_limit(self, max_indent):
        """
        Bound skip_whitespace by ``max_indent`` for the duration

        While active, skip_whitespace sets :attr:`dedented` and stops without
        consuming when the next content line is at or below ``max_indent``.
        The previous limit is restored on exit.

        :param max_indent: column that ends a block body on dedent
        """

        old = self._max_indent
        self._max_indent = max_indent
        try:
            yield
        finally:
            self._max_indent = old


# ===========================================================================
# Stream factory helpers
# ===========================================================================


@contextmanager
def source_open(filename, auto_skip_exec=True):
    """
    Context manager to open a file as a SourceStream

    :param filename: path to the file to open
    :param auto_skip_exec: if True, skip any #! line at the start
    :yields: SourceStream for the file
    """

    with open(filename, "rt") as fs:
        reader = SourceStream(fs, filename=filename,
                              auto_skip_exec=auto_skip_exec)
        yield reader


def source_str(src, filename, auto_skip_exec=True, line_no=1, col_no=0):
    """
    Create a SourceStream from a string

    Newlines are normalized to ``\\n`` so string sources behave like files
    opened in text mode; the reader then never sees a carriage return.

    :param src: the source string
    :param filename: the filename to report in errors
    :param auto_skip_exec: if True, skip any #! line at the start
    :param line_no: starting line number
    :param col_no: starting column number
    :return: a SourceStream
    """

    src = src.replace("\r\n", "\n").replace("\r", "\n")

    return SourceStream(
        StringIO(src),
        filename,
        auto_skip_exec=auto_skip_exec,
        line_no=line_no,
        col_no=col_no)


def source_stream(underlying, filename, auto_skip_exec=True):
    """
    Create a SourceStream wrapping an existing stream

    :param underlying: the stream to wrap
    :param filename: the filename to report in errors
    :param auto_skip_exec: if True, skip any #! line at the start
    :return: a SourceStream
    """

    return SourceStream(underlying, filename,
                        auto_skip_exec=auto_skip_exec)


# The end.
