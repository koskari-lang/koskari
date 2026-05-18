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
koskari_importer

Top-level import hook for ``.ki`` and ``.kio`` modules.  Registers
``KoskariPathFinder`` on ``sys.meta_path`` without importing ``koskari.compile``
or ``koskari.lib`` at module load time; those load when a Koskari module is
first executed.

Also re-exported from ``koskari.importer`` and ``koskari.importer.importlib``
for compatibility.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Auto via Cursor
"""


import sys
from importlib.abc import FileLoader
from importlib.machinery import FileFinder, PathFinder
from os import getcwd
from os.path import exists


__all__ = (
    'KoskariPathFinder',
    'KoskariSourceFileLoader',
    'KoskariFileFinder',
    'install',
    'is_installed',
    'uninstall',
)


path_importer_cache = {}
path_hooks = []


class KoskariPathFinder(PathFinder):
    """
    A PathFinder subclass that hunts for .ki and .kio files on sys.path.

    Uses module-local path_hooks and path_importer_cache so that koskari
    finder instances do not pollute Python's own importer cache.
    """

    @classmethod
    def invalidate_caches(cls):
        for finder in path_importer_cache.values():
            if hasattr(finder, 'invalidate_caches'):
                finder.invalidate_caches()

    @classmethod
    def _path_hooks(cls, path):
        for hook in path_hooks:
            try:
                return hook(path)
            except ImportError:
                continue
        return None

    @classmethod
    def _path_importer_cache(cls, path):
        if path == '':
            try:
                path = getcwd()
            except FileNotFoundError:
                return None
        try:
            finder = path_importer_cache[path]
        except KeyError:
            finder = cls._path_hooks(path)
            path_importer_cache[path] = finder
        return finder


class KoskariSourceFileLoader(FileLoader):
    """
    FileLoader for .ki source files and precompiled .kio artifacts.

    exec_module uses exec_module_demand by default (demand path).
    """

    def create_module(self, spec):
        return None


    def get_source(self, fullname):
        path = self.get_filename(fullname)

        if path.endswith('.kio'):
            path = path[:-1]
            if not exists(path):
                return ''
        return self.get_data(path).decode('utf-8')


    async def exec_module_async(self, module):
        from koskari.module import (
            init_module,
            load_module_async,
            load_module_kio_async,
            new_module,
        )

        filename = self.get_filename(module.__name__)

        ki_mod = await new_module(
            module.__name__,
            package=getattr(module, '__package__', None))

        init_module(ki_mod, filename, spec=getattr(module, '__spec__', None))

        if filename.endswith('.kio'):
            await load_module_kio_async(ki_mod)
        else:
            await load_module_async(ki_mod)

        ki_ns = vars(ki_mod)
        for name in list(ki_ns.keys()):
            try:
                val = getattr(ki_mod, name)
                setattr(module, name, val)
            except AttributeError:
                pass


    def exec_module_demand(self, module):
        """
        Execute the module synchronously via the demand path (demand_call).

        Same steps as exec_module_async but uses new_module_demand and
        load_module_demand so no event loop or await is required.
        """

        from koskari.module import (
            init_module,
            load_module_demand,
            load_module_kio_demand,
            new_module_demand,
        )

        filename = self.get_filename(module.__name__)

        ki_mod = new_module_demand(
            module.__name__,
            package=getattr(module, '__package__', None))

        init_module(ki_mod, filename, spec=getattr(module, '__spec__', None))

        if filename.endswith('.kio'):
            load_module_kio_demand(ki_mod)
        else:
            load_module_demand(ki_mod)

        ki_ns = vars(ki_mod)
        for name in list(ki_ns.keys()):
            try:
                val = getattr(ki_mod, name)
                setattr(module, name, val)
            except AttributeError:
                pass


    def exec_module(self, module):
        return self.exec_module_demand(module)


class KoskariFileFinder(FileFinder):
    """
    A FileFinder that refuses to return namespace-package specs.

    Python's FileFinder can return a namespace spec when a directory
    contains no __init__ file, which would shadow our __init__.ki.
    We override find_spec to return None in that case, allowing the
    search to continue to the next entry on sys.meta_path.
    """

    def find_spec(self, fullname, target=None):
        result = super().find_spec(fullname, target)
        return result if (result and result.loader) else None


path_hooks.append(
    KoskariFileFinder.path_hook((KoskariSourceFileLoader, ('.kio', '.ki')))
)


def _importer_suppressed():
    from os import environ
    from sys import _xoptions as xoptions
    return (
        xoptions.get('KOSKARI_NOIMPORTER', '0') != '0'
        or environ.get('KOSKARI_NOIMPORTER', '0') != '0')


def is_installed():
    """
    Return True if KoskariPathFinder is currently in sys.meta_path.

    KoskariPathFinder is inserted as a class (like Python's own PathFinder),
    so we check for the class object itself, not instances of it.

    :return: bool
    """

    return KoskariPathFinder in sys.meta_path


def install():
    """
    Insert KoskariPathFinder at the front of sys.meta_path.

    KoskariPathFinder is inserted as a class object, following the same
    convention as Python's own PathFinder.  Calling install() when the
    finder is already present is a no-op.

    Suppressed when the environment variable KOSKARI_NOIMPORTER is set to a
    non-zero value, or when the -X KOSKARI_NOIMPORTER Python xoption is set.
    """

    if _importer_suppressed():
        return

    if is_installed():
        return

    sys.meta_path.insert(0, KoskariPathFinder)


def uninstall():
    """
    Remove KoskariPathFinder from sys.meta_path.

    Calling uninstall() when the finder is not present is a no-op.
    """

    sys.meta_path[:] = [
        f for f in sys.meta_path if f is not KoskariPathFinder
    ]


# The end.
