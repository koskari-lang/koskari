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
koskari.lexer.pygments

Optional Pygments integration for Koskari syntax highlighting.

Pygments is imported lazily when KoskariLexer or highlight helpers are
used. Importing this subpackage does not require Pygments to be installed.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude Opus 4.6 via Cursor
"""


_lexer_class = None


def _get_lexer_class():
    global _lexer_class
    if _lexer_class is None:
        from .lexer import build_koskari_lexer_class
        _lexer_class = build_koskari_lexer_class()
    return _lexer_class


def __getattr__(name):
    if name == 'KoskariLexer':
        return _get_lexer_class()
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")


def register_sphinx_lexer(app):
    """
    Register KoskariLexer with a Sphinx application

    :param app: Sphinx application instance
    :raises ImportError: when Pygments is not installed
    """

    app.add_lexer('koskari', _get_lexer_class())


def highlight_source(source, lexer=None, formatter=None):
    """
    Highlight Koskari source to a formatted string via Pygments

    :param source: Koskari source text
    :param lexer: optional Pygments lexer instance
    :param formatter: optional Pygments formatter instance
    :return: highlighted output string
    :raises ImportError: when Pygments is not installed
    """

    from pygments import highlight
    from pygments.formatters import HtmlFormatter

    if lexer is None:
        lexer = _get_lexer_class()()
    if formatter is None:
        formatter = HtmlFormatter()
    return highlight(source, lexer, formatter)


__all__ = (
    'KoskariLexer',
    'highlight_source',
    'register_sphinx_lexer',
)


# The end.
