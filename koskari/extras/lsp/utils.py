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
koskari.extras.lsp.utils

Position conversion and document utilities for LSP handlers.
"""

# This file was entirely generated via prompt, as the basis for the
# prompt_toolkit LSP enhancements.


def koskari_to_lsp_range(location, line_content=None):
    """
    Convert Koskari 1-based (line, col) to LSP Range.

    :param location: (line, col) 1-based, or None for line 0 char 0
    :param line_content: optional full line string for end character
    :return: lsprotocol.types.Range
    """

    from lsprotocol.types import Position, Range

    if location is None:
        return Range(start=Position(line=0, character=0),
                     end=Position(line=0, character=0))

    line, col = location
    line_0 = max(0, line - 1)
    char_0 = max(0, col - 1)

    if line_content is not None:
        end_char = min(len(line_content), char_0 + 1)
    else:
        end_char = char_0 + 1

    return Range(
        start=Position(line=line_0, character=char_0),
        end=Position(line=line_0, character=end_char))


def get_word_at_position(document, position):
    """
    Extract the symbol/word at the given LSP position for completion prefix.

    :param document: TextDocument or object with .source and .lines
    :param position: LSP Position (0-based line, character)
    :return: str, the word being typed (prefix for completion)
    """

    try:
        line = document.lines[position.line]
    except IndexError:
        return ''

    char = position.character
    if char < 0 or char > len(line):
        return ''

    start = char
    while start > 0 and _is_symbol_char(line[start - 1]):
        start -= 1

    end = char
    while end < len(line) and _is_symbol_char(line[end]):
        end += 1

    return line[start:end]


def _is_symbol_char(c):
    return c.isalnum() or c in '-_./:'


__all__ = (
    'koskari_to_lsp_range',
    'get_word_at_position',
)


# The end.
