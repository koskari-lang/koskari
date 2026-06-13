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
koskari.launcher

Runtime host for koskari_scripts installed commands and programmatic entry
dispatch. Uses drive_async_loop and async module loading so targets run under
Domain and the koskari asyncio host without per-app Python shims.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


import inspect
import sys
from importlib import metadata
from os.path import basename

from koskari.importer import import_module_async
from koskari.lib import Domain, drive_async_loop


KOSKARI_SCRIPTS_GROUP = 'koskari_scripts'
DISPATCH_TARGET = 'koskari.launcher:dispatch'


__all__ = (
    'DISPATCH_TARGET',
    'KOSKARI_SCRIPTS_GROUP',
    'dispatch',
    'hosted_run',
    'parse_entry_target',
    'run_entry',
    'run_target',
    'script_name_from_argv',
)


class LauncherError(Exception):
    """
    Base error for launcher dispatch failures
    """


class EntryNotFoundError(LauncherError):
    """
    No koskari_scripts entry matches the requested script name
    """


class EntryTargetError(LauncherError):
    """
    Entry point value is not a supported module:callable target
    """


def script_name_from_argv(argv=None):
    """
    Derive the installed script basename from argv[0]

    Strips setuptools wrapper suffixes so dispatch can match entry point names.

    :param argv: argument list; defaults to sys.argv when None
    :return: script name suitable for koskari_scripts lookup
    """

    if argv is None:
        argv = sys.argv

    name = basename(argv[0].replace('\\', '/'))
    if name.endswith('-script.py'):
        name = name[:-len('-script.py')]
    if name.endswith('.exe'):
        name = name[:-len('.exe')]
    return name


def parse_entry_target(value):
    """
    Parse a koskari_scripts entry value into module and callable names

    v1 supports module:callable only. Bare script-module targets are rejected.

    :param value: entry point value string from packaging metadata
    :return: (module_name, callable_name) pair
    :raises EntryTargetError: when value is not module:callable form
    """

    value = value.strip()
    if not value:
        raise EntryTargetError('empty koskari_scripts entry value')

    if ':' not in value:
        raise EntryTargetError(
            'koskari_scripts entry %r must use module:callable form'
            % (value,))

    module_name, callable_name = value.split(':', 1)
    module_name = module_name.strip()
    callable_name = callable_name.strip()

    if not module_name or not callable_name:
        raise EntryTargetError(
            'koskari_scripts entry %r must use module:callable form'
            % (value,))

    if '.' in callable_name:
        raise EntryTargetError(
            'nested callable paths are not supported in v1: %r'
            % (value,))

    return module_name, callable_name


def _resolve_koskari_script_entry(name):
    """
    Look up a koskari_scripts entry point by script name

    :param name: console script basename
    :return: entry point value string (module:callable)
    :raises EntryNotFoundError: when no matching entry exists
    """

    matches = []
    for ep in metadata.entry_points(group=KOSKARI_SCRIPTS_GROUP):
        if ep.name == name:
            matches.append(ep)

    if not matches:
        raise EntryNotFoundError(
            'no koskari_scripts entry for command %r' % (name,))

    if len(matches) > 1:
        raise EntryNotFoundError(
            'multiple koskari_scripts entries for command %r' % (name,))

    return matches[0].value


def _invoke_entry_callable(fn, argv):
    """
    Call an entry callable with argv when its signature accepts it

    :param fn: callable loaded from the target module
    :param argv: sys.argv-style list for the script
    :return: callable result, possibly awaitable
    """

    try:
        sig = inspect.signature(fn)
    except (TypeError, ValueError):
        sig = None

    if sig is None:
        try:
            return fn(argv)
        except TypeError:
            return fn()

    params = list(sig.parameters.values())
    if not params:
        return fn()
    return fn(argv)


async def _normalize_exit(result):
    """
    Convert a target return value into a process exit code

    :param result: return value from the entry callable, possibly awaitable
    :return: integer exit code
    """

    if inspect.isawaitable(result):
        result = await result

    if result is None:
        return 0
    if isinstance(result, bool):
        return 0 if result else 1
    if isinstance(result, int):
        return result
    return 0


async def hosted_run(module_name, callable_name, script_name, argv):
    """
    Async bootstrap for a packaged koskari_scripts target

    :param module_name: dotted module name from entry metadata
    :param callable_name: attribute to invoke on the loaded module
    :param script_name: argv[0] for the hosted script
    :param argv: remaining command-line arguments
    :return: integer exit code
    """

    sys.argv = [script_name] + list(argv)

    with Domain():
        mod = await import_module_async(module_name)
        fn = getattr(mod, callable_name)
        result = _invoke_entry_callable(fn, sys.argv)
        return await _normalize_exit(result)


def run_target(target, argv=None, script_name=None):
    """
    Run a module:callable target under the koskari async host

    :param target: entry value string (module:callable)
    :param argv: optional argument list excluding argv[0]; defaults to
        sys.argv[1:]
    :param script_name: argv[0] for the hosted script; defaults to the
        last segment of module_name
    :return: integer exit code
    :raises EntryTargetError: when target is not module:callable form
    """

    module_name, callable_name = parse_entry_target(target)
    if argv is None:
        argv = sys.argv[1:]
    if script_name is None:
        script_name = module_name.rsplit('.', 1)[-1]

    return drive_async_loop(
        hosted_run(module_name, callable_name, script_name, argv))


def run_entry(name, argv=None):
    """
    Dispatch a koskari_scripts entry by installed command name

    :param name: script basename matching koskari_scripts metadata
    :param argv: optional argument list excluding argv[0]; defaults to
        sys.argv[1:]
    :return: integer exit code
    :raises EntryNotFoundError: when metadata has no matching entry
    :raises EntryTargetError: when the entry value is unsupported
    """

    target = _resolve_koskari_script_entry(name)
    module_name, callable_name = parse_entry_target(target)
    if argv is None:
        argv = sys.argv[1:]

    return drive_async_loop(
        hosted_run(module_name, callable_name, name, argv))


def dispatch():
    """
    console_scripts entry for mirrored koskari_scripts commands

    Resolves the real target from koskari_scripts metadata using the
    installed script basename, then runs it under drive_async_loop.

    :return: integer exit code
    """

    try:
        return run_entry(script_name_from_argv())

    except KeyboardInterrupt:
        print('', file=sys.stderr)
        return 130


# The end.
