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
koskari.lexer

Lexical-only parser and tokenizer for Koskari source.

Provides read-only parsing (no compile/eval) and token emission
for editor integration (syntax highlighting, completion).

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

# This file was entirely generated via prompt, as the basis for the
# prompt_toolkit REPL enhancements.


from .discovery import (
    collect_special_like_names_from_builtins,
    special_names_for_repl,
)
from .parser import READ_EOF, default_layout_reader, parse, parse_all
from .tokens import (
    Token,
    tokenize,
    SPECIAL_NAMES,
    LAYOUT_NAMES,
    LAYOUT_HEAD_NAMES,
    LAYOUT_PEER_NAMES,
    LAYOUT_BLOCK_NAMES,
)


__all__ = (
    'collect_special_like_names_from_builtins',
    'READ_EOF',
    'default_layout_reader',
    'parse',
    'parse_all',
    'special_names_for_repl',
    'Token',
    'tokenize',
    'SPECIAL_NAMES',
    'LAYOUT_NAMES',
    'LAYOUT_HEAD_NAMES',
    'LAYOUT_PEER_NAMES',
    'LAYOUT_BLOCK_NAMES',
)


# The end.
