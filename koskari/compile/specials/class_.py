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
koskari.compile.specials.class_

Special form handler for compile/class.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


from ...lib import nil, OpCode, pair, symbol
from ...types import is_pair
from ...exceptions import ParametersError, SpecialError
from ..compiler import Compiler
from ..helpers import gather_parameters_ng, helper_begin, is_symbolish, special


def _extract_metaclass(kwds_seq):
    """
    Extract metaclass expression from kwds_seq. Returns (metaclass_expr, rest)
    where rest excludes the meta entry.
    """

    for i, item in enumerate(kwds_seq):
        if item[0] == "meta":
            return item[1], kwds_seq[:i] + kwds_seq[i + 1:]
    return None, kwds_seq


@special(symbol("compile/class"))
def special_class(compiler, source, tc=False):
    """
    Compile (compile/class NAME (SUPERS) BODY...). Creates a class via
    PUSH_CLASS/POP_CLASS, runs body in CollectorNamespace, binds result to NAME.
    SUPERS may include superclass expressions and optional meta: Metaclass.
    """

    called_by, rest = source
    if rest is nil or not is_pair(rest):
        raise SpecialError("compile/class requires name and supers", source)

    name_form, rest_tail = rest
    if not is_symbolish(name_form):
        raise SpecialError("compile/class name must be a symbol", source)

    name = str(name_form)

    if rest_tail is nil or not is_pair(rest_tail):
        raise SpecialError("compile/class requires supers list and body", source)

    supers, body = rest_tail

    try:
        args_seq, kwds_seq = gather_parameters_ng(supers, source=supers)
    except ParametersError as e:
        raise SpecialError(str(e), source)

    metaclass_expr, extra_kwds = _extract_metaclass(kwds_seq)
    if extra_kwds:
        raise SpecialError(
            "compile/class supers only supports meta: keyword, not %s"
            % [k for k, _ in extra_kwds],
            source)

    has_star = any(x[0] == "*" for x in args_seq)
    if has_star:
        raise SpecialError(
            "compile/class supers does not support *args", source)

    bases = [expr for _, expr in args_seq]

    frag = compiler.fragment(called_by)
    frag.position_of(source)
    frag.declare_var(name)

    # Build pos tuple: (name, bases) or (name, bases, metaclass)
    # Stack order for BUILD_ARGS: first pushed is last popped
    frag.instruction(OpCode.KOS_OP_CONST, name)

    if bases:
        for base_expr in bases:
            frag.merge(compiler.full_compile(base_expr, False))
        frag.instruction(OpCode.KOS_OP_BUILD_ARGS, len(bases))
    else:
        frag.instruction(OpCode.KOS_OP_CONST, ())

    if metaclass_expr is not None:
        frag.merge(compiler.full_compile(metaclass_expr, False))
        frag.instruction(OpCode.KOS_OP_BUILD_ARGS, 3)
    else:
        frag.instruction(OpCode.KOS_OP_BUILD_ARGS, 2)

    frag.instruction(OpCode.KOS_OP_PUSH_CLASS, 1)
    helper_begin(frag, body, tc)
    frag.instruction(OpCode.KOS_OP_POP_CLASS, 0)
    frag.instruction(OpCode.KOS_OP_VAR_BIND, name)
    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


# The end.
