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
koskari.importer

The koskari import system. Implements the import hook and loader for ki and
kio files via Python's importlib machinery.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import importlib.util
import sys
from importlib.machinery import (
    BYTECODE_SUFFIXES, EXTENSION_SUFFIXES, SOURCE_SUFFIXES,
    ExtensionFileLoader, FileFinder, SourceFileLoader,
    SourcelessFileLoader)
from os import stat
from os.path import basename, exists


__all__ = (
    'KoskariSourceFileLoader',
    'import_module_async',
    'install',
    'source_for_stale_kio',
    'is_installed',
    'uninstall',
)


def source_for_stale_kio(kio_path):
    """
    The sibling .ki to load in place of ``kio_path``, or None when the
    .kio stands.

    A .kio with no sibling source stands. Beside a source it stands only
    when its stamp matches the source's current mtime and size; an
    unstamped, unreadable, or wrong-ABI .kio yields to the source.

    :param kio_path: path ending in .kio
    :return: the .ki path, or None
    """

    ki_path = kio_path[:-1]
    if not exists(ki_path):
        return None

    from koskari.serialize.codex import KioFile

    try:
        meta = KioFile.read_meta(kio_path)
        st = stat(ki_path)
    except (OSError, ValueError, KeyError):
        return ki_path

    if meta.source_mtime_ns != st.st_mtime_ns \
            or meta.source_size != st.st_size:
        return ki_path
    return None


class KoskariSourceFileLoader(SourceFileLoader):
    """
    Loader for .ki source files and precompiled .kio artifacts, on the
    machinery's source loader for its file access; the base is also
    the abc's FileLoader, which this module does not import, since
    importlib.abc pulls in importlib.resources and seventy modules
    behind it at every interpreter start the .pth triggers.

    exec_module uses exec_module_demand by default (demand path).
    """

    def create_module(self, spec):
        """
        The koskari Module the module's code runs in is the module
        importlib places in sys.modules: what the body defines, what
        its functions rebind, what Python assigns and what a circular
        import reads are one namespace
        """

        from koskari.module import new_module

        return new_module(spec.name, package=spec.parent)


    def _filename_to_load(self, fullname):
        """
        The file to execute: the finder's pick, unless that is a .kio
        that has fallen behind its sibling .ki
        """

        filename = self.get_filename(fullname)
        if filename.endswith('.kio'):
            source = source_for_stale_kio(filename)
            if source is not None:
                return source
        return filename


    def get_code(self, fullname):
        """
        The code object runpy executes for python -m and run_module: a
        Python shim that loads this Koskari module under the executing
        name through the launcher and, under __main__, runs its entry
        point and exits with the status. The import path never calls
        this; exec_module loads the module directly
        """

        return compile(
            "from koskari.launcher import run_main_module\n"
            "run_main_module(__spec__, __name__)\n",
            f"<koskari entry shim for {fullname}>", 'exec')


    def is_package(self, fullname):
        stem = basename(self.get_filename(fullname)).rsplit('.', 1)[0]
        return stem == '__init__'


    def get_source(self, fullname):
        path = self.get_filename(fullname)

        if path.endswith('.kio'):
            path = path[:-1]
            if not exists(path):
                return ''
        return self.get_data(path).decode('utf-8')


    def load_target(self, module):
        """
        Initialise the module from its spec and name the file to load
        """

        from koskari.module import init_module

        filename = self._filename_to_load(module.__name__)
        init_module(module, filename, spec=getattr(module, '__spec__', None))
        return filename


    async def exec_module_async(self, module):
        from koskari.module import load_module_async, load_module_kio_async

        filename = self.load_target(module)

        if filename.endswith('.kio'):
            await load_module_kio_async(module)
        else:
            await load_module_async(module)


    def exec_module_demand(self, module):
        """
        Execute the module synchronously via the demand path (demand_call).

        Same steps as exec_module_async but uses load_module_demand so no
        event loop or await is required.
        """

        from koskari.module import load_module_demand, load_module_kio_demand

        filename = self.load_target(module)

        if filename.endswith('.kio'):
            load_module_kio_demand(module)
        else:
            load_module_demand(module)


    def exec_module(self, module):
        return self.exec_module_demand(module)


