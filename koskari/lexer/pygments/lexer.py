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
koskari.lexer.pygments.lexer

Pygments Lexer implementation for Koskari source.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude Opus 4.6 via Cursor
"""


from ..discovery import special_names_for_repl
from ..highlight import pygments_token_type, token_start_index
from ..tokens import tokenize


def build_koskari_lexer_class():
    """
    Build and return the Pygments Lexer class for Koskari

    :return: Pygments Lexer subclass
    :raises ImportError: when Pygments is not installed
    """

    from pygments.lexer import Lexer
    from pygments.token import Text

    class KoskariPygmentsLexer(Lexer):
        """
        Pygments lexer backed by koskari.lexer.tokens.tokenize
        """

        name = 'Koskari'
        aliases = ['koskari', 'ki']
        filenames = ['*.ki']
        mimetypes = ['text/x-koskari']

        def __init__(self, special_names=None, **options):
            Lexer.__init__(self, **options)
            self._special_names = (
                special_names if special_names is not None
                else special_names_for_repl())

        def get_tokens_unprocessed(self, text):
            """
            Yield (index, token_type, value) triples for Pygments

            :param text: Koskari source string
            :yield: Pygments token triples covering the full text
            """

            pos = 0
            for tok in tokenize(
                    text, '<pygments>', special_names=self._special_names):
                start = token_start_index(text, tok)
                if start > pos:
                    yield pos, Text, text[pos:start]
                yield start, pygments_token_type(tok.type), tok.text
                pos = start + len(tok.text)
            if pos < len(text):
                yield pos, Text, text[pos:]

    return KoskariPygmentsLexer


__all__ = (
    'build_koskari_lexer_class',
)


# The end.
