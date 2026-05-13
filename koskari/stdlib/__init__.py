# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.


"""
koskari.stdlib

Registration hooks for names merged into the Koskari builtins dict.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Composer via Cursor
"""


from typing import Any, Dict
from ..types import TypePredicate


BUILTINS: Dict[str, Any] = {}


def builtin(name: str, *aliases: str):
    """
    Register the decorated object under explicit stdlib string keys

    Each name must be the bootstrap string for that binding (no inference).

    :param names: one or more builtin name strings
    :return: decorator that registers ``obj`` and returns it unchanged
    :raises TypeError: when ``names`` is empty or any entry is not a ``str``
    """

    def decorator(obj):
        BUILTINS[name] = obj
        for alias in aliases:
            BUILTINS[alias] = obj
        return obj

    return decorator


def add_builtin(name: str, obj: Any):
    BUILTINS[name] = obj


def add_builtin_type(name: str, typeobj: Any):
    add_builtin(name, typeobj)
    add_builtin(name + '?', TypePredicate(name + '?', typeobj))


# The end.
