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
koskari.reader.layout

LayoutReader: the reader that supports layout-based block syntax. It
subclasses Reader as a layout coordinator, owning structural colon,
LINE_END header tails, and layout-only prefixes, and holds an inner
Reader with full s-expression defaults. Bridge macros on the
coordinator forward delimited reads into the inner reader so they do
not see layout rules; the one exception is an index bracket attached
to a value on a layout line, which the coordinator's own suffix macro
reads in header mode. Body reads require an IndentAwareStream.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import warnings
from contextlib import contextmanager
from functools import partial

from ..exceptions import LayoutError
from ..lib import Module, cons, nil, symbol
from ..types import get_position, is_nil, is_pair
from .layouts import apply_layout, apply_layouts, find_layout, is_block
from .pragma import IndentMismatch
from .parser import (
    BLOCK, BLOCKS, COLON, DOT, EOF, LINE, LINE_END, READ_EOF, VALUE, Reader,
    _keyword_block, _keyword_splice, _symbol_decorate, _symbol_quasiquote,
    _symbol_quasiquote_syntax, _symbol_quote, _symbol_quote_syntax,
    _symbol_unquote_splicing)
from .stream import IndentAwareStream


__all__ = (
    "LayoutReader",
)


# ===========================================================================
# LayoutReader
# ===========================================================================


