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

Provides new_module, init_module, and load_module_async which together
implement the read-compile-execute loop for .ki source files.  Each
top-level expression is read, compiled to a Code object, executed as a
koskari expression coroutine, and awaited in sequence.  This allows
each expression to mutate the module namespace for subsequent expressions,
e.g. by adding new bindings or special forms.

This is the koskari analogue of sibilant.module from python-sibilant.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


from os.path import split


__all__ = (
    'new_module',
    'init_module',
    'load_module_async',
)


def new_module(name, package=None):
    """
    Create a new koskari Module with an initialised ModuleNamespace.

    The namespace receives a fresh builtins dict from make_builtins() and
    has the module name stored as '__name__'.

    :param name: fully-qualified module name string
    :param package: parent package name string, or None
    :return: koskari Module instance
    """

    from ..lib import Module, ModuleNamespace
    from .builtins import make_builtins

    ns = ModuleNamespace(name, make_builtins())
    module = Module(ns)

    _ns_store(ns, '__name__', name)
    if package is not None:
        _ns_store(ns, '__package__', package)

    return module


def init_module(module, filename, spec=None):
    """
    Initialise a koskari Module with source file metadata and register
    compiler specials into its namespace.

    Sets __file__, and if the filename is an __init__.ki also sets __path__
    to the containing directory.  Stores __spec__ and __loader__ when
    provided via spec.  Registers all compiler special forms so that the
    module's namespace can be used directly with the koskari Compiler.

    :param module: koskari Module instance
    :param filename: absolute path to the .ki source file
    :param spec: importlib ModuleSpec, or None
    """

    from ..compile.specials import SPECIALS

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

    # Register all compiler specials so the Compiler can find them
    for sym, fn in SPECIALS.items():
        ns.store(str(sym), fn)


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

    from ..compile import Compiler
    from ..reader import default_reader, source_open

    filename = getattr(module, '__file__', '<unknown>')
    ns = vars(module)

    with source_open(filename) as stream:
        compiler = Compiler(getattr(module, '__name__', '<module>'),
                            module,
                            filename=filename)

        reader = default_reader

        while True:
            source_expr = reader.read(stream)
            if source_expr is None:
                break

            fragment = compiler.full_compile(source_expr)
            expr = fragment.as_expression(ns)
            coro = expr()
            await coro


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
