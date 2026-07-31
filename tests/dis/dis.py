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
tests.dis.dis

Pure-Python disassembler for Koskari code objects, kept in the test tree
as a fallback for koskari.dis (which is now written in Koskari itself).
Tests can rely on this to show opcodes, constant pools, and namespaces
even when the Koskari stack is too broken to run its own disassembler.

Similar to Python's dis module:
unpacks constant pool, vars (local_syms), closure refs (closure_syms from
namespace_spec), and prints indexed opcodes with human-readable arguments
(e.g. constant pool index as repr of value, jump targets as instruction
indices). Also accepts BoundMethod (unwraps __func__ for bytecode, prints
__self__ in a leading section).

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from koskari.lib import (BoundMethod, Code, Expression, Function, demand,
                         NamespaceSpec, OpCode, OpStyle)


def _boundmethod_inner_function(bm):
    """
    Return the KosFunction behind a BoundMethod, unwrapping Demand if needed

    :param bm: BoundMethod instance
    :return: Function instance
    :raises TypeError: when __func__ is not Function or Demand(Function)
    """

    func = bm.__func__
    if isinstance(func, demand):
        func = func.__wrapped__
    if not isinstance(func, Function):
        raise TypeError(
            'BoundMethod __func__ must be Function or Demand(Function), got %s'
            % type(func).__name__)
    return func


def _get_code(x):
    """
    Return Code for disassembly targets: Code, or Expression (unwrap .code).

    :param x: value to check
    :return: Code instance or None
    """

    if isinstance(x, Code):
        return x
    if isinstance(x, Expression):
        return x.code
    return None


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


def _write_function_instance_section(file, fn):
    """
    Write a leading section for a Function instance: name, formals,
    base_ns (closures).

    :param file: file-like object to write to
    :param fn: Function instance (KosFunction)
    """

    name = fn.name
    name_repr = repr(name) if name is not None else 'None'
    file.write('Function instance:\n')
    file.write('  name: %s\n' % name_repr)
    file.write('  formals:\n')
    formals = fn.formals
    if formals is not None:
        for attr in ('pos', 'pos_only', 'kwd', 'kwd_only', 'star', 'starstar',
                     'defaults'):
            val = getattr(formals, attr, None)
            file.write('    %s: %s\n' % (attr, repr(val)))
    else:
        file.write('    (none)\n')
    base_ns = fn.base_ns
    file.write('  base_ns (closures):\n')
    if base_ns is not None:
        try:
            items = base_ns.items()
        except (AttributeError, TypeError):
            file.write('    (unable to iterate)\n')
        else:
            for k, v in items:
                file.write('    %s: %s\n' % (repr(k), _format_constant(v)))
    else:
        file.write('    (none)\n')
    file.write('\n')


def _write_bound_method_section(file, bm):
    """
    Write a leading section for a BoundMethod: bound __self__.

    :param file: file-like object to write to
    :param bm: BoundMethod instance
    """

    file.write('Bound method instance:\n')
    file.write('  __self__: %s\n' % _format_constant(bm.__self__))
    file.write('\n')


def _format_nspec_inline(cpool_idx, nspec):
    """
    Short inline label for a NamespaceSpec used in instruction annotations.

    Returns a string of the form "ns#K 'name'" where K is the constant
    pool index and name is the namespace name.

    :param cpool_idx: integer index into the constant pool
    :param nspec: NamespaceSpec instance
    :return: inline annotation string
    """

    ns_name = nspec.name
    name_repr = repr(ns_name) if ns_name is not None else 'None'
    return 'ns#%d %s' % (cpool_idx, name_repr)


def _write_nspec_cpool_entry(file, cpool_idx, nspec):
    """
    Write an expanded constant pool entry for a NamespaceSpec.

    Emits the ns# label line followed by indented locals and closures
    lines, so the reader can see all names available in the namespace.

    :param file: file-like object to write to
    :param cpool_idx: integer index into the constant pool
    :param nspec: NamespaceSpec instance
    """

    ns_name = nspec.name
    name_repr = repr(ns_name) if ns_name is not None else 'None'
    local_syms = nspec.local_syms
    closure_syms = nspec.closure_syms
    global_syms = nspec.global_syms
    all_captured = tuple(closure_syms) + tuple(global_syms)
    file.write('  ns#%d: NamespaceSpec %s\n' % (cpool_idx, name_repr))
    file.write('          locals:   %r\n' % (tuple(local_syms),))
    file.write('          closures: %r\n' % (tuple(all_captured),))


