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
:ai-assistant: Claude Opus 4.5 via Cursor
"""

# This file was entirely generated via prompt, as the basis for the
# prompt_toolkit REPL enhancements.


from .parser import default_layout_reader, parse, parse_all
from .tokens import (
    Token,
    tokenize,
    SPECIAL_NAMES,
    LAYOUT_NAMES,
    LAYOUT_HEAD_NAMES,
)


__all__ = (
    'default_layout_reader',
    'parse',
    'parse_all',
    'Token',
    'tokenize',
    'SPECIAL_NAMES',
    'LAYOUT_NAMES',
    'LAYOUT_HEAD_NAMES',
)


# The end.
