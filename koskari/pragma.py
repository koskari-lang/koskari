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
koskari.pragma

Module pragma object: an immutable mapping of setting name to value, plus a
mutable watcher set shared across a module's pragma lineage. Rebuilding a
pragma produces a new immutable copy and announces it to the watchers.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


from types import MappingProxyType
from typing import (Any, Callable, Dict, Iterator, Optional, Sequence,
                    Tuple)
from warnings import warn

from .lib import values


__all__ = (
    "ModulePragma",
    "Watcher",

    "register",
    "registry",
    "pragma_setting"
)


# Registry of available settings mapped to their undefined value -- the value
# a setting holds when it is not explicitly called. Used to build a pragma
# from scratch.
_REGISTRY: Dict[str, Tuple[Callable[..., Any], Any]] = {}


def register(
        name: str,
        settingfn: Callable[..., Any],
        default: Any) -> None:

    """
    Register a setting and the value it holds when not explicitly called
    """

    _REGISTRY[str(name)] = (settingfn, default)


def pragma_setting(name: str, default: Any = None) -> Callable[..., Any]:
    def decorator(fn: Callable[..., Any]) -> Callable[..., Any]:
        register(name, fn, default)
        return fn

    return decorator


def unregister(name: str) -> None:
    """
    Unregister a setting
    """

    del _REGISTRY[str(name)]


def registry() -> MappingProxyType[str, Tuple[Callable[..., Any], Any]]:
    """
    A copy of the current setting/undefined-value registry
    """

    return MappingProxyType(_REGISTRY)


# A watcher is any callable invoked with the newly active pragma.
Watcher = Callable[["ModulePragma"], None]


@pragma_setting("pragma/strict-settings", True)
def _pragma_strict_settings(strict: bool = True) -> bool:
    return strict


@pragma_setting("pragma/strict-watchers", True)
def _pragma_strict_watchers(strict: bool = True) -> bool:
    return strict


class ModulePragma:
    """
    Module level Pragma settings
    """

    def __init__(
            self,
            settings: Optional[Dict[str, Any]] = None,
            watchers: Optional[Sequence[Watcher]] = None):

        stns = {key: val[1] for key, val in registry().items()}
        if settings:
            stns.update(settings)

        self._settings = MappingProxyType(stns)
        self._watchers = set(watchers) if watchers else set()


    def get(self, name: str, default: Any = None) -> Any:
        """
        The value of a setting, or default when it is not present
        """

        return self._settings.get(str(name), default)


    def __iter__(self) -> Iterator[Tuple[str, Any]]:
        """
        Iterate over the settings and their values
        """

        return iter(self._settings.items())


    def rebuild(
            self,
            *changes: Tuple[str, values]) -> "ModulePragma":
        """
        Produce a new pragma with the given (name, value) changes applied,
        sharing this pragma's watcher set, and announce it to those watchers
        """

        strict_settings = self.get("pragma/strict-settings", True)
        strict_watchers = self.get("pragma/strict-watchers", True)

        collected = dict(self._settings)
        for name, valobj in changes:
            found = _REGISTRY.get(str(name))

            if not found:
                if strict_settings:
                    raise ValueError(f"Unknown pragma setting {name}")
                warn(f"Unknown pragma setting {name}")
                continue

            collected[str(name)] = valobj(found[0])

        replacement = ModulePragma(collected, self._watchers)

        # copy to avoid mutating while iterating over watchers
        for watch in tuple(self._watchers):
            try:
                watch(replacement)
            except Exception as e:
                if strict_watchers:
                    raise
                warn(f"Error while notifying pragma watcher {watch}: {e}")

        return replacement


    def watch(self, watcher: Watcher) -> None:
        """
        Subscribe a watcher to be announced on rebuild
        """

        self._watchers.add(watcher)


    def unwatch(self, watcher: Watcher) -> None:
        """
        Remove a previously subscribed watcher
        """

        self._watchers.discard(watcher)


# The end.
