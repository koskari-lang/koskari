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
koskari.serialize.incremental

Compile .ki sources into .kio bodies, stamped from the source they were
compiled from; loading a .kio lives in koskari.module.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import importlib
import os
from warnings import warn

from ..lib import demand_call
from ..module import (
    compile_top_level, init_module, load_facets, new_module)
from ..reader import source_open, source_str
from .codex import KioFile, KioMeta


def compile_top_level_to_codes(mod, filename, source_stream):
    """
    Read top-level forms from a SourceStream and return compiled Code
    objects, running each as it is compiled so compile-time artifacts
    are available to the forms after it

    :param mod: module namespace for evaluation side effects
    :param filename: logical source filename for the load facets
    :param source_stream: SourceStream from source_open or source_str
    :return: list of Code objects in source order
    """

    ns = vars(mod)
    bodies = []

    with load_facets(mod, filename, seal=False, detach=True) as facets:
        for code in compile_top_level(*facets, source_stream):
            bodies.append(code)
            demand_call(code.expression(ns))

    return bodies


# the name a .kio build runs under when the file's own name would run
# a when-main body or names no module at all
SYNTHETIC_MODULE_NAME = '__kio_compile__'


def _is_package_dir(directory):
    return (os.path.isfile(os.path.join(directory, '__init__.ki'))
            or os.path.isfile(os.path.join(directory, '__init__.py')))


def module_identity(ki_path):
    """
    The dotted module name and parent package name an import of ki_path
    would give it: every directory above the file that holds an
    __init__.ki or __init__.py is a package segment, and an __init__
    file names the package itself. A file under no package is named by
    its stem alone, unless that stem is __main__, which would run a
    when-main body at build; it gets the synthetic name instead

    :param ki_path: path to the source file
    :return: (name, package) with package None outside any package
    """

    directory, filename = os.path.split(os.path.abspath(ki_path))
    stem = os.path.splitext(filename)[0]

    parts = []
    while _is_package_dir(directory):
        directory, segment = os.path.split(directory)
        if not segment:
            break
        parts.insert(0, segment)

    if stem == '__init__':
        if not parts:
            return SYNTHETIC_MODULE_NAME, None
        name = '.'.join(parts)
        return name, name
    if not parts:
        return (SYNTHETIC_MODULE_NAME if stem == '__main__' else stem), None
    return '.'.join(parts + [stem]), '.'.join(parts)


def _module_for_build(mod_name, package):
    """
    The module a .kio build compiles and runs in, with its parent package
    imported first so the inherited package pragma and relative imports
    resolve as they would in a source load
    """

    if package is not None and package != mod_name:
        try:
            importlib.import_module(package)
        except ImportError as exc:
            warn(f"parent package {package!r} of {mod_name!r} is not"
                 f" importable ({exc}); compiling without its package"
                 " pragma")
    return new_module(mod_name, package=package, system=False)


def compile_ki_file_to_kio(
        ki_path,
        kio_dest,
        mod_name=None,
        source_filename=None):
    """
    Read each top-level form from a .ki file, compile to expression Code,
    and write a single .kio containing one CODEP body per form.

    The module is compiled under the name an import of ``ki_path`` would
    give it (:func:`module_identity`), with its parent package imported
    first, so the package pragma in force and a relative import in the
    body are the ones a source load sees. ``mod_name`` overrides the
    derived name; its parent package is the part before the last dot.

    Source bytes are always read from ``ki_path``. Each body stores a
    ``filename`` string used when the .kio is loaded so ``Code.filename`` and
    native tracebacks refer to that path. By default this is ``ki_path``. If
    ``source_filename`` is set (for example a stable path for distributed
    packages), it is stored instead; line and column metadata still refer to
    the same text as ``ki_path``, so that file must have identical contents at
    runtime for ``linecache`` and traceback source lines to resolve. Relative
    paths depend on the process current working directory.

    :param ki_path: path to Koskari source file to open and compile
    :param kio_dest: output .kio path or writable binary stream
    :param mod_name: module name to compile under, or None to derive it
        from the path
    :param source_filename: optional string stored per body as the logical
        source path for diagnostics when it differs from ``ki_path``
    """

    display = source_filename if source_filename is not None else ki_path

    if mod_name is None:
        mod_name, package = module_identity(ki_path)
    else:
        package = mod_name.rpartition('.')[0] or None

    mod = _module_for_build(mod_name, package)
    init_module(mod, ki_path, spec=None)

    with source_open(ki_path) as source_stream:
        bodies = compile_top_level_to_codes(mod, display, source_stream)

    # stamped from the source as it was compiled, so a loader can tell
    # this file apart from one left behind by an older copy of the .ki
    KioFile.from_codes(bodies, meta=KioMeta.for_source(ki_path)).write(kio_dest)


def compile_ki_source_to_kio(
        source_text,
        kio_dest,
        mod_name=SYNTHETIC_MODULE_NAME,
        source_filename=None):
    """
    Compile Koskari source text to a single .kio with one CODEP body per form.

    Same semantics as compile_ki_file_to_kio except input is a string. The
    logical filename for diagnostics and embedded ``Code.filename`` is
    ``source_filename`` when set, otherwise a placeholder string. No
    source stamp is recorded, so the loader treats the file as stale
    beside a sibling .ki.

    :param source_text: Koskari source string
    :param kio_dest: output .kio path or writable binary stream
    :param mod_name: synthetic module name for compiled fragments
    :param source_filename: logical path for errors and stored filenames
    """

    display = source_filename if source_filename is not None else '<compile_ki_source>'

    mod = new_module(mod_name, system=False)
    init_module(mod, display, spec=None)

    source_stream = source_str(source_text, display)
    bodies = compile_top_level_to_codes(mod, display, source_stream)

    KioFile.from_codes(bodies).write(kio_dest)


# The end.
