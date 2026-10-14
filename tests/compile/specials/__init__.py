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


from textwrap import dedent

import unittest

import koskari.compile.specials  # noqa: F401 - populate _BUILTIN_REGISTRY
from koskari.bootstrap import _BUILTIN_REGISTRY
from koskari.compile import Compiler, CompilerMode
from koskari.matcher import get_extern_defaults
from koskari.lib import (
    FunctionSpec, Module, ModuleNamespace, OpCode, bracket_cons, build_dict)
from koskari.reader import (
    IndentAwareStream, LayoutReader, default_reader, source_str)
from koskari.types import bracket_splice


def _register_specials(ns, specials):
    for sym, fn in specials.items():
        ns.store(str(sym), fn)
    # bracket-cons and bracket-splice are builtins a real module
    # namespace has bound; quasiquote of a bracket form emits the
    # BRACKET_* opcodes and calls neither.
    ns.store("bracket-cons", bracket_cons)
    ns.store("bracket-splice", bracket_splice)
    # the #{ } literal lowers to a plain call of the #dict builtin; a
    # real module namespace has it bound.
    ns.store("#dict", build_dict)


class Suspend:
    """
    An awaitable that yields once, then completes on the next send
    """

    def __await__(self):
        yield self
        return "suspended"


class SpecialsTestCase(unittest.IsolatedAsyncioTestCase):
    """
    TestCase subclass with helpers for compiling koskari lisp into expressions
    """

    def get_glbls(self, **defns):
        # a syntax definition's matcher closes over the types its
        # pattern names, symbol and pair among them; a real module
        # namespace reaches them through its builtins. A test's own
        # definitions shadow them.
        builtins = dict(get_extern_defaults())
        builtins.update(defns)
        return ModuleNamespace("test", builtins)


    def parse_source(self, src_str, lineno=1, reader=None):
        stream = IndentAwareStream.from_source(
            source_str(dedent(src_str), __file__, line_no=lineno))
        if reader is None:
            reader = default_reader
        return reader.read(stream)


    def compile_expr(
            self, src_str, lineno=1, specials=_BUILTIN_REGISTRY,
            resolution_mode=None, resolution_instance=None,
            **defns):
        """
        Compile source to an expression; specials=None uses _BUILTIN_REGISTRY
        """

        mod_ns = self.get_glbls(**defns)
        module = Module(mod_ns)
        ns = vars(module)

        _register_specials(ns, specials)

        source = self.parse_source(src_str, lineno=lineno, reader=LayoutReader(ns))

        kwargs = {}
        if resolution_mode is not None:
            kwargs['resolution_mode'] = resolution_mode
        compiler = Compiler("<unittest>", module, filename=__file__, **kwargs)
        if resolution_instance is not None:
            compiler._resolution_instance = resolution_instance
        fragment = compiler.full_compile(source)
        expr = fragment.as_expression(mod_ns)
        return expr, module


    def compile_to_code(
            self, src_str, lineno=1,
            resolution_mode=None, resolution_instance=None,
            **defns):
        """
        Compile source to a Code object for opcode inspection. Tail-call
        promotion runs in the processor for FUNCTION-mode streams; a test
        that needs it off sets the compiler/tco-enable pragma
        """

        mod_ns = self.get_glbls(**defns)
        module = Module(mod_ns)
        ns = vars(module)
        _register_specials(ns, _BUILTIN_REGISTRY)

        source = self.parse_source(src_str, lineno=lineno)
        kwargs = {}
        if resolution_mode is not None:
            kwargs['resolution_mode'] = resolution_mode
        compiler = Compiler("<unittest>", module, filename=__file__, **kwargs)
        if resolution_instance is not None:
            compiler._resolution_instance = resolution_instance
        fragment = compiler.full_compile(source)
        return compiler.complete(fragment, mode=CompilerMode.EXPRESSION)


    CALL_OPCODES = (
        OpCode.CALL,
        OpCode.TAIL_CALL,
        OpCode.CALL_NAMES,
        OpCode.TAIL_CALL_NAMES,
        OpCode.CALL_STAR,
        OpCode.TAIL_CALL_STAR,
        OpCode.CALL_VAR,
        OpCode.TAIL_CALL_VAR,
        OpCode.CALL_VAR_NAMES,
        OpCode.TAIL_CALL_VAR_NAMES,
        OpCode.CALL_VAR_STAR,
        OpCode.TAIL_CALL_VAR_STAR,
        OpCode.CALL_FAST,
        OpCode.TAIL_CALL_FAST,
        OpCode.CALL_FAST_NAMES,
        OpCode.TAIL_CALL_FAST_NAMES,
        OpCode.CALL_FAST_STAR,
        OpCode.TAIL_CALL_FAST_STAR,
        OpCode.CALL_ATTR,
        OpCode.TAIL_CALL_ATTR,
        OpCode.CALL_ATTR_NAMES,
        OpCode.TAIL_CALL_ATTR_NAMES,
        OpCode.CALL_ATTR_STAR,
        OpCode.TAIL_CALL_ATTR_STAR,
        OpCode.CALL_VAR_ATTR,
        OpCode.TAIL_CALL_VAR_ATTR,
        OpCode.CALL_VAR_ATTR_NAMES,
        OpCode.TAIL_CALL_VAR_ATTR_NAMES,
        OpCode.CALL_VAR_ATTR_STAR,
        OpCode.TAIL_CALL_VAR_ATTR_STAR,
        OpCode.CALL_FAST_ATTR,
        OpCode.TAIL_CALL_FAST_ATTR,
        OpCode.CALL_FAST_ATTR_NAMES,
        OpCode.TAIL_CALL_FAST_ATTR_NAMES,
        OpCode.CALL_FAST_ATTR_STAR,
        OpCode.TAIL_CALL_FAST_ATTR_STAR,
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
            OpCode.BUILD_ARGS,
            OpCode.COMBINE_ARGS,
            OpCode.BUILD_KWDS_NAMES,
            OpCode.COMBINE_KWDS,
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
