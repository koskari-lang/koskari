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
koskari.reader.layout_nd

Indent/layout reader variant with context-sensitive block header terminator
handling.

In `LayoutReader`, `:` is treated as a terminating character at layout depth
0, which prevents keyword atoms (e.g. `as:` / `x:`) from forming at the
layout header depth.

`LayoutNdReader` implements the layout-header terminator rule instead:
`:` is structural only when, after it, only whitespace and a comment or
line-ending remain on that logical line.
"""

from contextlib import contextmanager

from ..types import is_pair
from .parser import ATOM, COLON, DOT, EOF, LayoutReader, VALUE, symbol


class LayoutNdReader(LayoutReader):
    """
    Layout reader with a structural `:` rule for block headers.

    Outside of header parsing, `:` behaves like in pure s-expression mode
    (it may be part of atoms/keyword atoms).

    During header parsing, when `:` is followed only by whitespace, a comment,
    or a line ending, it is treated as the structural header terminator and
    emitted as a `COLON` event.
    """

    def __init__(self, namespace, nodefaults=False):
        """
        Initialize a layout_nd reader.

        :param namespace: Module, Namespace, or dict for layout lookup
        :param nodefaults: if True, do not add default macros and patterns
        """
        super().__init__(namespace, nodefaults=nodefaults)

        # LayoutReader installs ':' as an interrupting terminating macro at depth
        # 0. We remove it, and instead emit COLON contextually while parsing
        # header forms.
        if ':' in self.reader_macros:
            self.clear_event_macro(':')

        self._header_scan = False

    @contextmanager
    def _header_mode(self):
        """
        Enable header terminator handling for `:` within atom scans.
        """
        old = self._header_scan
        self._header_scan = True
        try:
            yield
        finally:
            self._header_scan = old

    def _is_structural_colon_after_consumed(self, stream):
        """
        Decide whether a structural header-terminator `:` follows.

        Assumes ':' has already been consumed.
        """
        ahead = stream.peek(2048) or ''
        i = 0
        while i < len(ahead) and ahead[i] in ' \t':
            i += 1
        if i >= len(ahead):
            # No more characters in the peek window; assume EOF.
            return True
        ch = ahead[i]
        return ch == ';' or ch in '\n\r'

    def _is_structural_colon_at_next(self, stream):
        """
        Decide whether the next ':' is the structural header terminator.

        Assumes the current stream position begins with ':'.
        """
        ahead = stream.peek(2048) or ''
        if not ahead or ahead[0] != ':':
            return False
        i = 1
        while i < len(ahead) and ahead[i] in ' \t':
            i += 1
        if i >= len(ahead):
            return True
        ch = ahead[i]
        return ch == ';' or ch in '\n\r'

    def _has_block_colon_ahead(self, stream):
        """
        Probe ahead for a structural header-terminator ':'.

        Scans a limited window and tracks delimiter depth so ':' within
        parentheses/brackets/braces does not count.
        """
        ahead = stream.peek(2048) or ''
        depth = 0
        for i, ch in enumerate(ahead):
            if depth == 0 and ch in '\n\r;':
                return False
            if ch in '([{':
                depth += 1
            elif ch in ')]}':
                depth -= 1
            elif ch == ':' and depth == 0:
                if self._is_structural_colon_in_ahead(ahead, i):
                    return True
        return False

    def _is_structural_colon_in_ahead(self, ahead, colon_index):
        """
        Apply the structural rule to a ':' inside `ahead`.
        """
        j = colon_index + 1
        while j < len(ahead) and ahead[j] in ' \t':
            j += 1
        if j >= len(ahead):
            return True
        ch = ahead[j]
        return ch == ';' or ch in '\n\r'

    def _read_header_args(self, stream):
        """
        Read header argument forms until a structural COLON or end-of-line.
        """
        args = []
        while True:
            stream.skip_whitespace()
            if stream.dedented:
                return args, False

            peek = stream.peek(1)
            if not peek or peek in '\n\r':
                return args, False
            if peek == ';':
                return args, False

            with self._header_mode():
                event, pos, value = super()._read(stream)
            if event is COLON:
                return args, True
            if event is EOF:
                return args, False
            args.append(value)

    def read_block_header_nd(self, stream):
        """
        Read the block head and arguments up to a structural header ':'.
        """
        with self._header_mode():
            event, pos, head = super()._read(stream)
        if event is COLON:
            raise stream.error('block header must have a head', pos)
        if event is EOF:
            raise stream.error('unexpected EOF in block header', pos)

        args, found_colon = self._read_header_args(stream)
        if not found_colon:
            raise stream.error('block header must end with colon', pos)

        return head, args, symbol(':')

    def read_block_header(self, stream):
        """
        Override LayoutReader.read_block_header.
        """
        return self.read_block_header_nd(stream)

    def _read_default(self, stream, c):
        """
        Override atom scanning to support maybe-interrupting ':'.
        """
        # Only treat ':' as structural while parsing a header at layout depth 0.
        if self._header_scan and self._layout_depth == 0:
            if c == ':' and self._is_structural_colon_after_consumed(stream):
                return COLON, symbol(':')

            terms_with_colon = self._terms + ':'
            atom_parts = [c]
            while True:
                chunk = stream.read_until_chars(terms_with_colon)
                if chunk:
                    atom_parts.append(chunk)

                if stream.peek(1) != ':':
                    break

                if self._is_structural_colon_at_next(stream):
                    break

                # Non-structural ':' becomes part of this atom.
                stream.read(1)
                atom_parts.append(':')

            atom = ''.join(atom_parts)
            if atom == '.':
                return DOT, None
            return ATOM, atom

        return super()._read_default(stream, c)

    def read_form(self, stream):
        """
        Override LayoutReader.read_form to ensure header-mode is enabled
        when reading the candidate header head token.
        """
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

            # IndentAwareStream's dedent-bounded whitespace skipping only
            # treats '\n' as a line break; tolerate CRLF by consuming a
            # leading '\r' here.
            if c == '\r':
                stream.read(1)
                continue

            if c in '([{':
                return self._read(stream)

            outer_indent = position[1]

            # Case 2: block-macro prefix character (' or `).
            if c in self._block_macros:
                next_c = stream.peek(2)[1:2]
                if next_c and next_c not in '([{' and self._has_block_colon_ahead(stream):
                    stream.read(1)
                    block_macro = self._block_macros[c]
                    result = block_macro(stream, outer_indent)
                    if is_pair(result):
                        result.set_position(position)
                    return VALUE, position, result

                # Fall back to plain reader.
                return self._read(stream)

            # Case 3: bare atom, possibly followed by a colon block opener.
            with self._header_mode():
                event, pos, form1 = super()._read(stream)
            if event is COLON:
                raise stream.error('block header must have a head', pos)
            if event is EOF:
                return EOF, pos, None

            header_indent = pos[1]

            if self._has_block_colon_ahead(stream):
                args, found_colon = self._read_header_args(stream)
                if found_colon:
                    if self._block_mode:
                        block = self.read_block(stream, header_indent, form1, args)
                    else:
                        block = self.read_layout_block(stream, header_indent, form1, args)
                    return VALUE, position, block

            return VALUE, pos, form1


# The end.

