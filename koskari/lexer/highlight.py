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
koskari.lexer.highlight

Shared token-kind to style mappings for syntax highlighting adapters.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


KIND_TO_PROMPT_TOOLKIT = {
    'symbol': 'class:koskari.symbol',
    'keyword': 'class:koskari.keyword',
    'ellipsis': 'class:koskari.ellipsis',
    'py_singleton': 'class:koskari.py_singleton',
    'string': 'class:koskari.string',
    'number': 'class:koskari.number',
    'comment': 'class:koskari.comment',
    'special': 'class:koskari.special',
    'layout': 'class:koskari.layout',
    'paren_open': 'class:koskari.punctuation',
    'paren_close': 'class:koskari.punctuation',
    'bracket_open': 'class:koskari.punctuation',
    'bracket_close': 'class:koskari.punctuation',
    'brace_open': 'class:koskari.punctuation',
    'brace_close': 'class:koskari.punctuation',
    'collection_open': 'class:koskari.punctuation',
    'dot': 'class:koskari.punctuation',
    'comma': 'class:koskari.punctuation',
    'colon': 'class:koskari.punctuation',
    'quote': 'class:koskari.punctuation',
    'quasiquote': 'class:koskari.punctuation',
    'unquote_splicing': 'class:koskari.punctuation',
}


def prompt_toolkit_style_class(tok_type):
    """
    Map a tokenize() type string to a prompt_toolkit style class name

    :param tok_type: token type from tokenize()
    :return: prompt_toolkit style class string
    """

    return KIND_TO_PROMPT_TOOLKIT.get(tok_type, 'class:koskari.symbol')


def pygments_token_type(tok_type):
    """
    Map a tokenize() type string to a Pygments token type

    Pygments is imported lazily on first call.

    :param tok_type: token type from tokenize()
    :return: Pygments token type object
    :raises ImportError: when Pygments is not installed
    """

    from pygments.token import (
        Comment, Keyword, Name, Number, Punctuation, String, Text)

    mapping = {
        'symbol': Name,
        'keyword': Keyword,
        'ellipsis': Name.Constant,
        'py_singleton': Name.Constant,
        'string': String,
        'number': Number,
        'comment': Comment,
        'special': Name.Builtin,
        'layout': Keyword,
        'paren_open': Punctuation,
        'paren_close': Punctuation,
        'bracket_open': Punctuation,
        'bracket_close': Punctuation,
        'brace_open': Punctuation,
        'brace_close': Punctuation,
        'collection_open': Punctuation,
        'dot': Punctuation,
        'comma': Punctuation,
        'colon': Punctuation,
        'quote': Punctuation,
        'quasiquote': Punctuation,
        'unquote_splicing': Punctuation,
    }
    return mapping.get(tok_type, Text)


__all__ = (
    'KIND_TO_PROMPT_TOOLKIT',
    'prompt_toolkit_style_class',
    'pygments_token_type',
)


# The end.
