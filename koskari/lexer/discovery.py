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
koskari.lexer.discovery

Discover names of compile-time forms from a Koskari builtins mapping for
lexical highlighting (REPL and editors).

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude Opus 4.5 via Cursor
"""


_COMPILE_PREFIX = 'compile/'


__all__ = (
    'collect_special_like_names_from_builtins',
    'ensure_builtins_loaded',
    'special_names_for_repl',
)


def ensure_builtins_loaded():
    """
    Load the full Koskari builtins module (bootstrap plus basics.ki)

    Highlighting and name discovery must call this before reading the
    merged builtins environment. Importing ``koskari`` alone is not enough.
    """

    import koskari.builtins  # noqa: F401


def _names_for_special_binding(name):
    """
    Yield the binding key and, for compile/ specials, the head name without
    the ``compile/`` prefix so both forms highlight.
    """

    yield name
    if name.startswith(_COMPILE_PREFIX):
        yield name[len(_COMPILE_PREFIX):]


def collect_special_like_names_from_builtins(builtins_dict):
    """
    Return the set of builtin dict keys whose values are compiler specials
    or syntax-rules transformers.

    Walks the merged runtime builtins (bootstrap plus loaded basics) so new
    specials registered only in Python stay in sync, and user-defined
    ``define-syntax`` names from basics.ki are included when present.

    :param builtins_dict: mapping from string name to builtin value
    :return: frozenset of name strings (each ``compile/...`` special also
        contributes the unprefixed name)
    """

    from koskari.compile.compiler import Compiled

    names = set()
    for name, val in builtins_dict.items():
        if name.startswith('__'):
            continue
        if isinstance(val, Compiled):
            names.update(_names_for_special_binding(name))

    return frozenset(names)


def special_names_for_repl():
    """
    Return special-form names for the REPL using the full builtins environment.

    Loads builtins via ``import koskari.builtins`` then ``make_builtins_demand()``,
    then collects ``Compiled`` and ``Syntax`` bindings.

    :return: frozenset of name strings for REPL special-form highlighting
    """

    ensure_builtins_loaded()
    from koskari.bootstrap import make_builtins_demand

    return collect_special_like_names_from_builtins(make_builtins_demand())


# The end.
