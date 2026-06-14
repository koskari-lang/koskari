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


import sys
from importlib import metadata

from koskari.importer import import_module_async
from koskari.lib import Domain, drive_async_loop


__all__ = (
    'KOSKARI_SCRIPTS_GROUP',
    'hosted_run',
    'parse_entry_target',
    'resolve_koskari_script_value',
    'run_resolved_script',
    'run_target',
)


KOSKARI_SCRIPTS_GROUP = 'koskari_scripts'


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
        raise EntryTargetError("empty koskari_scripts entry value")

    if ':' not in value:
        raise EntryTargetError(
            f"koskari_scripts entry {value!r} must use"
            " module:callable form")

    module_name, callable_name = value.split(':', 1)
    module_name = module_name.strip()
    callable_name = callable_name.strip()

    if not module_name or not callable_name:
        raise EntryTargetError(
            f"koskari_scripts entry {value!r} must use"
            " module:callable form")

    if '.' in callable_name:
        raise EntryTargetError(
            f"nested callable paths are not supported in v1: {value!r}")

    return module_name, callable_name


def _koskari_script_entry_points(dist):
    """
    Yield koskari_scripts entry points declared on one distribution

    :param dist: importlib.metadata.Distribution instance
    """

    eps = dist.entry_points
    if hasattr(eps, 'select'):
        return eps.select(group=KOSKARI_SCRIPTS_GROUP)
    return [ep for ep in eps if ep.group == KOSKARI_SCRIPTS_GROUP]


def resolve_koskari_script_value(spec, name):
    """
    Resolve a koskari_scripts entry value from one installed distribution

    Reads packaging metadata only; does not load the target callable.

    :param spec: distribution spec string (distname==version)
    :param name: koskari_scripts entry name / installed command basename
    :return: entry point value string (module:callable)
    :raises EntryNotFoundError: when spec or entry is missing
    """

    dist_name, sep, _version = spec.partition('==')
    dist_name = dist_name.strip()
    if not dist_name:
        raise EntryNotFoundError(
            "invalid distribution spec for"
            f" koskari_scripts {name!r}: {spec!r}")

    try:
        dist = metadata.distribution(dist_name)
    except metadata.PackageNotFoundError:
        raise EntryNotFoundError(
            'no installed distribution %r for koskari_scripts command %r'
            % (dist_name, name))

    if sep and _version.strip():
        dist_version = dist.version
        if dist_version != _version.strip():
            raise EntryNotFoundError(
                'distribution %r version mismatch for koskari_scripts %r: '
                'expected %r, found %r'
                % (dist_name, name, _version.strip(), dist_version))

    matches = []
    for ep in _koskari_script_entry_points(dist):
        if ep.name == name:
            matches.append(ep)

    if not matches:
        raise EntryNotFoundError(
            'no koskari_scripts entry %r in distribution %r'
            % (name, dist_name))

    if len(matches) > 1:
        raise EntryNotFoundError(
            'multiple koskari_scripts entries for command %r in %r'
            % (name, dist_name))

    return matches[0].value


def _isawaitable(obj):
    """
    Return True when obj looks like an awaitable coroutine result
    """

    if obj is None or isinstance(obj, (str, bytes)):
        return False
    return hasattr(obj, '__await__')


async def _normalize_exit(result):
    """
    Convert a target return value into a process exit code

    :param result: return value from the entry callable, possibly awaitable
    :return: integer exit code
    """

    if _isawaitable(result):
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
        result = fn()
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


def run_resolved_script(spec, name, argv=None):
    """
    Run a koskari_scripts entry resolved from one distribution spec

    Installed command stubs embed spec and name at build time, then call
    this entry without scanning all installed packages.

    :param spec: distribution spec string (distname==version)
    :param name: koskari_scripts entry name / installed command basename
    :param argv: optional argument list excluding argv[0]; defaults to
        sys.argv[1:]
    :return: integer exit code
    :raises EntryNotFoundError: when metadata has no matching entry
    :raises EntryTargetError: when the entry value is unsupported
    """

    try:
        target = resolve_koskari_script_value(spec, name)
        module_name, callable_name = parse_entry_target(target)
        if argv is None:
            argv = sys.argv[1:]

        return drive_async_loop(
            hosted_run(module_name, callable_name, name, argv))

    except KeyboardInterrupt:
        print('', file=sys.stderr)
        return 130


# The end.