def _directory_hook():
    """
    The path hook install registers: a FileFinder for a directory that
    knows Python's own loaders and then the Koskari one, so a directory
    is served by one finder and a module name resolves by Python's
    rules. sys.path order decides between directories, builtins and
    frozen modules come first, and within a directory Python's suffixes
    are tried before .kio and then .ki.
    """

    return FileFinder.path_hook(
        (ExtensionFileLoader, EXTENSION_SUFFIXES),
        (SourceFileLoader, SOURCE_SUFFIXES),
        (SourcelessFileLoader, BYTECODE_SUFFIXES),
        (KoskariSourceFileLoader, ['.kio', '.ki']))


_hook = None


def _python_directory_hook_index():
    """
    The position of Python's own FileFinder hook in sys.path_hooks, or
    the end when it is absent
    """

    for i, hook in enumerate(sys.path_hooks):
        if getattr(hook, '__name__', '') == 'path_hook_for_FileFinder':
            return i
    return len(sys.path_hooks)


def _importer_suppressed():
    from os import environ
    from sys import _xoptions as xoptions
    return (
        xoptions.get('KOSKARI_NOIMPORTER', '0') != '0'
        or environ.get('KOSKARI_NOIMPORTER', '0') != '0')


def is_installed():
    """
    Return True if the Koskari directory hook is in sys.path_hooks.

    :return: bool
    """

    return _hook is not None and _hook in sys.path_hooks


def install():
    """
    Register the Koskari directory hook on sys.path_hooks, just ahead
    of Python's own FileFinder hook, and drop the cached per-directory
    finders so every directory is re-resolved through it. Calling
    install() when the hook is already present is a no-op.

    Suppressed when the environment variable KOSKARI_NOIMPORTER is set to a
    non-zero value, or when the -X KOSKARI_NOIMPORTER Python xoption is set.
    """

    global _hook

    if _importer_suppressed():
        return

    if is_installed():
        return

    if _hook is None:
        _hook = _directory_hook()

    sys.path_hooks.insert(_python_directory_hook_index(), _hook)
    sys.path_importer_cache.clear()


def uninstall():
    """
    Remove the Koskari directory hook from sys.path_hooks and drop the
    cached per-directory finders built through it.

    Calling uninstall() when the hook is not present is a no-op.
    """

    if not is_installed():
        return

    sys.path_hooks[:] = [h for h in sys.path_hooks if h is not _hook]
    sys.path_importer_cache.clear()


async def import_module_async(fullname):
    """
    Import fullname under a running event loop using the async loader path

    When the module loader provides exec_module_async (koskari .ki/.kio
    sources), that path is used so module load may suspend on await. Pure
    Python modules fall back to exec_module.

    :param fullname: dotted module name to import
    :return: loaded module object
    :raises ModuleNotFoundError: when find_spec returns None
    :raises ImportError: when the spec has no loader
    """

    if fullname in sys.modules:
        return sys.modules[fullname]

    spec = importlib.util.find_spec(fullname)
    if spec is None:
        raise ModuleNotFoundError(fullname)

    module = importlib.util.module_from_spec(spec)
    if spec.loader is None:
        raise ImportError(f"missing loader for {fullname!r}")

    # as importlib._bootstrap._load_unlocked does: the module is visible
    # in sys.modules while its body runs, and gone again if the body
    # fails, so a later import tries again instead of getting the
    # half-loaded module
    loader = spec.loader
    sys.modules[fullname] = module
    try:
        if hasattr(loader, 'exec_module_async'):
            await loader.exec_module_async(module)
        else:
            loader.exec_module(module)
    except BaseException:
        sys.modules.pop(fullname, None)
        raise

    # the module in sys.modules is the one to bind, in case the loader
    # replaced it
    module = sys.modules[fullname]

    # mirror importlib._bootstrap._find_and_load_unlocked: bind the freshly
    # loaded submodule as an attribute on its parent package, so that later
    # dotted access (and cache-hit re-imports) resolve it.
    parent, _, child = fullname.rpartition('.')
    if parent:
        parent_module = sys.modules.get(parent)
        if parent_module is not None:
            try:
                setattr(parent_module, child, module)
            except AttributeError:
                pass

    return module


# The end.
