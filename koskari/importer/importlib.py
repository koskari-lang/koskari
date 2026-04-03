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
koskari.importer.importlib

Python importlib extensions for loading .ki Koskari source files.

Integrates with Python's import machinery by inserting KoskariPathFinder
into sys.meta_path.  This allows both Python and koskari code to import
.ki modules transparently.

KoskariPathFinder is inserted at the front of sys.meta_path so that it is
consulted before Python's default PathFinder.  This precedence is required
because Python's PathFinder will declare a directory a namespace package if
it contains no __init__.py, refusing to give us the chance to find
__init__.ki.  By going first we can find koskari packages and decline
gracefully on everything else.

The loader (KoskariSourceFileLoader) can use either run_sync(exec_module_async)
or exec_module_demand to satisfy the synchronous exec_module entry point
required by importlib.  Swap between them by changing the single return
line in exec_module.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


from importlib.abc import FileLoader
from importlib.machinery import FileFinder, PathFinder
from os import getcwd

from .module import (init_module, load_module_async, load_module_demand,
                     new_module, new_module_demand)


__all__ = (
    'SOURCE_SUFFIXES',
    'KoskariPathFinder',
    'KoskariSourceFileLoader',
    'KoskariFileFinder',
    'path_hooks',
    'path_importer_cache',
)


SOURCE_SUFFIXES = ('.ki',)

# Module-level path hook storage, kept separate from Python's own caches
# so we do not interfere with the standard PathFinder.
path_importer_cache = {}
path_hooks = []


class KoskariPathFinder(PathFinder):
    """
    A PathFinder subclass that hunts for .ki files on sys.path.

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
    FileLoader for .ki source files.

    exec_module can use either run_sync(exec_module_async) or exec_module_demand.
    Swap by changing the single return line in exec_module.
    """

    def create_module(self, spec):
        return None


    def get_source(self, fullname):
        return self.get_data(self.get_filename(fullname)).decode('utf-8')


    async def exec_module_async(self, module):
        filename = self.get_filename(module.__name__)

        ki_mod = await new_module(
            module.__name__,
            package=getattr(module, '__package__', None))

        init_module(ki_mod, filename, spec=getattr(module, '__spec__', None))

        await load_module_async(ki_mod)

        # Copy all non-dunder bindings from the koskari namespace back
        # to the Python module so that standard attribute access works.
        ki_ns = vars(ki_mod)
        for name in list(ki_ns.keys()):
            try:
                val = getattr(ki_mod, name)
                setattr(module, name, val)
            except AttributeError:
                pass


    def exec_module_demand(self, module):
        """
        Execute the module synchronously via the demand path (demand_sync).

        Same steps as exec_module_async but uses new_module_demand and
        load_module_demand so no event loop or await is required.
        """
        filename = self.get_filename(module.__name__)

        ki_mod = new_module_demand(
            module.__name__,
            package=getattr(module, '__package__', None))

        init_module(ki_mod, filename, spec=getattr(module, '__spec__', None))

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


# Pre-build the path hook so it is available even before install() is called.
path_hooks.append(
    KoskariFileFinder.path_hook((KoskariSourceFileLoader, SOURCE_SUFFIXES))
)


# The end.
