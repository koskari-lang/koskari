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
koskari.serialize.softcode

Hardened Code to softened instruction streams and back, with FAST slot
validation against NamespaceSpec.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Auto via Cursor
"""


from typing import Tuple, Union

from koskari.lib import Code, OpCode, OpStyle


SoftArg = Union[
    int,
    str,
    Tuple[object, object, object, object],
    Tuple[int, int],
]


def fast_slot_count(nspec):
    """
    Number of fast slots for NamespaceSpec ordering used by convert_fast.

    :param nspec: NamespaceSpec instance
    :return: local + closure + global symbol count
    """

    return (
        len(nspec.local_syms)
        + len(nspec.closure_syms)
        + len(nspec.global_syms))


def _collect_label_targets(code):
    """
    Collect all instruction indices used as jump targets from LABEL-style args.

    :param code: hardened Code
    :return: set of pc integers
    """

    targets = set()
    n = code.length()
    for i in range(n):
        op_int, arg = code.oparg(i)
        op = OpCode(op_int)
        st = getattr(op.style, 'value', op.style)
        if st == OpStyle.KOSTYLE_LABEL:
            targets.add(int(arg))
        elif st == OpStyle.KOSTYLE_EXTRA:
            schema = tuple(op.extra) if getattr(op, 'extra', None) else None
            if not schema:
                continue
            row = code.extra_tuple(arg)
            for val, fk in zip(row, schema):
                fk = getattr(fk, 'value', fk)
                if fk == OpStyle.KOSTYLE_LABEL:
                    targets.add(int(val))
    return targets


def soften_code(code):
    """
    Convert hardened Code to a softened instruction list with LABEL and
    POSITION pseudo-ops and string label names for branch targets.

    FAST opcodes are preserved with integer slot indices (per project policy).

    :param code: koskari Code instance
    :return: tuple (instructions, label_count) where instructions is a list
        of (OpCode, arg) pairs suitable for Code(...)
    """

    targets = _collect_label_targets(code)
    label_for_pc = {}
    for j, pc in enumerate(sorted(targets)):
        label_for_pc[pc] = 'L%d' % j
    label_count = len(label_for_pc)

    n = code.length()
    out = []
    prev_pos = None
    for i in range(n):
        line, col = code.position(i)
        cur_pos = (line, col)
        if prev_pos is None or cur_pos != prev_pos:
            out.append((OpCode.KOS_OP_POSITION, (line, col)))
            prev_pos = cur_pos

        if i in label_for_pc:
            out.append((OpCode.KOS_OP_LABEL, label_for_pc[i]))

        op_int, arg = code.oparg(i)
        op = OpCode(op_int)
        st = getattr(op.style, 'value', op.style)
        if st == OpStyle.KOSTYLE_CONST:
            val = code.constant(arg)
            out.append((op, val))
        elif st == OpStyle.KOSTYLE_VAR:
            val = code.constant(arg)
            out.append((op, val))
        elif st == OpStyle.KOSTYLE_LABEL:
            out.append((op, label_for_pc[arg]))
        elif st == OpStyle.KOSTYLE_EXTRA:
            schema = tuple(op.extra) if getattr(op, 'extra', None) else None
            row = code.extra_tuple(arg)
            if not schema:
                out.append((op, row))
                continue
            elems = []
            for val, fk in zip(row, schema):
                fk = getattr(fk, 'value', fk)
                if fk == OpStyle.KOSTYLE_CONST:
                    elems.append(code.constant(val))
                elif fk == OpStyle.KOSTYLE_VAR:
                    elems.append(code.constant(val))
                elif fk == OpStyle.KOSTYLE_LABEL:
                    elems.append(label_for_pc[int(val)])
                else:
                    elems.append(int(val))
            out.append((op, tuple(elems)))
        else:
            out.append((op, int(arg)))

    return out, label_count


def validate_fast_indices(nspec, instructions):
    """
    Ensure every FASTVAR slot in instructions is within NamespaceSpec bounds.

    :param nspec: NamespaceSpec for the code body
    :param instructions: softened instruction list
    :raises ValueError: if any index is out of range
    """

    limit = fast_slot_count(nspec)
    for op, arg in instructions:
        op = op if isinstance(op, OpCode) else OpCode(op)
        st = getattr(op.style, 'value', op.style)
        if st == OpStyle.KOSTYLE_FASTVAR:
            if int(arg) < 0 or int(arg) >= limit:
                raise ValueError(
                    'FAST index %s out of bounds (limit %s)' % (arg, limit))
        elif st == OpStyle.KOSTYLE_EXTRA:
            schema = tuple(op.extra) if getattr(op, 'extra', None) else None
            if not schema:
                continue
            if not isinstance(arg, tuple):
                continue
            for val, fk in zip(arg, schema):
                fk = getattr(fk, 'value', fk)
                if fk == OpStyle.KOSTYLE_FASTVAR:
                    if int(val) < 0 or int(val) >= limit:
                        raise ValueError(
                            'FAST index %s out of bounds (limit %s)'
                            % (val, limit))


def reharden_code(nspec, instructions, name, filename):
    """
    Build a hardened Code from a softened instruction list.

    :param nspec: NamespaceSpec
    :param instructions: list of (OpCode, arg) pairs
    :param name: function or module fragment name
    :param filename: source filename string
    :return: Code instance
    """

    validate_fast_indices(nspec, instructions)
    return Code(nspec, instructions, name, filename)


# The end.
