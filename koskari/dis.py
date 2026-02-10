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
koskari.dis

Disassembler for Koskari code objects. Similar to Python's dis module:
unpacks constant pool, vars (local_syms), closure refs (closure_syms from
namespace_spec), and prints indexed opcodes with human-readable arguments
(e.g. constant pool index as repr of value, jump targets as instruction
indices).

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Auto via Cursor
"""

from koskari.lib import Code, OpCode, OpStyle


def _opcode_maps():
    """
    Build opcode int -> name and opcode int -> style from OpCode enum
    """

    names = {}
    styles = {}
    for op in OpCode:
        names[op.value] = op.name
        style = op.style
        styles[op.value] = style.value if hasattr(style, 'value') else style
    return names, styles


_OPCODE_NAMES, _OPCODE_STYLES = _opcode_maps()


def _get_code(x):
    """
    Return x if it is a Code instance, else None.

    :param x: value to check
    :return: Code instance or None
    """

    return x if isinstance(x, Code) else None


def _format_constant(val):
    """
    Repr suitable for constant pool display (bounded length)

    :param val: constant pool value
    :return: string representation of the value
    """

    r = repr(val)
    if len(r) > 60:
        r = r[:57] + '...'
    return r


def _format_arg(code, op_style, arg, cpool, local_names, closure_names):
    """
    Format an opcode argument for display. Uses constant pool, local
    names, and closure names when the argument is an index into them.

    :param code: Koskari Code
    :param op_style: OpStyle value (from OpCode.style or OpStyle enum)
    :param arg: raw argument value
    :param cpool: code.constants_pool (sequence)
    :param local_names: sequence of local sym names from nspec
    :param closure_names: sequence of closure sym names from nspec
    :return: string description of the argument
    """

    if op_style == OpStyle.KOSTYLE_CONST:
        if 0 <= arg < len(cpool):
            val = cpool[arg]
            return '%d (%s)' % (arg, _format_constant(val))
        return str(arg)

    if op_style == OpStyle.KOSTYLE_VAR:
        if 0 <= arg < len(cpool):
            name = cpool[arg]
            return '%d (%s)' % (arg, repr(name))
        return str(arg)

    if op_style == OpStyle.KOSTYLE_FASTVAR:
        # Fast index: locals then closures in nspec order
        n_local = len(local_names) if local_names else 0
        if arg < n_local:
            name = local_names[arg] if local_names else ('local[%d]' % arg)
            return '%d (%s)' % (arg, repr(name))
        idx = arg - n_local
        if closure_names and 0 <= idx < len(closure_names):
            return '%d (closure %s)' % (arg, repr(closure_names[idx]))
        return str(arg)

    if op_style == OpStyle.KOSTYLE_LABEL:
        return '%d (-> %d)' % (arg, arg)

    if op_style in (OpStyle.KOSTYLE_COUNT, OpStyle.KOSTYLE_PAIR_COUNT, OpStyle.KOSTYLE_STAR):
        return str(arg)

    return str(arg)


def _local_and_closure_syms(code):
    """
    Return (local_syms tuple, closure_syms tuple) from code.namespace_spec
    """

    nspec = code.namespace_spec
    if nspec is None:
        return (), ()

    locals_seq = nspec.local_syms or ()
    closure_seq = nspec.closure_syms or ()
    return locals_seq, closure_seq


def dis(x, file=None):
    """
    Print a disassembly of a Koskari Code object.

    Output includes constant pool, vars (local_syms from namespace_spec),
    closure refs (closure_syms from namespace_spec), and indexed
    instructions with decoded arguments.

    :param x: Code instance
    :param file: file-like object to print to (default: stdout)
    """

    code = _get_code(x)
    if code is None:
        raise TypeError('expected Code, got %s' % type(x).__name__)
    if file is None:
        import sys
        file = sys.stdout

    cpool = code.constants_pool
    local_syms, closure_syms = _local_and_closure_syms(code)

    # Header (Code has .name, .filename, .yields from the C extension)
    name = code.name
    filename = code.filename
    yields = code.yields
    file.write('KosCode %s\n' % repr(name))
    file.write('  filename: %s\n' % repr(filename))
    file.write('  yields: %s\n' % yields)
    file.write('\n')

    # Constant pool
    file.write('Constant pool (%d):\n' % len(cpool))
    for i, val in enumerate(cpool):
        file.write('  %3d: %s\n' % (i, _format_constant(val)))
    file.write('\n')

    # Vars (local_syms)
    file.write('Vars (local_syms, %d):\n' % len(local_syms))
    if local_syms:
        for i, sym in enumerate(local_syms):
            file.write('  %3d: %s\n' % (i, repr(sym)))
    else:
        file.write('  (none)\n')
    file.write('\n')

    # Closure refs
    file.write('Closure refs (%d):\n' % len(closure_syms))
    if closure_syms:
        for i, sym in enumerate(closure_syms):
            file.write('  %3d: %s\n' % (i, repr(sym)))
    else:
        file.write('  (none)\n')
    file.write('\n')

    # Instructions
    n = code.length()
    file.write('Instructions (%d):\n' % n)
    for i in range(n):
        op_int, arg = code.oparg(i)
        op_name = _OPCODE_NAMES.get(op_int, 'OP_%d' % op_int)
        op_style = _OPCODE_STYLES.get(op_int, OpStyle.KOSTYLE_NOARG)
        arg_str = _format_arg(
            code, op_style, arg, cpool, local_syms, closure_syms
        )
        pos_str = ''
        try:
            line, col = code.position(i)
            pos_str = '  # line %d, col %d' % (line, col)
        except (IndexError, TypeError):
            pass
        file.write('  %3d  %-24s %s%s\n' % (i, op_name, arg_str, pos_str))
    file.write('\n')


def _get_instruction_lines(code):
    """
    Yield (index, op_name, arg_str) for each instruction (for tests/API)
    """

    cpool = code.constants_pool
    local_syms, closure_syms = _local_and_closure_syms(code)
    n = code.length()
    for i in range(n):
        op_int, arg = code.oparg(i)
        op_name = _OPCODE_NAMES.get(op_int, 'OP_%d' % op_int)
        op_style = _OPCODE_STYLES.get(op_int, OpStyle.KOSTYLE_NOARG)
        arg_str = _format_arg(
            code, op_style, arg, cpool, local_syms, closure_syms
        )
        yield i, op_name, arg_str


# The end.
