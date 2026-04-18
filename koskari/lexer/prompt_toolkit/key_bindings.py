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
koskari.lexer.prompt_toolkit.key_bindings

Key bindings for the Koskari REPL: Tab and Shift+Tab adjust the **entire**
current line's leading whitespace to 4-space tab stops. Tab only applies
when the cursor is in leading whitespace (so Tab still runs completion
elsewhere). Shift+Tab dedents from any cursor position on the line.
"""

# This file was entirely generated via prompt, as the basis for the
# prompt_toolkit REPL enhancements.


from prompt_toolkit.document import Document
from prompt_toolkit.filters import Condition
from prompt_toolkit.key_binding.key_bindings import KeyBindings
from prompt_toolkit.keys import Keys


TAB_WIDTH = 4


def _leading_ws_span_and_visual_cols(line, tab_width=TAB_WIDTH):
    """
    Scan leading spaces and tabs; return (character_span, visual_columns).

    Tabs advance the visual column to the next multiple of tab_width.
    """

    i = 0
    cols = 0
    while i < len(line):
        c = line[i]
        if c == ' ':
            cols += 1
            i += 1
        elif c == '\t':
            cols = (cols // tab_width + 1) * tab_width
            i += 1
        else:
            break
    return i, cols


def _line_start_index(doc):
    """Absolute buffer index of the start of the current line."""

    return doc.cursor_position + doc.get_start_of_line_position()


def _replace_line_leading_indent(buffer, new_cols):
    """
    Replace the current line's leading whitespace with new_cols spaces.
    Preserves cursor offset relative to content after the leading run.
    """

    doc = buffer.document
    old_cursor = buffer.cursor_position
    line_start = _line_start_index(doc)
    line = doc.current_line
    span, _old_cols = _leading_ws_span_and_visual_cols(line)
    new_prefix = ' ' * new_cols
    old_prefix_len = span
    new_text = (
        buffer.text[:line_start]
        + new_prefix
        + buffer.text[line_start + old_prefix_len:]
    )
    delta = len(new_prefix) - old_prefix_len
    new_cursor = old_cursor + delta
    buffer.document = Document(new_text, new_cursor)


def _whitespace_before_cursor():
    from prompt_toolkit.application import get_app
    doc = get_app().current_buffer.document
    before = doc.current_line_before_cursor
    return before == '' or before.isspace()


def load_koskari_key_bindings():
    """
    Return KeyBindings for the Koskari REPL.

    Tab moves the current line's leading indent to the **next** 4-space tab
    stop when the text before the cursor on the current line is
    whitespace-only; otherwise the default Tab (completion) runs.

    Shift+Tab moves the current line's leading indent to the **previous** tab
    stop (from any cursor position on that line).
    """

    kb = KeyBindings()

    @kb.add(
        Keys.Tab,
        filter=Condition(_whitespace_before_cursor),
        eager=True,
    )
    def _tab_indent_line(event):
        doc = event.current_buffer.document
        line = doc.current_line
        _span, cols = _leading_ws_span_and_visual_cols(line)
        next_cols = ((cols // TAB_WIDTH) + 1) * TAB_WIDTH
        _replace_line_leading_indent(event.current_buffer, next_cols)

    @kb.add(Keys.BackTab, eager=True)
    def _shift_tab_dedent_line(event):
        doc = event.current_buffer.document
        line = doc.current_line
        _span, cols = _leading_ws_span_and_visual_cols(line)
        prev_cols = max(0, ((cols - 1) // TAB_WIDTH) * TAB_WIDTH)
        _replace_line_leading_indent(event.current_buffer, prev_cols)

    return kb


__all__ = ('load_koskari_key_bindings',)


# The end.
