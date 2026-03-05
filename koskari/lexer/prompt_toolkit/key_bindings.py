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

Key bindings for Koskari REPL: Tab inserts 2-space indent when at line start.
"""


from prompt_toolkit.filters import Condition
from prompt_toolkit.key_binding.key_bindings import KeyBindings
from prompt_toolkit.keys import Keys


def _whitespace_before_cursor():
    from prompt_toolkit.application import get_app
    doc = get_app().current_buffer.document
    before = doc.current_line_before_cursor
    return before == '' or before.isspace()


def load_koskari_key_bindings():
    """
    Return KeyBindings for the Koskari REPL.

    Tab inserts 2 spaces when the text before the cursor on the current
    line is whitespace-only.  Otherwise the default Tab (completion) runs.
    """

    kb = KeyBindings()

    @kb.add(Keys.Tab, filter=Condition(_whitespace_before_cursor))
    def _insert_indent(event):
        event.current_buffer.insert_text('  ')

    return kb


__all__ = ('load_koskari_key_bindings',)


# The end.
