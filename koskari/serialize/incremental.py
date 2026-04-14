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
:ai-assistant: Auto via Cursor
"""


from koskari.compile.compiler import CompilerMode
from koskari.lib import OpCode
from koskari.reader import READ_EOF, IndentAwareStream, LayoutReader, source_open

from koskari.serialize.kio import dump_kio
from koskari.serialize.softcode import soften_code


def compile_ki_file_to_kio(
        ki_path,
        kio_path,
        mod_name='__kio_compile__',
        source_filename=None):
    """
    Read each top-level form from a .ki file, compile to expression Code,
    soften, and write a single .kio containing one CODEP body per form.

    Source bytes are always read from ``ki_path``. Each body stores a
    ``filename`` string used when the .kio is loaded so ``Code.filename`` and
    native tracebacks refer to that path. By default this is ``ki_path``. If
    ``source_filename`` is set (for example a stable path for distributed
    packages), it is stored instead; line and column metadata still refer to
    the same text as ``ki_path``, so that file must have identical contents at
    runtime for ``linecache`` and traceback source lines to resolve. Relative
    paths depend on the process current working directory.

    :param ki_path: path to Koskari source file to open and compile
    :param kio_path: output .kio path
    :param mod_name: synthetic module name for compiled fragments
    :param source_filename: optional string stored per body as the logical
        source path for diagnostics when it differs from ``ki_path``
    """

    from koskari.compile.compiler import Compiler
    from koskari.importer.module import init_module, new_module_demand

    display = source_filename if source_filename is not None else ki_path

    mod = new_module_demand(mod_name, system=False)
    init_module(mod, ki_path, spec=None)
    compiler = Compiler(mod_name, mod, filename=ki_path)
    bodies = []
    idx = 0

    with source_open(ki_path) as source_stream:
        reader = LayoutReader(mod)
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
            code = compiler.complete(
                fragment, mode=CompilerMode.EXPRESSION, is_root=True)
            instr, lc = soften_code(code)
            yld = False
            for op, _ in instr:
                if op in (OpCode.KOS_OP_YIELD, OpCode.KOS_OP_YIELD_FROM):
                    yld = True
                    break
            bodies.append({
                'nspec': code.namespace_spec,
                'name': '%s$%d' % (mod_name, idx),
                'filename': display,
                'instructions': instr,
                'label_count': lc,
                'yields': yld,
            })
            idx += 1

    dump_kio(kio_path, bodies)


# The end.
