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

Provides new_module, init_module, load_module_async, and load_module_demand.
new_module is synchronous and copies parent bindings from koskari.builtins.
Module body loading may use the async or demand execution path.

Each top-level expression is read, compiled to a Code object, executed as a
koskari expression coroutine (async path) or via demand_call (demand path),
and run in sequence.  This allows each expression to mutate the module
namespace for subsequent expressions, e.g. by adding new bindings or
special forms.

This is the koskari analogue of sibilant.module from python-sibilant.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import sys
from contextlib import contextmanager
from os.path import split

from .compile import Compiler, CompilerMode
from .lib import Module, ModuleNamespace, cons, demand_call, nil, symbol
from .pragma import ModulePragma
from .reader import (
    READ_EOF, IndentAwareStream, LayoutReader, source_open, source_str)


__all__ = (
    'MODULE_DETACH_FACET_NAMES',

    'build_module_load_facets',
    'build_module_pragma',
    'attach_module_load_facets',
    'detach_module_load_facets',

    'new_module',
    'init_module',
    'compile_top_level',
    'load_facets',
    'exports_materialized',
    'run_codes_async',
    'run_codes_demand',
    'kio_codes',
    'load_module_async',
    'load_module_demand',
    'load_module_kio_async',
    'load_module_kio_demand',
    'load_module_source_async',
)


# the facets detached after a load; __pragma__ stays bound as the
# module's record so a compiler can be rebuilt from it later
MODULE_DETACH_FACET_NAMES = (
    '__compiler__',
    '__reader__',
)


def public_vars(obj):
    """
    Return a shallow dict of public attributes from obj

    Names starting with ``__`` are omitted.
    """

    return {k: v for k, v in vars(obj).items() if not k.startswith('__')}


def new_module(name, package=None, system=False):
    """
    Create a new koskari Module with an initialised ModuleNamespace

    Parent bindings are copied from the loaded koskari.builtins Python
    module (bootstrap registry plus basics.ki).  No event loop or await
    is required.

    :param name: fully-qualified module name string
    :param package: parent package name string, or None
    :param system: when True, register the module in sys.modules
    :return: koskari Module instance
    """

    import koskari.builtins as builtins

    ns = ModuleNamespace(name, public_vars(builtins))
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


def compile_top_level(module, compiler, reader, source_stream):
    """
    Read and compile each top-level form of a module's source in order,
    yielding one expression Code per form. The caller runs each before
    the next is read, so compile-time artifacts a form defines (syntax,
    reader macros) are live when the following form compiles.

    A leading string is the module docstring: it is rewritten to a
    define of ``__doc__`` and yielded like any other form, which is also
    the body a .kio carries for it

    :param module: koskari Module whose namespace the forms run in
    :param compiler: Compiler facet for this load
    :param reader: LayoutReader facet for this load
    :param source_stream: SourceStream from source_open or source_str
    """

    stream = IndentAwareStream.from_source(source_stream)

    docstr = None
    while True:
        source_expr = reader.read(stream)
        if source_expr is READ_EOF:
            return

        if docstr is None:
            docstr = isinstance(source_expr, str)
            if docstr:
                source_expr = cons(
                    symbol('define'), symbol('__doc__'), source_expr, nil)

        fragment = compiler.compile_expression(source_expr)
        yield compiler.complete(
            fragment, mode=CompilerMode.EXPRESSION, is_root=True)


@contextmanager
def load_facets(module, filename, *, seal, detach):
    """
    The facets of one load. Builds (or reuses) the compiler and reader
    and yields them; seals the module on normal exit when ``seal`` is
    set; when ``detach`` is set, materializes ``__all__`` and detaches
    the facets afterward, always, even when the load raises, with
    ``__pragma__`` staying bound. Callers passing ``detach`` False own
    the completion steps: an interactive session seals but retains
    facets; the bootstrap builtins loop defers both across its source
    files and performs them once at the end

    :param module: koskari Module (or object whose ``vars()`` is the ns)
    :param filename: source filename for the compiler
    :param seal: on success, pack the compiler's surviving code objects
        per the compiler/pack-module pragma
    :param detach: materialize exports and drop the load facets
    :return: the (compiler, reader) pair
    """

    compiler, reader = build_module_load_facets(module, filename)
    try:
        yield compiler, reader
        if seal:
            compiler.seal()
    finally:
        if detach:
            materialize_exports(module)
            detach_module_load_facets(module)


@contextmanager
def exports_materialized(module):
    """
    Materialize ``__all__`` when the block ends, whether or not it raised
    """

    try:
        yield
    finally:
        materialize_exports(module)


async def run_codes_async(module, codes):
    """
    Run each Code as an expression of ``module``, awaiting each in turn.

    A helper so no caller local outlives the loop: the last expression
    must be released before the compiler seals, or it would be swept
    into an arena
    """

    ns = vars(module)
    for code in codes:
        await code.expression(ns)()


def run_codes_demand(module, codes):
    """
    Run each Code as an expression of ``module`` via demand_call. No
    async suspension is allowed. See :func:`run_codes_async` for why
    the loop lives in a helper
    """

    ns = vars(module)
    for code in codes:
        demand_call(code.expression(ns))