def _build_ns_const_map(code):
    """
    Pre-scan instructions and return a mapping from PUSH_NS / PUSH_COLLECT
    instruction index to the constant pool index of the NamespaceSpec.

    PUSH_NS: extra row slot a0 is the constant pool index of the NamespaceSpec.
    PUSH_COLLECT: arg is the constant pool index.

    :param code: Code instance
    :return: dict mapping instruction index -> constant pool index
    """

    result = {}
    for i in range(code.length()):
        op_int, arg = code.oparg(i)
        op = OpCode(op_int)
        if op == OpCode.PUSH_NS:
            a0, _a1, _a2, _a3 = code.extra_tuple(arg)
            result[i] = a0
        elif op == OpCode.PUSH_COLLECT:
            result[i] = arg
    return result


def _format_arg(
        code,
        op_style,
        arg,
        cpool,
        local_names,
        closure_names,
        ns_label=None,
        extra_schema=()):
    """
    Format an opcode argument for display. Uses constant pool, local
    names, and closure names when the argument is an index into them.

    For PUSH_NS and PUSH_CLASS, the optional ns_label string is appended
    to annotate which namespace is being pushed.

    :param code: Koskari Code
    :param op_style: OpStyle member (op.style)
    :param arg: raw argument value
    :param cpool: code.constants_pool (sequence)
    :param local_names: sequence of local sym names from nspec
    :param closure_names: sequence of closure sym names from nspec
    :param ns_label: optional inline namespace label for PUSH_NS / PUSH_CLASS
    :param extra_schema: op.extra -- a 4-tuple of OpStyle field kinds for
      an EXTRA-style opcode, empty for every other style
    :return: string description of the argument
    """

    if op_style == OpStyle.EXTRA:
        if extra_schema:
            schema = extra_schema
            try:
                a0, a1, a2, a3 = code.extra_tuple(arg)
            except (IndexError, TypeError):
                return str(arg)
            parts = []
            for val, fk in ((a0, schema[0]), (a1, schema[1]),
                            (a2, schema[2]), (a3, schema[3])):
                if fk == OpStyle.CONST or fk == OpStyle.VAR:
                    if 0 <= val < len(cpool):
                        cval = cpool[val]
                        if fk == OpStyle.CONST and isinstance(
                                cval, NamespaceSpec):
                            inner = _format_nspec_inline(val, cval)
                        else:
                            inner = _format_constant(cval)
                        parts.append('%s#%d (%s)' % (
                            'c' if fk == OpStyle.CONST else 'v',
                            val, inner))
                    else:
                        parts.append('%d' % val)
                elif fk == OpStyle.FASTVAR:
                    n_local = len(local_names) if local_names else 0
                    if val < n_local:
                        name = (local_names[val]
                                if local_names else 'local[%d]' % val)
                        parts.append('fast %d (%s)' % (val, repr(name)))
                    else:
                        idx = val - n_local
                        if closure_names and 0 <= idx < len(closure_names):
                            parts.append('fast %d (closure %s)' % (
                                val, repr(closure_names[idx])))
                        else:
                            parts.append('fast %d' % val)
                elif fk == OpStyle.COUNT:
                    parts.append(str(val))
                elif fk == OpStyle.LABEL:
                    parts.append('-> %d' % val)
                else:
                    parts.append(str(val))
            return '%d (%s)' % (arg, ', '.join(parts))
        return str(arg)

    if op_style == OpStyle.CONST:
        if 0 <= arg < len(cpool):
            val = cpool[arg]
            if isinstance(val, NamespaceSpec):
                return '%d (%s)' % (arg, _format_nspec_inline(arg, val))
            return '%d (%s)' % (arg, _format_constant(val))
        return str(arg)

    if op_style == OpStyle.VAR:
        if 0 <= arg < len(cpool):
            name = cpool[arg]
            return '%d (%s)' % (arg, repr(name))
        return str(arg)

    if op_style == OpStyle.FASTVAR:
        n_local = len(local_names) if local_names else 0
        if arg < n_local:
            name = local_names[arg] if local_names else ('local[%d]' % arg)
            return '%d (local %s)' % (arg, repr(name))
        idx = arg - n_local
        if closure_names and 0 <= idx < len(closure_names):
            return '%d (closure %s)' % (arg, repr(closure_names[idx]))
        return str(arg)

    if op_style == OpStyle.LABEL:
        return '%d (-> %d)' % (arg, arg)

    if op_style in (OpStyle.COUNT, OpStyle.PAIR_COUNT,
                    OpStyle.STAR):
        if ns_label is not None:
            return '%d (%s)' % (arg, ns_label)
        return str(arg)

    return str(arg)


