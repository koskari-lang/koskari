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
"""


from ...bootstrap import builtin
from ...exceptions import ParametersError, SpecialError
from ...lib import OpCode
from ...matcher import CALLEDBY, MATCH, REST, SYMBOL, VALUE
from ...syntax import special
from ..helpers import (
    gather_parameters, helper_begin, strip_leading_docstring)


_CLASS_PATTERNS = [
    MATCH(
        CALLEDBY("called_by"),
        SYMBOL("name"),
        VALUE("supers"),
        REST("body")),
]


def _extract_metaclass(kwds_seq):
    """
    Extract metaclass expression from kwds_seq. Returns (metaclass_expr, rest)
    where rest excludes the meta entry.
    """

    for i, item in enumerate(kwds_seq):
        if item[0] == "meta":
            return item[1], kwds_seq[:i] + kwds_seq[i + 1:]
    return None, kwds_seq


def _compiling_module_name(compiler):
    """
    Return the compiling context's module name for __module__ injection, or
    None if no non-empty string is available.
    """

    modname = getattr(compiler.module, "__name__", compiler.name)
    return str(modname) if modname else None


@builtin("compile/class")
@special("compile/class", patterns=_CLASS_PATTERNS, unpack="kwargs")
def special_class(compiler, called_by, name, supers, body):
    """
    Compile (compile/class NAME (SUPERS) BODY...). Creates a class via
    PUSH_COLLECT/POP_COLLECT/BUILD_CLASS, runs body in CollectorNamespace,
    and leaves the class on the stack. NAME is the class's name; no
    variable is defined. SUPERS may include superclass expressions and
    optional meta: Metaclass.
    """

    pos = called_by.position
    head = called_by.datum
    name = str(name.datum)
    supers_form = supers.datum
    body = body.datum

    try:
        args_seq, kwds_seq = gather_parameters(
            supers_form, source=supers_form)
    except ParametersError as e:
        raise SpecialError(str(e), supers_form, position=pos)

    metaclass_expr, extra_kwds = _extract_metaclass(kwds_seq)
    if extra_kwds:
        raise SpecialError(
            "compile/class supers only supports meta: keyword, not %s"
            % [k for k, _ in extra_kwds],
            supers_form, position=pos)

    has_star = any(x[0] == "*" for x in args_seq)
    if has_star:
        raise SpecialError(
            "compile/class supers does not support *args",
            supers_form, position=pos)

    bases = [expr for _, expr in args_seq]

    doc, body = strip_leading_docstring(body)

    frag = compiler.fragment(head)
    frag.position(pos)

    # Build pos tuple: (name, bases) or (name, bases, metaclass)
    # Stack order for BUILD_ARGS: first pushed is last popped
    frag.instruction(OpCode.CONST, name)

    if bases:
        for base_expr in bases:
            frag.merge(compiler.full_compile(base_expr))
        frag.instruction(OpCode.BUILD_ARGS, len(bases))
    else:
        frag.instruction(OpCode.CONST, ())

    if metaclass_expr is not None:
        frag.merge(compiler.full_compile(metaclass_expr))
        frag.instruction(OpCode.BUILD_ARGS, 3)
    else:
        frag.instruction(OpCode.BUILD_ARGS, 2)

    with frag.collector_namespace(head) as body_frag:
        # The class body scope owns __class__, as CPython's does: methods
        # defined in the body close over this slot, and the class builder
        # fills it once the class object exists, so (super) and a bare
        # __class__ inside a method name the defining class.
        body_frag.declare_var("__class__")
        helper_begin(body_frag, body)

        if doc and "__doc__" not in body_frag.declares:
            # don't override an existing __doc__, just in case
            # some code has pre-built it.

            doc_frag = compiler.fragment("__doc__")
            doc_frag.declare_var("__doc__")
            doc_frag.instruction(OpCode.CONST, doc)
            doc_frag.instruction(OpCode.VAR_STORE, "__doc__")
            body_frag.merge(doc_frag)

        if "__qualname__" not in body_frag.declares:
            # CPython-shaped: scope-only name; module path goes in __module__.
            # Nested Outer.Inner needs a future nesting stack.

            qn_frag = compiler.fragment("__qualname__")
            qn_frag.declare_var("__qualname__")
            qn_frag.instruction(OpCode.CONST, name)
            qn_frag.instruction(OpCode.VAR_STORE, "__qualname__")
            body_frag.merge(qn_frag)

        mod_for_class = _compiling_module_name(compiler)
        if (mod_for_class is not None
                and "__module__" not in body_frag.declares):
            mod_frag = compiler.fragment("__module__")
            mod_frag.declare_var("__module__")
            mod_frag.instruction(OpCode.CONST, mod_for_class)
            mod_frag.instruction(OpCode.VAR_STORE, "__module__")
            body_frag.merge(mod_frag)

        binds = body_frag.annotation_bindings
        if binds and "__annotations__" not in body_frag.declares:
            # don't override an existing __annotations__, just in case
            # some code has pre-built it.

            annos = compiler.fragment("__annotations__")
            annos.declare_var("__annotations__")

            collapsed = dict(binds)
            for name, type_expr in collapsed.items():
                annos.merge(compiler.full_compile(type_expr))

            annos.instruction(OpCode.BUILD_KWDS_NAMES,
                              tuple(collapsed.keys()))
            annos.instruction(OpCode.VAR_STORE, "__annotations__")
            body_frag.merge(annos)

    frag.instruction(OpCode.BUILD_CLASS, 0)
    return frag


# The end.
