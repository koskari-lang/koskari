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
koskari.lexer.prompt_toolkit

prompt_toolkit integration for Koskari REPL.

Provides KoskariLexer, KoskariCompleter, and koskari_style for syntax
highlighting and Tab completion. Requires prompt_toolkit.
"""


from .completer import KoskariCompleter
from .key_bindings import load_koskari_key_bindings
from .lexer import KoskariLexer, koskari_style


__all__ = (
    'KoskariCompleter',
    'KoskariLexer',
    'koskari_style',
    'load_koskari_key_bindings',
)


# The end.
