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
koskari.importer.module

Module lifecycle for Koskari source files.

Provides new_module, init_module, load_module_async, and a parallel demand
chain (new_module_demand, load_module_demand) which drive execution via
demand_call instead of coroutines.  The loader may use either path; swapping
is a one-line change in exec_module.

Each top-level expression is read, compiled to a Code object, executed as a
koskari expression coroutine (async path) or via demand_call (demand path),
and run in sequence.  This allows each expression to mutate the module
namespace for subsequent expressions, e.g. by adding new bindings or
special forms.

This is the koskari analogue of sibilant.module from python-sibilant.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


import sys
from os.path import split

from .compile import Compiler
from .lib import Module, ModuleNamespace, demand_call
from .reader import READ_EOF, IndentAwareStream, LayoutReader, source_open


__all__ = (
    'MODULE_LOAD_FACET_NAMES',
    'attach_module_load_facets',
    'detach_module_load_facets',
    'new_module',
    'init_module',
    'load_module_async',
    'load_module_demand',
    'load_module_kio_async',
    'load_module_kio_demand',
    'new_module_demand',
)


MODULE_LOAD_FACET_NAMES = (
    '__compiler__',
    '__reader__',
)


async def new_module(name, package=None, system=False):
    """
    Create a new koskari Module with an initialised ModuleNamespace.

    The namespace receives a fresh builtins dict from make_builtins() and
    has the module name stored as '__name__'.

    :param name: fully-qualified module name string
    :param package: parent package name string, or None
    :return: koskari Module instance
    """

    from .bootstrap import make_builtins

    bltns = await make_builtins()
    ns = ModuleNamespace(name, bltns)
    module = Module(ns)

    ns['__name__'] = name
    if package is not None:
        ns['__package__'] = package

    if system:
        sys.modules[name] = module

    return module


def new_module_demand(name, package=None, system=False):
    """
    Create a new koskari Module with an initialised ModuleNamespace (demand path).

    Same as new_module but uses make_builtins_demand() so no event loop or
    await is required.  Used by the loader's exec_module_demand path.

    :param name: fully-qualified module name string
    :param package: parent package name string, or None
    :return: koskari Module instance
    """

    from .bootstrap import make_builtins_demand

    bltns = make_builtins_demand()
    ns = ModuleNamespace(name, bltns)
    module = Module(ns)

    ns['__name__'] = name
    if package is not None:
        ns['__package__'] = package

    if system:
        sys.modules[name] = module

    return module


def init_module(module, filename, spec=None):
    """
    Initialise a koskari Module with source file metadata.

    Sets __file__, and if the filename is an __init__.ki also sets __path__
    to the containing directory.  Stores __spec__ and __loader__ when
    provided via spec.

    :param module: koskari Module instance
    :param filename: absolute path to the .ki source file
    :param spec: importlib ModuleSpec, or None
    """

    ns = vars(module)

    ns['__file__'] = filename

    dirpart, basename = split(filename)
    if basename.startswith('__init__.'):
        ns['__path__'] = [dirpart]

    if spec is not None:
        ns['__spec__'] = spec
        if spec.loader is not None:
            ns['__loader__'] = spec.loader
        if spec.submodule_search_locations is not None:
            ns['__path__'] = list(spec.submodule_search_locations)