def _local_and_closure_syms(code):
    """
    Return (local_syms tuple, closure_syms tuple) from code.namespace_spec.
    Closure syms include both closure_syms and global_syms (all captured refs).
    """

    nspec = code.namespace_spec
    if nspec is None:
        return (), ()

    locals_seq = nspec.local_syms
    closure_seq = nspec.closure_syms
    global_seq = nspec.global_syms
    all_captured = tuple(closure_seq) + tuple(global_seq)
    return locals_seq, all_captured


def dis(x, file=None):
    """
    Print a disassembly of a Koskari Code or Function object.

    For Function: prints a leading section with function instance details
    (name, formals, base_ns closures), then the disassembly of its code.

    For BoundMethod: prints a bound-method section (__self__), then the
    same function-instance section and code disassembly as for __func__.

    For Code: output includes constant pool (with NamespaceSpec entries
    expanded to show their locals and closures), vars (local_syms from
    namespace_spec), closure refs (closure_syms from namespace_spec),
    and indexed instructions with decoded arguments. FAST_* instructions
    show whether the slot is a local or closure. PUSH_NS and PUSH_CLASS
    instructions are annotated with the namespace ID (ns#K) and name of
    the NamespaceSpec they push.

    :param x: Code, Expression (unwraps to Code), Function, or BoundMethod
    :param file: file-like object to print to (default: stdout)
    """

    if isinstance(x, Function):
        code = x.code
        if file is None:
            import sys
            file = sys.stdout
        _write_function_instance_section(file, x)
    elif isinstance(x, BoundMethod):
        fn = _boundmethod_inner_function(x)
        code = fn.code
        if file is None:
            import sys
            file = sys.stdout
        _write_bound_method_section(file, x)
        _write_function_instance_section(file, fn)
    else:
        code = _get_code(x)
        if code is None:
            raise TypeError(
                'expected Code, Expression, Function, or BoundMethod, got %s'
                % type(x).__name__)
    if file is None:
        import sys
        file = sys.stdout

    cpool = code.constants_pool
    local_syms, closure_syms = _local_and_closure_syms(code)
    ns_const_map = _build_ns_const_map(code)

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
        if isinstance(val, NamespaceSpec):
            _write_nspec_cpool_entry(file, i, val)
        else:
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
        op = OpCode(op_int)
        op_style = op.style
        extra_schema = op.extra
        ns_label = None
        if i in ns_const_map:
            ns_label = _format_nspec_inline(ns_const_map[i], cpool[ns_const_map[i]])
        arg_str = _format_arg(
            code, op_style, arg, cpool, local_syms, closure_syms,
            ns_label=ns_label,
            extra_schema=extra_schema,
        )
        pos_str = ''
        try:
            line, col = code.position(i)
            pos_str = '  # line %d, col %d' % (line, col)
        except (IndexError, TypeError):
            pass
        file.write('  %3d  %-24s %s%s\n' % (i, op.name, arg_str, pos_str))
    file.write('\n')


def _get_instruction_lines(code):
    """
    Yield (index, op_name, arg_str) for each instruction (for tests/API)
    """

    cpool = code.constants_pool
    local_syms, closure_syms = _local_and_closure_syms(code)
    ns_const_map = _build_ns_const_map(code)
    n = code.length()
    for i in range(n):
        op_int, arg = code.oparg(i)
        op = OpCode(op_int)
        op_style = op.style
        extra_schema = op.extra
        ns_label = None
        if i in ns_const_map:
            ns_label = _format_nspec_inline(ns_const_map[i], cpool[ns_const_map[i]])
        arg_str = _format_arg(
            code, op_style, arg, cpool, local_syms, closure_syms,
            ns_label=ns_label,
            extra_schema=extra_schema,
        )
        yield i, op.name, arg_str


# The end.
