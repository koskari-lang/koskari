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
koskari.reader.layout_ng

Strict layout_ng reader.

This reader is implemented as a sibling of the existing LayoutReader and
LayoutNdReader. Pass 1 introduces strict commit + error-on-missing-terminator
behaviour (see tests/reader/test_layout_ng.py).
"""

from .layout_nd import LayoutNdReader

from ..types import is_pair, is_symbol
from .parser import COLON, EOF, VALUE


class HeaderCommitDetector:
    """
    Decide whether the current header head should commit to header parsing.

    Commit rule:
    - After consuming the head atom, if the next non-horizontal-whitespace
      character is:
        - `;` (comment start) => do NOT commit
        - `\\n` or `\\r` or EOF => do NOT commit
      then commit only if there is any other token character present.
    """

    def __init__(self, *, hot_peek=2, fallback_peek=2048):
        self._hot_peek = hot_peek
        self._fallback_peek = fallback_peek

    def should_commit(self, stream):
        ahead = stream.peek(self._hot_peek) or ''
        i = 0
        while i < len(ahead) and ahead[i] in ' \t':
            i += 1

        if i >= len(ahead):
            # Undecidable from the hot peek alone; inspect a larger window.
            ahead = stream.peek(self._fallback_peek) or ''
            i = 0
            while i < len(ahead) and ahead[i] in ' \t':
                i += 1

            if i >= len(ahead):
                return False
            ch = ahead[i]
        else:
            ch = ahead[i]

        if ch == ';' or ch in '\n\r':
            return False
        return True


class LogicalLineScanner:
    """
    Minimal logical-line boundary helper for header-mode logic.

    A newline ends the logical line only at delimiter depth 0.
    """

    def __init__(self, reader):
        self._reader = reader

    def at_logical_eol(self, stream):
        if self._reader._layout_depth != 0:
            return False
        nxt = stream.peek(1)
        if not nxt:
            return True
        return nxt in '\n\r'


class LayoutNgReader(LayoutNdReader):
    """
    Strict layout_ng reader.
    """

    def __init__(self, namespace, nodefaults=False):
        super().__init__(namespace, nodefaults=nodefaults)
        self._commit_detector = HeaderCommitDetector()
        self._line_scanner = LogicalLineScanner(self)

    def read_header_until_terminator(self, stream, *, head_pos, head):
        """
        Read header argument forms until a structural header terminator ':'.

        Assumes the stream is positioned right after the already-consumed
        header head atom.
        """
        args, found_colon = self._read_header_args(stream)
        if not found_colon:
            raise stream.error('block header must end with colon', head_pos)
        return args

    def _is_structural_colon_after_consumed(self, stream):
        """
        Decide whether a header-terminator `:` is structural.

        Assumes the `:` has already been consumed and the stream is positioned
        right after it.
        """
        hot = stream.peek(2) or ''
        i = 0
        while i < len(hot) and hot[i] in ' \t':
            i += 1

        if i < len(hot):
            ch = hot[i]
            return ch == ';' or ch in '\n\r'

        # Undecidable from the hot peek window; fall back to a larger peek.
        ahead = stream.peek(2048) or ''
        j = 0
        while j < len(ahead) and ahead[j] in ' \t':
            j += 1
        if j >= len(ahead):
            return True
        ch = ahead[j]
        return ch == ';' or ch in '\n\r'

    def _is_structural_colon_at_next(self, stream):
        """
        Decide whether the next `:` is a structural header terminator.

        Assumes the current stream position begins with `:`.
        """
        hot = stream.peek(2) or ''
        if not hot or hot[0] != ':':
            return False

        i = 1
        while i < len(hot) and hot[i] in ' \t':
            i += 1
        if i < len(hot):
            ch = hot[i]
            return ch == ';' or ch in '\n\r'

        # Undecidable from the hot peek window; fall back.
        ahead = stream.peek(2048) or ''
        if not ahead or ahead[0] != ':':
            return False
        j = 1
        while j < len(ahead) and ahead[j] in ' \t':
            j += 1
        if j >= len(ahead):
            return True
        ch = ahead[j]
        return ch == ';' or ch in '\n\r'

    def read_form(self, stream):
        """
        Override LayoutNdReader.read_form with strict header-commit semantics.

        - At layout depth > 0: delegate to s-expression parsing.
        - At layout depth 0:
          - Parse one head atom.
          - If the head is a symbol and commit detector says it is intended
            as a header, parse header args until structural `:`.
          - If structural `:` is missing by logical EOL, raise a syntax error.
        """
        # Preserve nested s-expr parsing behaviour.
        if self._layout_depth > 0:
            return self._read(stream)

        while True:
            stream.skip_whitespace()
            if stream.dedented:
                return EOF, stream.position(), None

            position = stream.position()
            c = stream.peek(1)
            if not c:
                return EOF, position, None

            if c == ';':
                stream.read(1)
                stream.read_until_chars('\n')
                if stream.peek(1) == '\n':
                    stream.read(1)
                continue

            # Tolerate CRLF by consuming a stray leading '\r'.
            if c == '\r':
                stream.read(1)
                continue

            if c in '([{':
                return self._read(stream)

            outer_indent = position[1]

            # Preserve raw block macro behaviour by deferring to the parent
            # implementation. This keeps `'` and ` in sync with LayoutNdReader.
            if c in self._block_macros:
                next_c = stream.peek(2)[1:2]
                if next_c and next_c not in '([{' and self._has_block_colon_ahead(stream):
                    stream.read(1)
                    block_macro = self._block_macros[c]
                    result = block_macro(stream, outer_indent)
                    if is_pair(result):
                        result.set_position(position)
                    return VALUE, position, result

                return super()._read(stream)

            # Bare atom candidate: read it under header atom rules so that
            # keyword atoms like as:/x: are possible at delimiter depth 0.
            with self._header_mode():
                event, pos, form1 = super()._read(stream)
            if event is COLON:
                raise stream.error('block header must have a head', pos)
            if event is EOF:
                return EOF, pos, None

            header_indent = pos[1]

            # Strict commit decision: only if this head is a symbol and there
            # is any non-comment, non-ws, non-newline content on the logical
            # line following it.
            if is_symbol(form1) and self._commit_detector.should_commit(stream):
                args = self.read_header_until_terminator(
                    stream, head_pos=pos, head=form1)

                if self._block_mode:
                    block = self.read_block(stream, header_indent, form1, args)
                else:
                    block = self.read_layout_block(stream, header_indent, form1, args)
                return VALUE, position, block

            return VALUE, pos, form1


# The end.

