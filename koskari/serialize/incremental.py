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

Compile .ki sources into .kio bodies and drive evaluation from loaded .kio.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from ..compile.compiler import CompilerMode
from ..lib import cons, demand_call, nil, symbol
from ..module import (
    build_module_load_facets, detach_module_load_facets, init_module,
    materialize_exports, new_module)
from ..reader import READ_EOF, IndentAwareStream, source_open, source_str
from .codex import KioFile, KioMeta


def _compile_top_level_to_codes(mod, compiler, reader, source_stream):
    """
    Read top-level forms from a SourceStream and return compiled Code objects

    :param mod: module namespace for evaluation side effects
    :param compiler: Compiler instance
    :param source_stream: SourceStream from source_open or source_str
    :return: list of Code objects in source order
    """

    ns = vars(mod)
    try:
        stream = IndentAwareStream(
            source_stream.stream,
            source_stream.filename,
            auto_skip_exec=False,
            line_no=source_stream.lin,
            col_no=source_stream.col,
            max_indent=None)

        bodies = []
        docstr = None
        while True:
            source_expr = reader.read(stream)
            if source_expr is READ_EOF:
                break

            # the source loader lifts a leading string into __doc__
            # without evaluating it; the .kio carries the same lift as
            # its first body, a define of __doc__
            if docstr is None:
                if isinstance(source_expr, str):
                    source_expr = cons(
                        symbol('define'), symbol('__doc__'), source_expr, nil)
                    docstr = True
                else:
                    docstr = False

            fragment = compiler.compile_expression(source_expr)
            code = compiler.complete(
                fragment, mode=CompilerMode.EXPRESSION, is_root=True)
            bodies.append(code)

            # we must evaluate the code to ensure future compile-time
            # artifacts are available
            demand_call(code.expression(ns))

        return bodies
    finally:
        materialize_exports(mod)
        detach_module_load_facets(mod)


def compile_ki_file_to_kio(
        ki_path,
        kio_dest,
        mod_name='__kio_compile__',
        source_filename=None):
    """
    Read each top-level form from a .ki file, compile to expression Code,
    and write a single .kio containing one CODEP body per form.

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
    :param mod_name: synthetic module name for compiled fragments
    :param source_filename: optional string stored per body as the logical
        source path for diagnostics when it differs from ``ki_path``
    """

    display = source_filename if source_filename is not None else ki_path

    mod = new_module(mod_name, system=False)
    init_module(mod, ki_path, spec=None)
    compiler, reader = build_module_load_facets(mod, display)

    with source_open(ki_path) as source_stream:
        bodies = _compile_top_level_to_codes(mod, compiler, reader, source_stream)

    # stamped from the source as it was compiled, so a loader can tell
    # this file apart from one left behind by an older copy of the .ki
    KioFile.from_codes(bodies, meta=KioMeta.for_source(ki_path)).write(kio_dest)


def compile_ki_source_to_kio(
        source_text,
        kio_dest,
        mod_name='__kio_compile__',
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
    compiler, reader = build_module_load_facets(mod, display)

    source_stream = source_str(source_text, display)
    bodies = _compile_top_level_to_codes(mod, compiler, reader, source_stream)

    KioFile.from_codes(bodies).write(kio_dest)


# The end.
