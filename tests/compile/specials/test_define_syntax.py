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
Tests for koskari.compile define-syntax special form.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from koskari.compile.syntax import SyntaxRulesTransformer, build_syntax
from koskari.dis import dis
from koskari.lib import OpCode

from . import SpecialsTestCase


class TestDefineSyntax(SpecialsTestCase):

    def compile_expr(self, src_str, **defns):
        defns["build-syntax-rules"] = build_syntax
        return super().compile_expr(src_str, **defns)


    async def test_define_syntax_unless_with_literal_else(self):
        src_str = """
        (compile/define-syntax unless
          (syntax-rules (else:)
            ((unless test body ... else: alt)
             (if test alt (compile/begin body ...)))
            ((unless test body ...)
             (if test None (compile/begin body ...)))))
        """
        expr, mod = self.compile_expr(src_str)
        dis(expr.code)

        await expr()
        defn = {"compile/unless": vars(mod)["unless"]}

        src_str = """
        (compile/begin
          (unless True 10)
          (unless False 40))
        """
        expr, mod = self.compile_expr(src_str, **defn)
        dis(expr.code)

        result = await expr()
        self.assertEqual(40, result)


    async def test_hygienic_renaming_for_introduced_symbols(self):
        src_str = """
        (compile/define-syntax use-temp
          (syntax-rules ()
            ((use-temp x)
              (let [[t 99]]
                (+ t x)))))
        """
        expr, mod = self.compile_expr(src_str)
        dis(expr.code)

        await expr()
        defn = {"compile/use-temp": vars(mod)["use-temp"]}

        src_str = """
        (begin
          (define t 2)
          (use-temp 5)
          t)
        """
        expr, mod = self.compile_expr(src_str, **defn)
        dis(expr.code)

        result = await expr()
        self.assertEqual(2, result)


    async def test_rule_without_match_raises(self):
        src_str = """
        (compile/define-syntax only-one
          (syntax-rules ()
            ((only-one x) x)))
        """
        expr, mod = self.compile_expr(src_str)
        await expr()
        defn = {"compile/only-one": vars(mod)["only-one"]}

        src_str = """
        (only-one 1 2)
        """
        with self.assertRaises(Exception):
            self.compile_expr(src_str, **defn)


    async def test_define_syntax_uses_tagged_tuple_constant(self):
        code = self.compile_to_code(
            """
            (compile/define-syntax m
              (syntax-rules ()
                ((m x) x)))
            """
        )

        consts = []
        for i in range(code.length()):
            op, arg = code.oparg(i)
            if op == OpCode.KOS_OP_CONST:
                consts.append(code.constant(arg))

        self.assertFalse(any(isinstance(v, SyntaxRulesTransformer) for v in consts))
        self.assertTrue(any(isinstance(v, tuple) for v in consts))


# The end.