def kio_codes(module):
    """
    The stored bodies of a module's precompiled .kio file, with the
    module's ``__pragma__`` record bound first so ``(pragma ...)``
    bodies and package-pragma stamping run as they do under a source
    load. No compiler or reader is built: the bodies were compiled
    already

    :param module: koskari Module with ``__file__`` set to a .kio path
    :return: the Code objects in stored order
    """

    from .serialize.codex import KioFile

    build_module_pragma(module)
    return KioFile.read(getattr(module, '__file__', '<unknown>')).codes


async def load_module_async(module, *, seal=True, detach=True):
    """
    Execute a koskari module by reading, compiling, and running each
    top-level expression in order.

    Each expression is compiled relative to the module, then executed as
    an expression coroutine and awaited.  Bindings created by one
    expression are visible to all subsequent expressions, supporting
    define, defsyntax, and reader macro registration at module load time.

    While loading, ``__pragma__``, ``__compiler__``, and ``__reader__``
    are bound on the module; facets already present (a prior load with
    ``detach=False``) are reused, so consecutive loads share one pragma,
    compiler, and reader. Sealing and detaching follow
    :func:`load_facets`.

    :param module: koskari Module instance with ``__file__`` set
    :param seal: when True, seal the module on successful completion
    :param detach: when True, detach the compiler and reader facets
        afterward
    """

    filename = getattr(module, '__file__', '<unknown>')

    with source_open(filename) as source_stream, \
            load_facets(module, filename, seal=seal, detach=detach) as facets:
        await run_codes_async(
            module, compile_top_level(module, *facets, source_stream))


def load_module_demand(module, *, seal=True, detach=True):
    """
    Execute a koskari module by reading, compiling, and running each
    top-level expression in order via demand_call.

    Same structure as :func:`load_module_async` but each expression is
    run with demand_call instead of awaiting a coroutine.  No async
    suspension is allowed.  Used by the loader's exec_module_demand path.

    :param module: koskari Module instance with __file__ set
    :param seal: when True, seal the module on successful completion
    :param detach: when True, detach the compiler and reader facets
        afterward
    """

    filename = getattr(module, '__file__', '<unknown>')

    with source_open(filename) as source_stream, \
            load_facets(module, filename, seal=seal, detach=detach) as facets:
        run_codes_demand(
            module, compile_top_level(module, *facets, source_stream))


async def load_module_source_async(
        module,
        source, *,
        seal=True,
        detach=True,
        layout=True):
    """
    Execute koskari source passed as a string.

    Same top-level expression loop as :func:`load_module_async`, but reads
    from ``source`` instead of opening ``module``'s ``__file__``.

    :param module: koskari Module instance with ``__file__`` set
    :param source: koskari source text to execute
    :param seal: when True, seal the module on successful completion
    :param detach: when True, detach the compiler and reader facets
        afterward
    :param layout: when False, read plain s-expressions instead of
        layout.  A layout line is a single form, so several expressions
        on one line become one implicit call; that suits a file but not
        a one-liner, where consecutive top-level forms are wanted
    """

    filename = getattr(module, '__file__', '<string>')
    source_stream = source_str(source, filename, auto_skip_exec=False)

    with load_facets(module, filename, seal=seal, detach=detach) \
            as (compiler, reader):
        if not layout:
            reader.set_layout_modes(layout_enabled=False)
        await run_codes_async(
            module, compile_top_level(module, compiler, reader, source_stream))


async def load_module_kio_async(module):
    """
    Execute a module from a precompiled .kio file.

    Each stored body is re-hardened and run as a top-level expression in
    order, matching the incremental structure of load_module_async;
    ``__all__`` is materialized from ``declare-exports`` at the end.

    :param module: koskari Module with __file__ pointing at a .kio path
    """

    codes = kio_codes(module)
    with exports_materialized(module):
        await run_codes_async(module, codes)


def load_module_kio_demand(module):
    """
    Execute a module from a precompiled .kio file via demand_call.

    Same steps as load_module_kio_async but synchronous.

    :param module: koskari Module with __file__ set to a .kio path
    """

    codes = kio_codes(module)
    with exports_materialized(module):
        run_codes_demand(module, codes)


# ---------------------------------------------------------------------------
# Load facets and namespace helpers
# ---------------------------------------------------------------------------


def find_package_pragma(module):
    """
    The nearest ancestor package's ``__package_pragma__``, or None.

    Walks the module-name hierarchy through ``sys.modules``, skipping
    ancestors that do not carry the slot (namespace packages, plain
    Python packages, missing entries).

    :param module: koskari Module (or object whose ``vars()`` is the ns)
    :return: ModulePragma or None
    """

    name = getattr(module, '__name__', None)
    while name and '.' in name:
        name = name.rsplit('.', 1)[0]
        parent = sys.modules.get(name)
        found = getattr(parent, '__package_pragma__', None)
        if found is not None:
            return found
    return None


