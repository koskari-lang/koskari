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
demand_sync instead of coroutines.  The loader may use either path; swapping
is a one-line change in exec_module.

Each top-level expression is read, compiled to a Code object, executed as a
koskari expression coroutine (async path) or via demand_sync (demand path),
and run in sequence.  This allows each expression to mutate the module
namespace for subsequent expressions, e.g. by adding new bindings or
special forms.

This is the koskari analogue of sibilant.module from python-sibilant.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


from os.path import split

from ..compile import Compiler
from ..lib import Module, ModuleNamespace, demand_sync
from ..reader import (
    IndentAwareStream,
    LayoutReader,
    READ_EOF,
    source_open,
)


__all__ = (
    'new_module',
    'init_module',
    'load_module_async',
    'load_module_demand',
    'new_module_demand',
)


async def new_module(name, package=None):
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

    _ns_store(ns, '__name__', name)
    if package is not None:
        _ns_store(ns, '__package__', package)

    return module


def new_module_demand(name, package=None):
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

    _ns_store(ns, '__name__', name)
    if package is not None:
        _ns_store(ns, '__package__', package)

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

    _ns_store(ns, '__file__', filename)

    dirpart, basename = split(filename)
    if basename.startswith('__init__.'):
        _ns_store(ns, '__path__', [dirpart])

    if spec is not None:
        _ns_store(ns, '__spec__', spec)
        if spec.loader is not None:
            _ns_store(ns, '__loader__', spec.loader)
        if spec.submodule_search_locations is not None:
            _ns_store(ns, '__path__', list(spec.submodule_search_locations))


async def load_module_async(module):
    """
    Execute a koskari module by reading, compiling, and running each
    top-level expression in order.

    Each expression is compiled relative to the module, then executed as
    an expression coroutine and awaited.  Bindings created by one
    expression are visible to all subsequent expressions, supporting
    define, defsyntax, and reader macro registration at module load time.

    :param module: koskari Module instance with __file__ and __stream__ set
    """

    filename = getattr(module, '__file__', '<unknown>')
    ns = vars(module)

    with source_open(filename) as source_stream:

        compiler = Compiler(
            getattr(module, '__name__', '<module>'),
            module,
            filename=filename)

        reader = LayoutReader(module)
        stream = IndentAwareStream(
            source_stream.stream,
            source_stream.filename,
            auto_skip_exec=False,
            line_no=source_stream.lin,
            col_no=source_stream.col,
            max_indent=None)

        while True:
            source_expr = reader.read(stream)
            # print(f"source_expr: {source_expr}")

            if source_expr is READ_EOF:
                break

            fragment = compiler.full_compile(source_expr)
            expr = fragment.as_expression(ns)
            coro = expr()
            await coro


def load_module_demand(module):
    """
    Execute a koskari module by reading, compiling, and running each
    top-level expression in order via demand_sync.

    Same structure as load_module_async but each expression is run with
    demand_sync(expr) instead of awaiting a coroutine.  No async
    suspension is allowed.  Used by the loader's exec_module_demand path.

    :param module: koskari Module instance with __file__ set
    """

    filename = getattr(module, '__file__', '<unknown>')
    ns = vars(module)

    with source_open(filename) as source_stream:

        compiler = Compiler(
            getattr(module, '__name__', '<module>'),
            module,
            filename=filename)

        reader = LayoutReader(module)
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
            demand_sync(expr)


# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------


def _ns_store(ns, name, value):
    """
    Store name -> value in a namespace or dict without creating a closure ref.

    Handles both ModuleNamespace (which uses .store()) and plain dicts
    (used in tests).

    :param ns: ModuleNamespace or dict
    :param name: str name to bind
    :param value: value to store
    """

    if hasattr(ns, 'store'):
        ns.store(name, value)
    else:
        ns[name] = value


# The end.
