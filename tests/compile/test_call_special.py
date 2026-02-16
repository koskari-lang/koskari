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
Tests for compile/call special: tail position emits TAIL_CALL; non-tail uses CALL
"""


import unittest

from koskari.compile import Compiler, CompilerMode
from koskari.compile.specials import SPECIALS
from koskari.lib import FunctionSpec, OpCode
from koskari.parse import default_reader, source_str
from koskari.types import Module, ModuleNamespace


def get_glbls(**defns):
    builtins = {}
    builtins.update(defns)
    return ModuleNamespace("test", builtins)


def parse_source(src_str, lineno=1):
    stream = source_str(src_str, __file__, line_no=lineno)
    return default_reader.read(stream)


def compile_to_code(src_str, lineno=1, **defns):
    """
    Compile source to a Code object so we can inspect opcodes
    """

    mod_ns = get_glbls(**defns)
    module = Module(mod_ns)
    ns = vars(module)
    for sym, fn in SPECIALS.items():
        name = str(sym)
        if not name.startswith("compile/"):
            name = "compile/" + name
        r = ns.ref(name)
        r.store(fn)

    source = parse_source(src_str, lineno=lineno)
    compiler = Compiler("<unittest>", module, filename=__file__)
    fragment = compiler.full_compile(source, tc=True)
    return compiler.complete(fragment, mode=CompilerMode.EXPRESSION)


def call_ops(code):
    """
    List of (index, opcode, arg) for CALL and TAIL_CALL in code
    """
    return [(i, op, arg) for i in range(code.length())
            for op, arg in [code.oparg(i)]
            if op in (OpCode.KOS_OP_CALL, OpCode.KOS_OP_TAIL_CALL)]


def lambda_body_code(code):
    """
    Code object for the lambda body (constant 0 is the FunctionSpec)
    """
    fspec = code.constant(0)
    assert isinstance(fspec, FunctionSpec)
    return fspec.code


class TestCallSpecialTailPosition(unittest.TestCase):
    """
    compile/call in tail position emits TAIL_CALL; otherwise CALL
    """

    def test_lambda_body_tail_call_one_arg(self):
        code = compile_to_code("(lambda [x] (f x))")
        ops = call_ops(lambda_body_code(code))
        self.assertEqual(len(ops), 1)
        self.assertEqual(ops[0][1], OpCode.KOS_OP_TAIL_CALL)
        self.assertEqual(ops[0][2], 1)

    def test_lambda_body_tail_call_zero_args(self):
        code = compile_to_code("(lambda [] (f))")
        ops = call_ops(lambda_body_code(code))
        self.assertEqual(len(ops), 1)
        self.assertEqual(ops[0][1], OpCode.KOS_OP_TAIL_CALL)
        self.assertEqual(ops[0][2], 0)

    def test_lambda_begin_first_call_then_tail_call(self):
        code = compile_to_code("(lambda [x] (begin (f 1) (g x)))")
        ops = call_ops(lambda_body_code(code))
        self.assertEqual(len(ops), 2)
        self.assertEqual(ops[0][1], OpCode.KOS_OP_CALL)
        self.assertEqual(ops[0][2], 1)
        self.assertEqual(ops[1][1], OpCode.KOS_OP_TAIL_CALL)
        self.assertEqual(ops[1][2], 1)

    def test_begin_single_form_tail_call(self):
        code = compile_to_code("(begin (f 1))")
        ops = call_ops(code)
        self.assertEqual(len(ops), 1)
        self.assertEqual(ops[0][1], OpCode.KOS_OP_TAIL_CALL)
        self.assertEqual(ops[0][2], 1)

    def test_begin_f_x_x_call_not_tail(self):
        """
        (begin (f x) x) -> f is not in tail position -> CALL
        """
        code = compile_to_code("(begin (f x) x)")
        ops = call_ops(code)
        self.assertEqual(len(ops), 1)
        self.assertEqual(ops[0][1], OpCode.KOS_OP_CALL)
        self.assertEqual(ops[0][2], 1)

    def test_begin_x_f_x_tail_call(self):
        """
        (begin x (f x)) -> f is in tail position -> TAIL_CALL
        """
        code = compile_to_code("(begin x (f x))")
        ops = call_ops(code)
        self.assertEqual(len(ops), 1)
        self.assertEqual(ops[0][1], OpCode.KOS_OP_TAIL_CALL)
        self.assertEqual(ops[0][2], 1)


# The end.
