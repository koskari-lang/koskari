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
"""


import sys
from importlib import metadata

from koskari.extras.setuptools.metadata import (
    KOSKARI_SCRIPTS_GROUP, EntryTargetError, parse_entry_target)
from koskari.importer import import_module_async
from koskari.lib import Domain, drive_async_loop


__all__ = (
    'EntryTargetError',
    'KOSKARI_SCRIPTS_GROUP',
    'hosted_run',
    'parse_entry_target',
    'resolve_koskari_script_value',
    'run_main_module',
    'run_resolved_script',
    'run_target',
)


class LauncherError(Exception):
    """
    Base error for launcher dispatch failures
    """


class EntryNotFoundError(LauncherError):
    """
    No koskari_scripts entry matches the requested script name
    """


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
            f"no installed distribution {dist_name!r} for "
            f"koskari_scripts command {name!r}")

    if sep and _version.strip():
        dist_version = dist.version
        if dist_version != _version.strip():
            raise EntryNotFoundError(
                f"distribution {dist_name!r} version mismatch for "
                f"koskari_scripts {name!r}: expected {_version.strip()}, "
                f"found {dist_version}")

    matches = []
    for ep in _koskari_script_entry_points(dist):
        if ep.name == name:
            matches.append(ep)

    if not matches:
        raise EntryNotFoundError(
            f"no koskari_scripts entry {name!r} in "
            f"distribution {dist_name!r}")

    if len(matches) > 1:
        raise EntryNotFoundError(
            "multiple koskari_scripts entries for "
            f"command {name!r} in {dist_name!r}")

    return matches[0].value


async def hosted_run(module_name, callable_name, script_name, argv):
    """
    Async bootstrap for a packaged koskari_scripts target

    :param module_name: dotted module name from entry metadata
    :param callable_name: attribute to invoke on the loaded module, or
        None for the module's own entry point, the function it holds
        as __entry__
    :param script_name: argv[0] for the hosted script
    :param argv: remaining command-line arguments
    :return: integer exit code
    :raises EntryTargetError: when callable_name is None and the module
        holds no entry point
    """

    sys.argv = [script_name] + list(argv)

    with Domain():
        mod = await import_module_async(module_name)
        if callable_name is None:
            fn = getattr(mod, '__entry__', None)
            if fn is None:
                raise EntryTargetError(
                    f"{module_name} holds no entry point; name a"
                    " callable as module:callable or declare one with"
                    " entry-point")
        else:
            fn = getattr(mod, callable_name)
        result = fn()
        if hasattr(result, '__await__'):
            result = await result
        return result


def run_target(target, argv=None, script_name=None):
    """
    Run a module or module:callable target under the koskari async host

    :param target: entry value string (module, or module:callable)
    :param argv: optional argument list excluding argv[0]; defaults to
        sys.argv[1:]
    :param script_name: argv[0] for the hosted script; defaults to the
        last segment of module_name
    :return: integer exit code
    :raises EntryTargetError: when target is malformed, or names a
        module with no entry point
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
    :raises EntryTargetError: when the entry value is malformed, or
        names a module with no entry point
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


def run_main_module(spec, name):
    """
    Load the Koskari module spec names as the module called name, and
    when that name is __main__ run its entry point and exit with the
    status. This is what the code object the import hook hands runpy
    does, so python -m and runpy.run_module run a Koskari module the
    way koskari -m does.

    The module is a koskari Module, since a Koskari namespace is not a
    plain dict. Under __main__ it is published in sys.modules before it
    loads, in place of the plain module runpy prepared: runpy has
    already captured the dict it executes the code object in, so the
    swap is safe, and import __main__ then finds the program. Under any
    other name sys.modules is left alone, as runpy.run_module leaves
    it, and the dict run_module returns is the shim's, not the module's
    namespace.

    :param spec: the ModuleSpec runpy found, bound as the module's
        __spec__
    :param name: the name the module runs under; __name__ in the
        executing namespace
    :raises SystemExit: under __main__, with the entry point's status,
        1 after rendering an exception the load or the entry raised, or
        130 after a KeyboardInterrupt
    """

    from koskari.importer import source_for_stale_kio
    from koskari.module import (
        init_module, load_module_async, load_module_kio_async, new_module)

    # the file to run: the spec's origin, unless that is a .kio fallen
    # behind its sibling source; the loader is not asked, since its
    # file access checks the module name against its own
    filename = spec.origin
    if filename.endswith('.kio'):
        source = source_for_stale_kio(filename)
        if source is not None:
            filename = source

    mod = new_module(name, package=spec.parent)
    init_module(mod, filename, spec=spec)
    if name == '__main__':
        sys.modules[name] = mod

    async def load_and_enter():
        with Domain():
            if filename.endswith('.kio'):
                await load_module_kio_async(mod)
            else:
                await load_module_async(mod)
            if name != '__main__':
                return None
            entry = vars(mod).get('__entry__')
            if entry is None:
                return 0
            result = entry()
            if hasattr(result, '__await__'):
                result = await result
            return result

    if name != '__main__':
        drive_async_loop(load_and_enter())
        return

    try:
        status = drive_async_loop(load_and_enter())
    except KeyboardInterrupt:
        print('', file=sys.stderr)
        sys.exit(130)
    except Exception as e:
        from koskari.exceptions import (
            apply_sys_last_exc, format_run_exception)
        lines, tb = format_run_exception(e)
        apply_sys_last_exc(type(e), e, tb)
        sys.stderr.write(''.join(lines))
        sys.exit(1)
    sys.exit(status)


# The end.