class LayoutReader(Reader):
    """
    Reader that supports layout-based block syntax

    Subclasses Reader and adds indent-aware block reading. Resolves
    layout heads via namespace (layout/<head>). Requires a namespace
    for layout resolution, like the compiler.

    **Three default surfaces:** (1) ``self.inner`` is a plain :class:`Reader`
    with full s-expr defaults—balanced forms and prefix reads there use only
    inner state. (2) **Bridge** macros on this coordinator forward ``( ) [ ] {
    }`` ``"`` ``;`` ``f`` ``r`` ``b`` ``#`` into ``inner`` so delimiter and
    string/comment/prefix parsing matches s-expr mode; an index bracket
    attached to a value is the exception, read by this coordinator's own
    suffix macro in header mode. (3) **Layout-only**
    registrations: structural ``:``, ``_layout_macros`` (e.g. ``@``), and
    stacked ``'`` / backtick (predicated raw block then normal quote).

    Top-level layout reads are **line-oriented**: header-mode values on a
    logical line are collected until ``LINE_END`` or structural ``COLON``.
    Empty lines are skipped. Multiple values on one line fold to an implicit
    ``cons`` chain or raise per :attr:`layout_implicit_calls`. A structural
    colon ends the block header (``head`` plus ``args``); the body is read
    until dedent and passed to the layout handler (non-symbol heads use the
    ``layout/block`` fallback when no named layout matches). Structural ``:``
    rules still allow keyword atoms such as ``as:`` inside header arguments
    at delimiter depth 0.

    Stream interface contract: LayoutReader expects its stream arguments
    to be SourceStream instances (for pushback support). Body reads always
    use an IndentAwareStream so that dedent detection is available through
    the stream.dedented attribute rather than getattr guards.

    Subclasses may override _add_default_layout_macros to register or omit
    layout-mode-only prefix handlers (_layout_macros). Raw block quote and
    quasiquote use predicated reader macros on ``'`` and backtick when
    defaults are enabled.

    Layout header tails use LINE_END and COLON events with horizontal-only
    whitespace between tokens on a logical line.
    """

    def __init__(
            self,
            namespace,
            nodefaults=False,
            layout_enabled=True,
            layout_implicit_calls=True,
            bracket_pairs=True):
        """
        Initialize the layout reader

        :param namespace: Module, Namespace, or dict for layout lookup
        :param nodefaults: if True, do not add default macros and patterns
        :param layout_implicit_calls: if True, multiple values on one layout
            line fold to an implicit call; if False, more than one value is an
            error
        :param bracket_pairs: if True (default), ``[ ]`` reads into distinct
            bracket-pair / bracket-nil values; if False, as plain pairs
        """

        super().__init__(nodefaults=True, bracket_pairs=bracket_pairs)
        self._inner = Reader(nodefaults=True, bracket_pairs=bracket_pairs)

        from ..lib import ModuleNamespace, Namespace

        # a ModuleNamespace is a dict subclass, so the namespace kinds
        # are checked ahead of a plain dict of builtins
        if isinstance(namespace, Module):
            self._ns = vars(namespace)
        elif isinstance(namespace, (Namespace, ModuleNamespace)):
            self._ns = namespace
        elif isinstance(namespace, dict):
            self._ns = ModuleNamespace('layout', namespace)
        else:
            self._ns = namespace

        self._block_mode = False
        self._layout_macros = {}

        self.layout_enabled = layout_enabled
        self.layout_implicit_calls = layout_implicit_calls

        # raw default is off; reload_pragma raises it to the pragma
        # baseline (reader/decorators)
        self.layout_decorators = False

        # a line whose indent does not match its block is an error unless
        # the pragma (reader/indent) relaxes it
        self.layout_indent = IndentMismatch.ERROR

        if not nodefaults:
            self._add_default_macros()
            self._add_default_atoms()

        # this one describes a fundamental behavior of the reader, so it's not
        # a default macro, it's an always macro.
        self.add_layout_event_macro(
            ':', self._layout_colon_macro, True,
            predicate=self._layout_colon_predicate)

        self._header_scan = False


    @property
    def inner(self):
        """
        The dedicated s-expression reader used inside bridge macros.

        Attach s-expr-only extensions with ``reader.inner.add_event_macro``.
        """

        return self._inner


    def add_event_macro(
            self,
            char,
            macro_fn,
            terminating=False,
            predicate=None):

        self.add_layout_event_macro(char, macro_fn, terminating, predicate)
        self.add_inner_event_macro(char, macro_fn, terminating, predicate)


    def add_layout_event_macro(
            self,
            char,
            macro_fn,
            terminating=False,
            predicate=None):
        super().add_event_macro(char, macro_fn, terminating, predicate)


    def add_inner_event_macro(
            self,
            char,
            macro_fn,
            terminating=False,
            predicate=None):
        self._inner.add_event_macro(char, macro_fn, terminating, predicate)


    def _add_default_macros(self):
        """
        Register bridge macros that forward delimiter, string, comment, and
        prefix reads to :attr:`_inner`.
        """

        sm = self.add_event_macro
        inner = self._inner

        sm('(', partial(inner._read_pair, ')'), True)
        sm(')', inner._close_pair, True)

        sm('[', partial(inner._read_pair, ']'), True)
        sm(']', inner._close_pair, True)

        sm('{', partial(inner._read_thunk, '}'), True)
        sm('}', inner._close_pair, True)

        sm('"', inner._read_string, True)
        sm(';', inner._read_comment, True)

        sm('f', inner._read_fstring, False)
        sm('r', inner._read_rstring, False)
        sm('b', inner._read_bstring, False)

        sm('#', inner._read_collection, False)

        # the prefixes that read an operand register once per level, each
        # bound to its own reader: at the layout level a header operand
        # keeps its block colon, and inside parens and backticks the inner
        # reader's operand read sees the macros it installs for the span,
        # such as a backtick's unquote
        for reader, sm_level in ((self, self.add_layout_event_macro),
                                 (inner, self.add_inner_event_macro)):
            sm_level('&', reader._read_ref_call, False)
            for c in "-+~":
                sm_level(c, reader._read_unary, False,
                         predicate=reader._unary_enabled)
            sm_level('-', reader._read_datumq, False,
                     predicate=reader._datumq_follows)

        self.add_event_macro("'", inner._read_quote, True)
        self.add_event_macro("`", inner._read_quasiquote, True)

        self.add_layout_event_macro(
            "'",
            self._layout_quote_block_reader_macro,
            True,
            predicate=self._layout_block_quote_predicate)
        self.add_layout_event_macro(
            "`",
            self._layout_quasiquote_block_reader_macro,
            True,
            predicate=self._layout_block_quote_predicate)
        self.add_layout_event_macro(
            "#",
            self._layout_syntax_quote_block_reader_macro,
            False,
            predicate=self._layout_block_syntax_quote_predicate)

        # registered after both backtick macros so the triple-backtick
        # check wins on the coordinator and inner stacks alike; bound to
        # the inner reader so opener-line options read with s-expr rules
        self.add_event_macro(
            "`",
            inner._read_fence,
            True,
            predicate=inner._fence_predicate)

        # self.add_layout_event_macro("@", self._read_layout_decorate, False)
        self._layout_macros.update({
            "@": self._read_layout_decorate,
        })


    @contextmanager
    def temporary_event_macro(
            self, char, macro_fn, terminating=False, predicate=None):
        """
        Context manager to temporarily apply a character event macro

        Applies the triplet to both the layout coordinator (via the base
        implementation) and the inner s-expr reader for the duration, so
        delimited reads inside the block see it too. Each side removes only
        its own triplet on exit.

        :param char: the character(s) to trigger on
        :param macro_fn: function(stream, char) -> (event, value); may return
            (DEFER, None) like other event macros (see add_event_macro).
        :param terminating: if True, this character terminates atoms
        :param predicate: optional predicate (see :meth:`add_event_macro`)
        :yields: self
        """

        with super().temporary_event_macro(
                char, macro_fn, terminating, predicate), \
                self._inner.temporary_event_macro(
                    char, macro_fn, terminating, predicate):
            yield self


    def set_atom_pattern(self, namesym, match_fn, conversion_fn):
        self._inner.set_atom_pattern(namesym, match_fn, conversion_fn)


    def process_atom(self, atom):
        return self._inner.process_atom(atom)


    @classmethod
    def from_pragma(cls, namespace, pragma=None):
        """
        Build a reader from a module pragma (a default pragma when omitted),
        apply its reader settings, and subscribe for later pragma changes.
        """

        if pragma is None:
            from ..pragma import ModulePragma
            pragma = ModulePragma()

        inst = cls(namespace)
        inst.reload_pragma(pragma)
        pragma.watch(inst)
        return inst


    def set_layout_modes(
            self, *, layout_enabled=None, layout_implicit_calls=None,
            layout_decorators=None, layout_indent=None):
        """
        Update layout-mode feature switches on this reader.

        Intended as the stable hook for future pragma-driven reader reload.
        Any None parameter leaves the current setting unchanged.
        """

        if layout_enabled is not None:
            self.layout_enabled = bool(layout_enabled)
        if layout_implicit_calls is not None:
            self.layout_implicit_calls = bool(layout_implicit_calls)
        if layout_decorators is not None:
            self.layout_decorators = bool(layout_decorators)
        if layout_indent is not None:
            if not isinstance(layout_indent, IndentMismatch):
                layout_indent = IndentMismatch(layout_indent)
            self.layout_indent = layout_indent


    def reload_pragma(self, pragma):
        """
        Apply reader settings from a module pragma

        Extends :meth:`Reader.reload_pragma` with the layout switches
        (reader/layout, reader/implicit-calls, reader/decorators, and
        reader/indent) and mirrors every setting onto the inner
        s-expression reader, so both surfaces agree.

        :param pragma: a module pragma, or None for a no-op
        """

        if pragma is None:
            return

        super().reload_pragma(pragma)
        self._inner.reload_pragma(pragma)

        self.set_layout_modes(
            layout_enabled=pragma.get("reader/layout"),
            layout_implicit_calls=pragma.get("reader/implicit-calls"),
            layout_decorators=pragma.get("reader/decorators"),
            layout_indent=pragma.get("reader/indent"))


    def _check_indent(self, stream, position, expected):
        """
        Apply the reader/indent policy to a content line

        :param stream: the source stream, positioned at the line's content
        :param position: (line, col) of the content
        :param expected: the column the line must start at
        :raises ReaderSyntaxError: under the error policy
        """

        col = position[1]
        if col == expected or self.layout_indent is IndentMismatch.IGNORE:
            return

        if col > expected:
            message = 'unexpected indent'
        else:
            message = 'unindent does not match the enclosing block'

        if self.layout_indent is IndentMismatch.ERROR:
            raise stream.error(message, position)

        warnings.warn_explicit(
            message, SyntaxWarning, stream.filename or '<koskari>',
            position[0])


    @contextmanager
    def _header_mode(self):
        """
        Enable header-aware ``:`` predicate behavior for atom scans and
        dispatch.
        """
        old = self._header_scan
        self._header_scan = True
        try:
            yield
        finally:
            self._header_scan = old


    def _layout_colon_predicate(self, stream, c, atom_prefix):
        """
        Gate ``:`` dispatch and predicated atom boundaries for layout headers.

        When ``atom_prefix`` is None, this is leading-character dispatch.
        Otherwise it is an atom boundary before an unconsumed ``:``.

        :param stream: the source stream
        :param c: the ``:`` character
        :param atom_prefix: atom text before ``c``, or None at dispatch
        :return: True to run the macro or split the atom before ``:``
        """

        if not self._header_scan:
            if atom_prefix is None:
                return False
            return True
        if atom_prefix is None:
            return self._is_structural_colon_after_consumed(stream)
        return self._is_structural_colon_at_next(stream)


    def _layout_colon_macro(self, stream, c):
        """
        Emit a structural header COLON.

        :meth:`_layout_colon_predicate` ensures this runs only for that case
        at dispatch time; nested layout and non-header ``:`` skip the macro.
        """

        return COLON, symbol(':')


    def _layout_block_quote_predicate(self, stream, c, atom_prefix):
        """
        Allow layout raw-block quote and quasiquote when this line has a
        structural block colon and the head does not open with a delimiter.

        :param stream: source stream positioned after the prefix character
        :param c: the prefix character
        :param atom_prefix: atom boundary context; None at dispatch
        """

        if atom_prefix is not None:
            return True
        if not self.layout_enabled:
            return False
        nxt = stream.peek(1)
        if not nxt or nxt in '([{':
            return False
        return self._structural_colon_before_line_end(stream)


    def _layout_quote_block_reader_macro(self, stream, c):
        """
        Reader macro for layout raw-block quote.

        :param stream: positioned after ``'``
        :param c: the quote character
        """

        _lin, col = stream.position()
        outer_indent = col - 1
        return BLOCK, self._read_quote_block(stream, outer_indent)


    def _layout_quasiquote_block_reader_macro(self, stream, c):
        """
        Reader macro for layout raw-block quasiquote.

        :param stream: positioned after the backtick
        :param c: the backtick character
        """

        _lin, col = stream.position()
        outer_indent = col - 1
        return BLOCK, self._read_quasiquote_block(stream, outer_indent)


    def _layout_block_syntax_quote_predicate(self, stream, c, atom_prefix):
        """
        Allow layout raw-block quote-syntax and quasiquote-syntax, spelled
        ``#'`` and ``#```, under the same conditions as the plain block
        quotes: a structural block colon on the line and a head that does
        not open with a delimiter. ``#`` is a non-terminating macro
        character, so this runs at dispatch only and never at an atom
        boundary.

        :param stream: source stream positioned after the ``#``
        :param c: the ``#`` character
        :param atom_prefix: atom boundary context; always None here
        """

        if not self.layout_enabled:
            return False
        nxt = stream.peek(2)
        if len(nxt) < 2 or nxt[0] not in "'`" or nxt[1] in '([{':
            return False
        return self._structural_colon_before_line_end(stream)


    def _layout_syntax_quote_block_reader_macro(self, stream, c):
        """
        Reader macro for layout raw-block quote-syntax and
        quasiquote-syntax.

        :param stream: positioned after the ``#``
        :param c: the ``#`` character
        """

        prefix = stream.read(1)
        _lin, col = stream.position()
        outer_indent = col - 2
        if prefix == "'":
            return BLOCK, self._read_quote_block(
                stream, outer_indent, _symbol_quote_syntax)
        return BLOCK, self._read_quasiquote_block(
            stream, outer_indent, _symbol_quasiquote_syntax)


    @staticmethod
    def _rest_after_colon_is_structural(rest):
        """
        Apply the structural-colon rule to the text following a ``:``.

        A ``:`` is a structural header terminator when the remainder of its
        physical line, after horizontal whitespace, is empty (end of line) or
        the start of a comment.

        :param rest: line text after the ``:`` (no newline)
        :return: True if the colon is structural
        """

        rest = rest.lstrip(' \t')
        return rest == '' or rest[0] == ';'


    def _is_structural_colon_after_consumed(self, stream):
        """
        Decide whether a header-terminator `:` is structural.

        Assumes the `:` has already been consumed and the stream is positioned
        right after it.
        """

        return self._rest_after_colon_is_structural(stream.peek_line())


    def _is_structural_colon_at_next(self, stream):
        """
        Decide whether the next `:` is a structural header terminator.

        Assumes the current stream position begins with `:`.
        """

        line = stream.peek_line()
        if not line or line[0] != ':':
            return False
        return self._rest_after_colon_is_structural(line[1:])


    @property
    def namespace(self):
        """
        The namespace layout handlers resolve against
        """

        return self._ns


    def find_layout(self, name):
        return find_layout(self._ns, name)


    def fold_line(self, entry):
        """
        The form one body entry denotes

        A line is the proper list of the elements the reader saw on it.
        One element is that element; several fold to the call of the
        first, or raise when :attr:`layout_implicit_calls` is off. A
        block entry lowers through its layout to its form.

        :param entry: a line, a proper cons list of elements, never
            empty, or a :#block: cons
        :return: the entry's form
        :raises LayoutError: several elements with implicit calls off
        """

        if is_block(entry):
            return apply_layout(entry, self)

        line = entry
        head, rest = line
        if is_nil(rest):
            return head
        if not self.layout_implicit_calls:
            raise LayoutError(
                'multiple forms on one layout line', get_position(line))
        form = line.copy()
        pos = get_position(line)
        if pos:
            form.set_position(pos)
        return form


    def fold_lines(self, entries):
        """
        Fold every entry of a body with :meth:`fold_line`

        :param entries: proper cons list of lines and blocks
        :return: proper cons list of forms
        """

        if is_nil(entries):
            return nil
        return cons(
            *(self.fold_line(entry) for entry in entries.unpack()), nil)


    def _structural_colon_before_line_end(self, stream):
        """
        True if the current layout line contains a structural header colon.

        Speculatively scans from the stream position until a structural
        header ``:`` is found or the logical line ends (a newline or comment
        at delimiter depth 0), tracking delimiter depth. Newlines enclosed in
        brackets do not end the scan. The stream is left unchanged.

        :param stream: the source stream
        :return: whether a structural ``:`` exists on this line
        """

        with stream.speculate():
            depth = 0
            while True:
                stream.read_until_chars("([{)]}:;\r\n")
                ch = stream.read(1)
                if not ch:
                    return False
                if depth == 0 and ch in ';\r\n':
                    return False
                if ch in '([{':
                    depth += 1
                elif ch in ')]}':
                    depth -= 1
                elif ch == ':' and depth == 0:
                    if self._rest_after_colon_is_structural(stream.peek_line()):
                        return True


    def _read_header_form(self, stream):
        """
        Read one header token or report logical line end.

        The stream must be positioned after vertical whitespace at a token
        boundary.

        :param stream: the source stream
        :return: ``(LINE_END, pos, None)`` or the result of ``_read``
        """

        peek = stream.peek(1)
        if not peek or peek in '\n\r' or peek == ';':
            return LINE_END, stream.position(), None
        with self._header_mode():
            return super()._read(stream, vertical_ws=False)


    def _read_header_args(self, stream):
        """
        Read header argument forms until a structural COLON or end-of-line.
        """

        args = []
        while True:
            stream.skip_whitespace()
            if stream.dedented:
                return args, False

            event, pos, value = self._read_header_form(stream)
            if event is LINE_END:
                return args, False
            if event is COLON:
                return args, True
            if event is EOF:
                return args, False
            args.append(value)


    def read_block_header(self, stream):
        """
        Read the block head and arguments up to a structural header ':'.

        Uses header-mode atom parsing so peer heads like `else:` are read as
        symbols, not keyword atoms.

        :param stream: the source stream positioned at the first header token
        :return: (head, args, opener) where head is the first form, args are
            the remaining forms before the colon, opener is symbol(':')
        :raises ReaderSyntaxError: on EOF or missing colon
        """

        with self._header_mode():
            event, pos, head = super()._read(stream)

        if event is COLON:
            raise stream.error('block header must have a head', pos)
        if event is EOF:
            raise stream.reader_eof(opened_at=pos, context='block header')

        args, found_colon = self._read_header_args(stream)
        if not found_colon:
            raise stream.error('block header must end with colon', pos)

        return head, args, symbol(':')


    def _read_line_tail_forms(self, stream):
        """
        Read remaining top-level forms on the current logical line
        """

        forms = []
        while True:
            self._skip_horizontal_whitespace(stream)
            c = stream.peek(1)
            if not c or c in '\n\r;':
                return forms

            event, pos, value = self._read(stream, vertical_ws=False)
            if event is EOF:
                return forms
            if event is DOT:
                raise stream.error('invalid use of dot in layout line', pos)
            if event is not VALUE:
                raise stream.error('invalid syntax', pos)

            forms.append(value)


    def read_block_body(self, stream, outer_indent):
        """
        Read body elements until dedent or EOF

        stream must be an IndentAwareStream (guaranteed by LayoutReader.read
        wrapping the top-level stream, and by all internal callers using the
        same stream throughout). Temporarily installs outer_indent as the
        max_indent so that skip_whitespace stops and sets stream.dedented when
        the next content line is at or below that column. Restores the
        previous max_indent on exit.

        :param stream: an IndentAwareStream
        :param outer_indent: column that ends the block body on dedent
        :return: list of body entries: a line, the proper cons list of
            the elements the reader saw on it, or a nested :#block: cons
        """

        body = []
        body_indent = None

        try:
            with stream.indent_limit(outer_indent):
                while True:
                    stream.skip_whitespace()
                    if stream.dedented:
                        break
                    if not stream.peek(1):
                        break
                    position = stream.position()
                    if body_indent is None:
                        body_indent = position[1]
                    else:
                        self._check_indent(stream, position, body_indent)
                    event, pos, form = self.read_form(stream)
                    if event is EOF:
                        break
                    elif event is LINE:
                        body.append(form)
                    elif event is BLOCKS:
                        body.extend(form.unpack())
                    elif is_block(form):
                        body.append(form)
                    else:
                        # a decorated block or a value that filled the
                        # line on its own is that line's one element
                        body.append(self._one_element_line(form, pos))
        finally:
            stream.dedented = False

        return body


    @staticmethod
    def _one_element_line(form, position):
        """
        Wrap a block or form that occupied a whole line as a line
        """

        line = cons(form, nil)
        if position:
            line.set_position(position)
        return line


    def _peek_peer_head_symbol(self, stream):
        """
        Speculatively read the leading header token of the current line

        Reads one header-mode form and rewinds, so the stream is left
        unchanged. Used to test a candidate line against a layout's declared
        peer heads before committing to read it as a block. Reusing the real
        header read means peer heads are recognised exactly as ``read_block_header``
        would recognise them.

        :param stream: IndentAwareStream positioned at the candidate peer line
        :return: the head form (a symbol for declared peers), or None
        """

        with stream.speculate():
            with self._header_mode():
                event, _pos, head = super()._read(stream)

        return head if event is VALUE else None


    def _skip_comment_line(self, stream):
        """
        Consume a comment from its semicolon through the end of its line,
        and the newline after it when there is one

        :param stream: the source stream, positioned at the semicolon
        """

        stream.read(1)
        stream.read_until_chars('\n')
        if stream.peek(1) == '\n':
            stream.read(1)


    def _consume_through_blank_lines_to_peer_column(self, stream, outer_indent):
        """
        Consume the rest of the prior line (trailing whitespace and a trailing
        comment), then blank lines and comment-only lines, until the stream
        sits at the first content character on a line indented to
        outer_indent (the peer block header).

        :param stream: IndentAwareStream after the previous logical line's
            last form, anywhere before its newline
        :param outer_indent: column of the peer head line's leading spaces
        :return: True if a peer header line was reached, False otherwise
        """

        self._skip_horizontal_whitespace(stream)
        if stream.peek(1) == ';':
            self._skip_comment_line(stream)
        elif stream.peek(1) == '\n':
            stream.read(1)
        else:
            return False

        while True:
            indent = len(self._skip_horizontal_whitespace(stream))

            pk = stream.peek(1)
            if pk == '\n':
                stream.read(1)
                continue
            if pk == ';':
                self._skip_comment_line(stream)
                continue
            if not pk:
                return False
            if indent != outer_indent:
                return False
            return True


    def _collect_peer_blocks(self, stream, outer_indent, layout_obj):
        """
        Read any declared peer blocks following the primary block

        :param stream: IndentAwareStream after the primary block body
        :param outer_indent: column of the enclosing block head
        :param layout_obj: LayoutHandler whose peers attribute lists allowed
            peer head symbols
        :return: list of (:#block: ...) conses, one per peer block
        """

        declared = tuple(layout_obj.peers or ())
        if not declared:
            return []

        declared_set = {str(sym) for sym in declared}
        peers = []

        while True:
            checkpoint = stream.mark()

            if not self._consume_through_blank_lines_to_peer_column(
                    stream, outer_indent):
                stream.rewind(checkpoint)
                break

            head = self._peek_peer_head_symbol(stream)
            if head is None or str(head) not in declared_set:
                stream.rewind(checkpoint)
                break

            phead, pargs, _opener = self.read_block_header(stream)
            peers.append(self._build_block(stream, outer_indent, phead, pargs))

        return peers


    def _build_block(self, stream, outer_indent, head, args):
        """
        Build the raw :#block: cons structure: (:#block: (head . args) entry...)

        :param stream: source stream positioned after the block header colon
        :param outer_indent: column of the block head line
        :param head: the block head symbol
        :param args: list of forms between head and colon
        :return: (:#block: (head . args) entry...) cons with position set
        """

        pos = stream.position()
        body = self.read_block_body(stream, outer_indent)
        header = cons(head, *args, nil)
        result = cons(_keyword_block, header, *body, nil)
        if is_pair(result):
            result.set_position(pos)
        return result


    def read_block(self, stream, outer_indent, head=None, args=None):
        """
        Read a block as a :#block: cons without invoking layout handlers

        Used when :attr:`_block_mode` is set (quote/quasiquote inner blocks).
        Sets _block_mode for the duration of the body read so nested colons
        also produce raw :#block: conses.

        :param stream: the source stream, positioned after the header colon
            and its trailing whitespace
        :param outer_indent: column of the block head line
        :param head: block head symbol if already read by the caller
        :param args: header argument forms if already read by the caller
        :return: (:#block: (head . args) entry...) cons
        """

        if head is None:
            head, args, opener = self.read_block_header(stream)

        prev = self._block_mode
        self._block_mode = True
        try:
            return self._build_block(stream, outer_indent, head, args)
        finally:
            self._block_mode = prev


    def _read_block_with_peers(self, stream, outer_indent, head, args):
        """
        Read a primary :#block: and any stream-collected peer :#block: conses

        :param stream: source stream positioned after the header colon
        :param outer_indent: column of the block head line
        :param head: block head symbol
        :param args: header argument forms
        :return: (primary_block, peer_blocks) where peer_blocks may be empty
        """

        primary = self._build_block(stream, outer_indent, head, args)
        layout_obj = self.find_layout(head)
        peer_blocks = []
        if layout_obj and layout_obj.peers:
            peer_blocks = self._collect_peer_blocks(
                stream, outer_indent, layout_obj)
        return primary, peer_blocks


    def _read_quote_block(
            self, stream, outer_indent, quote_head=_symbol_quote):
        """
        Block-macro handler for the ' prefix

        Reads the block header and body, then wraps the raw :#block: cons in a
        synthetic (:#block: (quote) INNER) for layout/quote to unbox

        :param stream: the source stream, positioned after the ' character
        :param outer_indent: column of the block head line
        :return: synthetic (:#block: (quote) INNER) cons
        """

        head, args, _ = self.read_block_header(stream)
        raw = self.read_block(stream, outer_indent, head, args)
        return cons(_keyword_block,
                    cons(quote_head, nil),
                    raw,
                    nil)


    def _read_quasiquote_block(
            self, stream, outer_indent, quote_head=_symbol_quasiquote):
        """
        Block-macro handler for the backtick prefix

        Installs the , and ,@ unquote reader macros for the duration of
        the header and body read so that unquote sites are recognised.
        Wraps the raw :#block: cons in a synthetic (:#block: (quasiquote) INNER)

        :param stream: the source stream, positioned after the ` character
        :param outer_indent: column of the block head line
        :return: synthetic (:#block: (quasiquote) INNER) cons
        """

        with self.temporary_event_macro(",", self._read_unquote, True):
            head, args, _ = self.read_block_header(stream)
            raw = self.read_block(stream, outer_indent, head, args)
        return cons(_keyword_block,
                    cons(quote_head, nil),
                    raw,
                    nil)


    def _finish_header_line(self, values, first_value_pos):
        """
        Collect the values on one layout line into a LINE result

        Nothing is folded here: the line is handed on as the list of its
        elements, and :meth:`fold_line` applies the line rule wherever a
        form is wanted, in :meth:`read` for a top-level line and in the
        layout handlers for the lines of a body. Inside a quoted or
        quasiquoted block a line holding only an unquote-splicing form is
        returned as ``(:#splice: FORM)``, so the filled forms spread into
        lines of their own when the block is expanded.

        :param values: non-empty list of values collected on the line
        :param first_value_pos: position of the first value
        :return: (LINE, first_value_pos, the line as a proper cons list)
        """

        if self._block_mode and len(values) == 1 and self._is_splice(values[0]):
            # a deferred block keeps its lines as data until the quasiquote
            # fills them; a line that is only ,@FORMS splices lines, not
            # elements, so it is marked for apply_lines to spread
            line = cons(_keyword_splice, values[0], nil)
        else:
            line = cons(*values, nil)
        if first_value_pos:
            line.set_position(first_value_pos)
        return LINE, first_value_pos, line


    @staticmethod
    def _is_splice(form):
        return (form and is_pair(form)
                and form.car is _symbol_unquote_splicing)


    def _read_layout_decorate(self, stream, outer_indent):
        """
        Layout-macro handler for the @ prefix (leading decorator lines)

        The first ``@`` was consumed by read_form. Reads more ``@`` lines at
        the same column, then one layout form (block or s-expr), and wraps
        ``(compile/decorate (d1 d2 ...) INNER)``.

        :param stream: the source stream, positioned after the first ``@``
        :param outer_indent: column of the first ``@`` character
        :return: (compile/decorate (decorators...) INNER) cons
        """

        decorators = []
        while True:
            forms = self._read_line_tail_forms(stream)
            if not forms:
                raise stream.error('empty decorator line', stream.position())

            decorators.append(self.fold_line(cons(*forms, nil)))
            stream.skip_whitespace()
            pk = stream.peek(1)
            if not pk:
                raise stream.reader_eof(
                    stream.position(), context='after decorator')

            col = stream.position()[1]
            if col != outer_indent:
                raise stream.error(
                    'decorator lines must align with the decorated form',
                    stream.position())

            if pk != '@':
                break

            stream.read(1)

        event, pos, inner = self.read_form(stream)
        if event is LINE:
            inner = self.fold_line(inner)
        elif event is not BLOCK and event is not BLOCKS:
            raise stream.error('invalid syntax after decorators', pos)

        deco_list = cons(*decorators, nil)
        if event is BLOCKS:
            return cons(_symbol_decorate, deco_list, inner)
        else:
            return cons(_symbol_decorate, deco_list, inner, nil)


    def _layout_macro_enabled(self, c):
        """
        Whether the layout-only prefix macro for ``c`` is currently active.

        Gated here rather than by removing the table entry, so pragma
        settings toggle behavior while registration stays stable (the
        same reasoning as :meth:`Reader._dotted_attr_match`). ``@`` is
        governed by ``reader/decorators`` via :attr:`layout_decorators`.

        :param c: the dispatch character
        :return: True when the macro should fire
        """

        if c == '@':
            return self.layout_decorators
        return True


    def read_form(self, stream):
        """
        Read one layout form using a line-oriented header-mode loop

        When layout is disabled, delegates to :meth:`Reader._read` on
        :attr:`inner`.

        When layout is enabled: ``c`` in ``_layout_macros`` runs the layout-only
        prefix handler (e.g. ``@``), provided its gate is open (see
        :meth:`_layout_macro_enabled`). Otherwise collects header-mode values on
        the current logical line until ``LINE_END``, ``EOF``, or structural
        ``COLON`` (horizontal whitespace only between tokens). Empty lines are
        skipped. At ``LINE_END`` returns ``LINE`` with the elements as a
        proper list, unfolded. At ``COLON``, returns
        ``BLOCK`` for a single :#block: cons, or ``BLOCKS`` for a primary
        :#block: plus stream-collected peer :#block: conses on a spine.
        Expansion is deferred to :meth:`read`.

        :param stream: source stream positioned at the start of the next form
        :return: (event, position, value)
        """

        if not self.layout_enabled:
            return self._inner._read(stream)

        while True:
            stream.skip_whitespace()
            if stream.dedented:
                return EOF, stream.position(), None

            position = stream.position()
            c = stream.peek(1)
            if not c:
                return EOF, position, None

            if c == ';':
                self._skip_comment_line(stream)
                continue

            outer_indent = position[1]

            if c in self._layout_macros and self._layout_macro_enabled(c):
                stream.read(1)
                layout_macro = self._layout_macros[c]
                result = layout_macro(stream, outer_indent)
                if is_pair(result):
                    result.fill_position(position)
                return BLOCK, position, result

            values = []
            first_value_pos = None

            while True:
                self._skip_horizontal_whitespace(stream)
                if stream.dedented:
                    return EOF, stream.position(), None

                event, pos, value = self._read_header_form(stream)
                if event is BLOCK or event is BLOCKS:
                    return event, pos, value

                if event is LINE_END:
                    if not values:
                        break
                    return self._finish_header_line(values, first_value_pos)

                if event is COLON:
                    if not values:
                        raise stream.error(
                            'block header must have a head', pos)
                    head = values[0]
                    args = values[1:]
                    header_indent = first_value_pos[1]
                    if self._block_mode:
                        return BLOCK, position, self.read_block(
                            stream, header_indent, head, args)
                    primary, peer_blocks = self._read_block_with_peers(
                        stream, header_indent, head, args)
                    if peer_blocks:
                        return BLOCKS, position, cons(primary, *peer_blocks, nil)
                    return BLOCK, position, primary

                if event is EOF:
                    if not values:
                        return EOF, pos, None
                    return self._finish_header_line(values, first_value_pos)

                if event is DOT:
                    raise stream.error('invalid syntax', pos)

                if event is not VALUE:
                    raise stream.error('invalid syntax', pos)

                if first_value_pos is None:
                    first_value_pos = pos
                values.append(value)


    def read(self, reader_stream):
        """
        Read a single layout form from the stream

        The stream must be an IndentAwareStream, made once for the whole
        source with :meth:`IndentAwareStream.from_source`, so successive
        reads continue one position track and stream.dedented is always
        available to the layout read. A plain SourceStream is refused: a
        wrapper made per read would restart its line count at the
        stream's stale position on every call.

        A top-level form must start at column 0; :attr:`layout_indent`
        decides whether anything else is an error, a warning, or read
        as-is.

        At EOF returns READ_EOF (same as Reader.read), not None, so callers
        can distinguish end-of-stream from a parsed Python None literal.

        A ``LINE`` folds through :meth:`fold_line`; ``BLOCK`` values are
        expanded via :func:`apply_layout` and ``BLOCKS`` spines via
        :func:`apply_layouts` (returning the expanded car), each with this
        reader.

        :raises TypeError: when reader_stream is not an IndentAwareStream
        """

        if not isinstance(reader_stream, IndentAwareStream):
            raise TypeError(
                "LayoutReader.read needs an IndentAwareStream; make one"
                " with IndentAwareStream.from_source")

        event, pos, value = self.read_form(reader_stream)

        if event is not EOF and self.layout_enabled:
            self._check_indent(reader_stream, pos, 0)

        if event is LINE:
            return self.fold_line(value)
        elif event is BLOCK:
            return apply_layout(value, self)
        elif event is BLOCKS:
            return apply_layouts(value, self).car
        elif event is VALUE:
            return value
        elif event is EOF:
            return READ_EOF
        else:
            raise reader_stream.error('invalid syntax', pos)


# The end.
