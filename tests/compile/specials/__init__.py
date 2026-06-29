# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.

"""
tests.compile.specials

Base class and helpers for testing koskari.compile.specials handlers.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import unittest

import koskari.compile.specials  # noqa: F401 - populate _BUILTIN_REGISTRY

from koskari.compile import Compiler, CompilerMode
from koskari.bootstrap import _BUILTIN_REGISTRY
from koskari.lib import FunctionSpec, OpCode, bracket_cons
from koskari.reader import LayoutReader, default_reader, source_str
from koskari.lib import Module, ModuleNamespace

def _register_specials(ns, specials):
    for sym, fn in specials.items():
        ns.store(str(sym), fn)
    # quote/quasiquote of bracket forms reconstructs via a runtime
    # bracket-cons call; a real module namespace has it bound.
    ns.store("bracket-cons", bracket_cons)


class SpecialsTestCase(unittest.IsolatedAsyncioTestCase):
    """
    TestCase subclass with helpers for compiling koskari lisp into expressions
    """

    def get_glbls(self, **defns):
        builtins = {}
        builtins.update(defns)
        return ModuleNamespace("test", builtins)


    def parse_source(self, src_str, lineno=1, reader=None):
        stream = source_str(src_str, __file__, line_no=lineno)
        if reader is None:
            reader = default_reader
        return reader.read(stream)


    def compile_expr(self, src_str, lineno=1, specials=_BUILTIN_REGISTRY,
                     resolution_pragma_class=None, **defns):
        """
        Compile source to an expression; specials=None uses _BUILTIN_REGISTRY
        """

        mod_ns = self.get_glbls(**defns)
        module = Module(mod_ns)
        ns = vars(module)

        _register_specials(ns, specials)

        source = self.parse_source(src_str, lineno=lineno, reader=LayoutReader(ns))

        kwargs = {}
        if resolution_pragma_class is not None:
            kwargs['resolution_pragma_class'] = resolution_pragma_class
        compiler = Compiler("<unittest>", module, filename=__file__, **kwargs)
        fragment = compiler.full_compile(source)
        expr = fragment.as_expression(mod_ns)
        return expr, module


    def compile_to_code(self, src_str, lineno=1, tc=False,
                        resolution_pragma_class=None, resolution_pragma=None,
                        **defns):
        """
        Compile source to a Code object for opcode inspection; tc=True enables
        tail-call tracking
        """

        mod_ns = self.get_glbls(**defns)
        module = Module(mod_ns)
        ns = vars(module)
        _register_specials(ns, _BUILTIN_REGISTRY)

        source = self.parse_source(src_str, lineno=lineno)
        kwargs = {}
        if resolution_pragma_class is not None:
            kwargs['resolution_pragma_class'] = resolution_pragma_class
        if resolution_pragma is not None:
            kwargs['resolution_pragma'] = resolution_pragma
        compiler = Compiler("<unittest>", module, filename=__file__, **kwargs)
        fragment = compiler.full_compile(source, tc=tc)
        return compiler.complete(fragment, mode=CompilerMode.EXPRESSION)


    CALL_OPCODES = (
        OpCode.KOS_OP_CALL,
        OpCode.KOS_OP_TAIL_CALL,
        OpCode.KOS_OP_CALL_NAMES,
        OpCode.KOS_OP_TAIL_CALL_NAMES,
        OpCode.KOS_OP_CALL_STAR,
        OpCode.KOS_OP_TAIL_CALL_STAR,
        OpCode.KOS_OP_CALL_VAR,
        OpCode.KOS_OP_CALL_VAR_NAMES,
        OpCode.KOS_OP_CALL_VAR_STAR,
        OpCode.KOS_OP_CALL_FAST,
        OpCode.KOS_OP_CALL_FAST_NAMES,
        OpCode.KOS_OP_CALL_FAST_STAR,
    )


    def call_ops(self, code):
        """
        List of (index, opcode, arg) for call opcodes in code
        """

        return [(i, op, arg) for i in range(code.length())
                for op, arg in [code.oparg(i)]
                if op in self.CALL_OPCODES]


    def arg_building_ops(self, code):
        """
        List of (index, opcode, arg) for BUILD_ARGS, COMBINE_ARGS,
        BUILD_KWDS_NAMES, COMBINE_KWDS in code
        """

        arg_ops = (
            OpCode.KOS_OP_BUILD_ARGS,
            OpCode.KOS_OP_COMBINE_ARGS,
            OpCode.KOS_OP_BUILD_KWDS_NAMES,
            OpCode.KOS_OP_COMBINE_KWDS,
        )
        return [(i, op, arg) for i in range(code.length())
                for op, arg in [code.oparg(i)]
                if op in arg_ops]


    def lambda_body_code(self, code):
        """
        Code object for the lambda body (constant 0 is the FunctionSpec)
        """

        fspec = code.constant(0)
        self.assertIsInstance(fspec, FunctionSpec)
        return fspec.code


# The end.
