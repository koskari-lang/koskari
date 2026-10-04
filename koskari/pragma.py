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
from typing import (
    Any, Callable, Dict, Iterator, Optional, Protocol, Sequence, Tuple, Union)
from warnings import warn
from weakref import WeakMethod, ref

from .lib import values


__all__ = (
    "ModulePragma",
    "Watcher",

    "lookup",
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


def lookup(name: str) -> Optional[Tuple[Callable[..., Any], Any]]:
    """
    Lookup a setting by name

    :return: the (setting, undefined value) pair, or None when no setting
      of that name is registered
    """

    return _REGISTRY.get(str(name))


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
    A read-only live view of the setting/undefined-value registry; later
    registrations show through it
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


class ReloadPragmaWatcher(Protocol):
    """
    A watcher for a pragma
    """

    def reload_pragma(self, pragma: "ModulePragma") -> None:
        ...


PragmaWatcher = Union[ReloadPragmaWatcher, Callable[["ModulePragma"], None]]


class _Held:
    """
    A strong hold on a watcher nothing else owns, with the same call
    shape as a weak reference
    """

    def __init__(self, watcher: PragmaWatcher):
        self.watcher = watcher

    def __call__(self) -> PragmaWatcher:
        return self.watcher

    def __hash__(self) -> int:
        return hash(self.watcher)

    def __eq__(self, other: Any) -> bool:
        return isinstance(other, _Held) and self.watcher == other.watcher


def _hold_watcher(watcher: PragmaWatcher) -> Callable[[], Any]:
    """
    How a pragma keeps hold of a watcher, by what the watcher is: an
    object offering reload_pragma is held weakly, so a reader or
    compiler is not kept alive by the pragma it reads; a bound method
    lives with its receiver; any other callable has no owner but the
    pragma and is held strongly

    :raises TypeError: when the watcher is not callable and has no
        reload_pragma method
    """

    if hasattr(watcher, "reload_pragma"):
        if not callable(watcher.reload_pragma):
            raise TypeError(
                f"Watcher {watcher} has a reload_pragma that is not callable")
        try:
            return ref(watcher)
        except TypeError:
            return _Held(watcher)

    if not callable(watcher):
        raise TypeError(
            f"Watcher {watcher} is not callable and does"
            " not have a reload_pragma method")

    if getattr(watcher, "__self__", None) is not None and hasattr(
            watcher, "__func__"):
        try:
            return WeakMethod(watcher)
        except TypeError:
            return _Held(watcher)

    return _Held(watcher)


class ModulePragma:
    """
    Module level Pragma settings
    """

    def __init__(
            self,
            settings: Optional[Dict[str, Any]] = None,
            watchers: Optional[Sequence[PragmaWatcher]] = None):

        stns = {key: val[1] for key, val in registry().items()}
        if settings:
            stns.update(settings)

        self._settings = MappingProxyType(stns)
        self._watchers = set(map(_hold_watcher, watchers)) if watchers else set()


    def get(self, name: str, default: Any = None) -> Any:
        """
        The value of a setting, or default when it is not present
        """

        return self._settings.get(str(name), default)


    def __getitem__(self, name: str) -> Any:
        """
        The value of a setting, or raise KeyError when it is not present
        """

        name = str(name)
        if name not in self._settings:
            raise KeyError(f"Unknown pragma setting {name}")
        return self._settings[name]


    def __iter__(self) -> Iterator[Tuple[str, Any]]:
        """
        Iterate over the settings and their values
        """

        return iter(self._settings.items())


    def __repr__(self):
        return f"<ModulePragma {self._settings}>"


    def __kos_repr__(self):
        elms = [f"[{key} {value}]" for key, value in self._settings.items()]
        txt = " ".join(elms)
        return f"(pragma {txt})"


    def __kos_help__(self, formatter) -> None:
        """
        Describe this pragma's current settings alongside every registered
        setting and its default.
        """

        reg = registry()

        formatter.paragraph(
            "Module-scoped reader and compiler feature settings.")

        current = formatter.section("Settings")
        for name in sorted(self._settings):
            value = self._settings[name]
            entry = reg.get(name)
            if entry is None:
                current.item(f"{name} = {value!r}  (unregistered)")
            elif value == entry[1]:
                current.item(f"{name} = {value!r}")
            else:
                current.item(f"{name} = {value!r}  (default {entry[1]!r})")

        unset = [name for name in sorted(reg) if name not in self._settings]
        if unset:
            missing = formatter.section("Registered (not set)")
            for name in unset:
                missing.item(f"{name}  (default {reg[name][1]!r})")


    def copy(self) -> "ModulePragma":
        """
        A new pragma with these settings and a fresh, empty watcher set
        """

        return ModulePragma(dict(self._settings))


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

        replacement = ModulePragma(collected)
        replacement._watchers = self._watchers

        # copy to avoid mutating while iterating over watchers; a hold
        # whose watcher has died is dropped here
        for held in tuple(self._watchers):
            watch = held()
            if watch is None:
                self._watchers.discard(held)
                continue
            if hasattr(watch, "reload_pragma"):
                watch = watch.reload_pragma

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

        self._watchers.add(_hold_watcher(watcher))


# The end.
