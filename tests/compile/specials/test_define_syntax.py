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
Tests for compile/syntax-rules and compile/macro-rules specials (with
compile/define and build-syntax-rules / build-macro-rules).

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from koskari.syntax import SyntaxRulesTransformer, build_macro_syntax, build_syntax
from koskari.dis import dis
from koskari.lib import OpCode

from . import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_specials


class TestCompileSyntaxRulesAndMacroRules(SpecialsTestCase):

    def compile_expr(self, src_str, **defns):
        defns.setdefault('build-syntax-rules', build_syntax)
        defns.setdefault('build-macro-rules', build_macro_syntax)
        return super().compile_expr(src_str, **defns)

    def compile_to_code(self, src_str, lineno=1, tc=False,
                        resolution_pragma_class=None, resolution_pragma=None,
                        **defns):
        defns.setdefault('build-syntax-rules', build_syntax)
        defns.setdefault('build-macro-rules', build_macro_syntax)
        return super().compile_to_code(
            src_str, lineno=lineno, tc=tc,
            resolution_pragma_class=resolution_pragma_class,
            resolution_pragma=resolution_pragma,
            **defns)

    async def test_compile_syntax_rules_unless_macro(self):
        src_str = """
        (compile/define unless
          (compile/syntax-rules unless (else:)
            ((unless test body ... else: alt)
             (compile/cond [test alt] [else: (compile/begin body ...)]))
            ((unless test body ...)
             (compile/cond [test None] [else: (compile/begin body ...)]))))
        """
        expr, mod = self.compile_expr(src_str)
        dis(expr.code)

        await expr()
        defn = {'compile/unless': vars(mod)['unless']}

        src_str = """
        (compile/begin
          (unless True 10)
          (unless False 40))
        """
        expr, mod = self.compile_expr(src_str, **defn)
        dis(expr.code)

        result = await expr()
        self.assertEqual(40, result)

    async def test_compile_syntax_rules_no_matching_rule_raises(self):
        src_str = """
        (compile/define only-one
          (compile/syntax-rules only-one ()
            ((only-one x) x)))
        """
        expr, mod = self.compile_expr(src_str)
        await expr()
        defn = {'compile/only-one': vars(mod)['only-one']}

        src_str = """
        (only-one 1 2)
        """
        with self.assertRaises(Exception):
            self.compile_expr(src_str, **defn)

    def test_compile_syntax_rules_tagged_tuple_constant(self):
        code = self.compile_to_code(
            """
            (compile/define m
              (compile/syntax-rules m ()
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

    async def test_compile_macro_rules_identity(self):
        src_str = """
        (compile/define ident
          (compile/macro-rules ident ()
            ((ident x) x)))
        """
        expr, mod = self.compile_expr(src_str)
        dis(expr.code)

        await expr()
        defn = {'compile/ident': vars(mod)['ident']}

        src_str = """
        (ident 42)
        """
        expr, mod = self.compile_expr(src_str, **defn)
        dis(expr.code)

        result = await expr()
        self.assertEqual(42, result)


# The end.
