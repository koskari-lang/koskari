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
koskari.extras.sphinx

Optional Sphinx integration for Koskari documentation builds.

Sphinx is imported only from setup(app) and from submodules when the
extension is loaded. Importing this package does not require Sphinx.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Composer via Cursor
"""


def _preload_builtins():
    """
    Materialize koskari.builtins for autodoc when the extension loads
    """

    try:
        import koskari.builtins  # noqa: F401
    except ImportError:
        pass


def _register_object_types(app):
    """
    Register Koskari-specific Python domain object types
    """

    app.add_object_type(
        'kosyntax',
        'kosyntax',
        objname='syntax form',
        indextemplate='pair: %s; syntax form')
    app.add_object_type(
        'kolayout',
        'kolayout',
        objname='layout handler',
        indextemplate='pair: %s; layout handler')


def setup(app):
    """
    Sphinx extension entry: autodocumenters, lexer, builtins preload

    :param app: Sphinx application instance
    :return: extension metadata dict
    """

    _preload_builtins()
    _register_object_types(app)

    from .autodoc import register_autodocumenters
    register_autodocumenters(app)

    from .builtins_directive import KosBuiltinsDirective
    app.add_directive('kos-builtins', KosBuiltinsDirective)

    from koskari.extras.pygments import register_sphinx_lexer
    register_sphinx_lexer(app)

    try:
        from koskari._version import __version__ as version
    except ImportError:
        version = '0.0.0'

    return {
        'version': version,
        'parallel_read_safe': True,
        'parallel_write_safe': True,
    }


__all__ = (
    'setup',
)


# The end.