def build_module_load_facets(module, filename=None):
    """
    Produce the load-time pragma, compiler, and reader for ``module``.

    When all three facets are already bound on the module (a prior load
    ran with ``detach=False``), they are reused as-is rather than
    rebuilt, so consecutive loads share one pragma lineage, one compiler
    (and its minted-code registry), and one reader.  The compiler's
    filename is refreshed so code minted by this load attributes
    backtrace positions to the current source file.

    Otherwise the module's bound ``__pragma__`` is kept when present (a
    detached load leaves it as the module's record), or a
    :class:`ModulePragma` is built and bound as ``__pragma__``; the
    compiler and reader are then chained off it via ``from_pragma``
    (each subscribing for later pragma changes) and bound as load
    facets.  A fresh pragma comes from :func:`build_module_pragma`,
    which seeds it from the nearest ancestor package's
    ``__package_pragma__``.

    :param module: koskari Module (or object whose ``vars()`` is the ns)
    :param filename: optional source filename
    :return: the (compiler, reader) pair
    """

    ns = vars(module)

    compiler = ns.get('__compiler__')
    reader = ns.get('__reader__')
    if compiler is not None and reader is not None \
            and ns.get('__pragma__') is not None:
        if filename is not None:
            compiler.filename = filename
        return compiler, reader

    name = getattr(module, '__name__', '<module>')

    pragma = build_module_pragma(module)

    compiler = Compiler.from_pragma(name, module, filename=filename,
                                    pragma=pragma)
    reader = LayoutReader.from_pragma(module, pragma=pragma)

    attach_module_load_facets(module, pragma, compiler, reader)
    return compiler, reader


def build_module_pragma(module):
    """
    The module's ``__pragma__`` record, bound if it was not already.

    A bound ``__pragma__`` is returned as-is. Otherwise this is the single
    chokepoint where the pragma source is resolved: the nearest ancestor
    package's ``__package_pragma__`` seeds the settings when one exists
    (a copy with a fresh watcher set -- watchers never cross modules),
    falling back to the built-in default. A package additionally pulls
    the inherited default down as its own ``__package_pragma__`` so its
    descendants inherit it; a ``(package-pragma ...)`` declaration in the
    package body overwrites that stamp.

    :param module: koskari Module (or object whose ``vars()`` is the ns)
    :return: the ModulePragma bound as ``__pragma__``
    """

    ns = vars(module)

    pragma = ns.get('__pragma__')
    if pragma is not None:
        return pragma

    inherited = find_package_pragma(module)
    pragma = inherited.copy() if inherited is not None else ModulePragma()

    if inherited is not None \
            and getattr(module, '__path__', None) is not None:
        ns['__package_pragma__'] = inherited.copy()

    ns['__pragma__'] = pragma
    return pragma


def attach_module_load_facets(module, pragma, compiler, reader):
    """
    Bind ``__pragma__``, ``__compiler__``, and ``__reader__`` on
    ``module`` for load time.

    :param pragma: ModulePragma instance for this load
    :param module: koskari Module (or object whose ``vars()`` is the ns)
    :param compiler: Compiler instance for this load
    :param reader: LayoutReader instance for this load
    """

    ns = vars(module)
    ns['__pragma__'] = pragma
    ns['__compiler__'] = compiler
    ns['__reader__'] = reader


def materialize_exports(module):
    """
    Materialize ``__all__`` from a module's declared feature stems.

    ``declare-exports`` authors stems -- feature names without facet
    prefixes -- into ``__features__`` before the definitions exist. At
    the end of the load every definition is settled, so each stem
    resolves to the binding keys that actually exist: the runtime name
    and its compile/ and layout/ facets. A stem written with a facet
    prefix is a literal single-binding export. The result is a plain
    Python ``__all__``: every entry is a real attribute, so Python-side
    star imports and import-all-from stay honest.

    A module with no ``__features__`` is left alone.

    :param module: koskari Module (or object whose ``vars()`` is the ns)
    """

    ns = vars(module)
    features = ns.get('__features__')
    if features is None:
        return

    facets = ("", "compile/", "layout/")
    out = []
    # membership answers at the value tier: a declared-but-unassigned
    # cell left by a compile-time probe is not in the mapping, while a
    # key legitimately bound to None is
    for stem in features:
        stem = str(stem)
        if stem.startswith(("compile/", "layout/")):
            if stem in ns:
                out.append(stem)
            continue
        for prefix in facets:
            key = prefix + stem
            if key in ns:
                out.append(key)

    ns['__all__'] = tuple(sorted(set(out)))


def detach_module_load_facets(module):
    """
    Remove load-time ``__compiler__`` and ``__reader__`` bindings.

    ``__pragma__`` stays bound as the module's record; the get-compiler
    runtime function rebuilds a compiler from it on demand. Ignores names
    listed in :data:`MODULE_DETACH_FACET_NAMES` that are not bound.

    :param module: koskari Module (or object whose ``vars()`` is the ns)
    """

    ns = vars(module)
    for name in MODULE_DETACH_FACET_NAMES:
        try:
            del ns[name]
        except KeyError:
            pass


# The end.