async def load_module_async(module, *, interactive=False):
    """
    Execute a koskari module by reading, compiling, and running each
    top-level expression in order.

    Each expression is compiled relative to the module, then executed as
    an expression coroutine and awaited.  Bindings created by one
    expression are visible to all subsequent expressions, supporting
    define, defsyntax, and reader macro registration at module load time.

    While loading, ``__compiler__`` and ``__reader__`` are bound on the
    module.  Unless ``interactive`` is True, they are removed when loading
    finishes (retain them for ongoing ``__main__``, e.g. CLI ``-i``).

    :param module: koskari Module instance with ``__file__`` set
    :param interactive: when False, detach load facets after the load loop
    """

    filename = getattr(module, '__file__', '<unknown>')
    ns = vars(module)

    with source_open(filename) as source_stream:

        compiler = Compiler(
            getattr(module, '__name__', '<module>'),
            module,
            filename=filename)

        reader = LayoutReader(module)
        attach_module_load_facets(module, compiler, reader)
        try:
            stream = IndentAwareStream(
                source_stream.stream,
                source_stream.filename,
                auto_skip_exec=False,
                line_no=source_stream.lin,
                col_no=source_stream.col,
                max_indent=None)

            while True:
                source_expr = reader.read(stream)
                if source_expr is READ_EOF:
                    break

                fragment = compiler.full_compile(source_expr)
                expr = fragment.as_expression(ns)
                await expr()
        finally:
            if not interactive:
                detach_module_load_facets(module)


def load_module_demand(module, *, interactive=False):
    """
    Execute a koskari module by reading, compiling, and running each
    top-level expression in order via demand_call.

    Same structure as load_module_async but each expression is run with
    demand_call(expr) instead of awaiting a coroutine.  No async
    suspension is allowed.  Used by the loader's exec_module_demand path.

    Load facet teardown matches :func:`load_module_async`.

    :param module: koskari Module instance with __file__ set
    :param interactive: when False, detach load facets after the load loop
    """

    filename = getattr(module, '__file__', '<unknown>')
    ns = vars(module)

    with source_open(filename) as source_stream:

        compiler = Compiler(
            getattr(module, '__name__', '<module>'),
            module,
            filename=filename)

        reader = LayoutReader(module)
        attach_module_load_facets(module, compiler, reader)
        try:
            stream = IndentAwareStream(
                source_stream.stream,
                source_stream.filename,
                auto_skip_exec=False,
                line_no=source_stream.lin,
                col_no=source_stream.col,
                max_indent=None)

            while True:
                source_expr = reader.read(stream)

                if source_expr is READ_EOF:
                    break

                fragment = compiler.full_compile(source_expr)
                expr = fragment.as_expression(ns)
                demand_call(expr)
        finally:
            if not interactive:
                detach_module_load_facets(module)


async def load_module_kio_async(module):
    """
    Execute a module from a precompiled .kio file.

    Each stored body is re-hardened and run as a top-level expression in
    order, matching the incremental structure of load_module_async.

    :param module: koskari Module with __file__ pointing at a .kio path
    """

    filename = getattr(module, '__file__', '<unknown>')
    ns = vars(module)

    from .serialize.codex import KioFile

    kio_file = KioFile.read(filename)
    for code in kio_file.codes:
        expr = code.expression(ns)
        await expr()


def load_module_kio_demand(module):
    """
    Execute a module from a precompiled .kio file via demand_call.

    Same ordering as load_module_kio_async but synchronous.

    :param module: koskari Module with __file__ set to a .kio path
    """

    filename = getattr(module, '__file__', '<unknown>')
    ns = vars(module)

    from .serialize.codex import KioFile

    kio_file = KioFile.read(filename)
    for code in kio_file.codes:
        demand_call(code.expression(ns))


# ---------------------------------------------------------------------------
# Load facets and namespace helpers
# ---------------------------------------------------------------------------


def attach_module_load_facets(module, compiler, reader):
    """
    Bind ``__compiler__`` and ``__reader__`` on ``module`` for load time.

    :param module: koskari Module (or object whose ``vars()`` is the ns)
    :param compiler: Compiler instance for this load
    :param reader: LayoutReader instance for this load
    """

    ns = vars(module)
    ns['__compiler__'] = compiler
    ns['__reader__'] = reader


def detach_module_load_facets(module):
    """
    Remove load-time ``__compiler__`` and ``__reader__`` bindings.

    Ignores names listed in :data:`MODULE_LOAD_FACET_NAMES` that are not
    bound.

    :param module: koskari Module (or object whose ``vars()`` is the ns)
    """

    ns = vars(module)
    for name in MODULE_LOAD_FACET_NAMES:
        del ns[name]


# The end.
