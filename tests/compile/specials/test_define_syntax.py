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
Integration tests for compile/define with syntax and macro transformers.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import pytest

from koskari.dis import dis
from koskari.lib import OpCode, build_list
from koskari.syntax import (Syntax, build_macro_rules, build_special_rules,
                               build_syntax_rules, parse_match_pattern,
                               parse_rule_pattern)

from . import SpecialsTestCase


pytestmark = pytest.mark.phase_specials


class DefineSyntaxCompileHelpers(SpecialsTestCase):

    def compile_expr(self, src_str, **defns):
        defns.setdefault('build-syntax-rules', build_syntax_rules)
        defns.setdefault('build-macro-rules', build_macro_rules)
        defns.setdefault('build-special-rules', build_special_rules)
        defns.setdefault('parse-match-pattern', parse_match_pattern)
        defns.setdefault('parse-rule-pattern', parse_rule_pattern)
        return super().compile_expr(src_str, **defns)


    def compile_to_code(self, src_str, lineno=1, tc=False,
                        resolution_pragma_class=None, resolution_pragma=None,
                        **defns):
        defns.setdefault('build-syntax-rules', build_syntax_rules)
        defns.setdefault('build-macro-rules', build_macro_rules)
        defns.setdefault('build-special-rules', build_special_rules)
        defns.setdefault('parse-match-pattern', parse_match_pattern)
        defns.setdefault('parse-rule-pattern', parse_rule_pattern)
        return super().compile_to_code(
            src_str, lineno=lineno, tc=tc,
            resolution_pragma_class=resolution_pragma_class,
            resolution_pragma=resolution_pragma,
            **defns)


class TestCompileSyntaxRules(DefineSyntaxCompileHelpers):

    async def test_compile_syntax_rules_unless_macro(self):
        src_str = """
        (compile/define unless
          (compile/syntax-rules unless [else:]
            [(unless test body ... else: alt)
             (compile/cond [test alt] [else: (compile/begin body ...)])]
            [(unless test body ...)
             (compile/cond [test None] [else: (compile/begin body ...)])]))
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

    async def test_compile_syntax_rules_compound_ellipsis_example_to_list(self):
        """
        ``(example fn (a b) ...)`` / ``(#list (fn a b) ...)`` repeats a compound
        template fragment; use-site expands to ``(#list (+ 1 2) ...)`` and
        evaluates like ``#[3 7 300 4]`` (``#list`` of those sums).
        """

        src_str = """
        (compile/define example
          (compile/syntax-rules example [#list]
            [(example fn (a b) ...)
             (#list (fn a b) ...)]))
        """
        expr, mod = self.compile_expr(src_str)
        dis(expr.code)

        await expr()
        defn = {
            'compile/example': vars(mod)['example'],
            '#list': build_list,
        }

        src_str = """
        (example + (1 2) (3 4) (100 200) (5 -1))
        """
        expr, mod = self.compile_expr(src_str, **defn)
        dis(expr.code)

        result = await expr()
        self.assertEqual([3, 7, 300, 4], list(result))


    async def test_compile_syntax_rules_no_matching_rule_raises(self):
        src_str = """
        (compile/define only-one
          (compile/syntax-rules only-one []
            [(only-one x) x]))
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
              (compile/syntax-rules m []
                [(m x) x]))
            """
        )

        consts = []
        for i in range(code.length()):
            op, arg = code.oparg(i)
            if op == OpCode.KOS_OP_CONST:
                consts.append(code.constant(arg))

        self.assertFalse(any(isinstance(v, Syntax) for v in consts))
        self.assertTrue(any(isinstance(v, tuple) for v in consts))


class TestCompileMacroRules(DefineSyntaxCompileHelpers):

    async def test_compile_macro_rules_identity(self):
        src_str = """
        (compile/define ident
          (compile/macro-rules ident []
            [(ident x) x]))
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


    async def test_compile_macro_rules_ellipsis_repeat_to_list(self):
        """
        ``macro-rules`` must bind ``xs ...`` to the pattern variable ``xs`` (not
        an internal gensym) and pass repeat captures so the macro body can
        splice them (e.g. ``(#list ,@xs)``). Regression for
        ``compile/function-compose``-style macros that iterate ``reverse`` over
        the repeated forms.
        """

        src_str = """
        (compile/define ellipsis-repro
          (compile/macro-rules ellipsis-repro []
            [(ellipsis-repro xs ...)
             `(#list ,@xs)]))
        """
        expr, mod = self.compile_expr(src_str, print=print)
        dis(expr.code)

        await expr()
        defn = {
            'compile/ellipsis-repro': vars(mod)['ellipsis-repro'],
            '#list': build_list,
        }

        src_str = """
        (ellipsis-repro 1 2 3)
        """
        expr, mod = self.compile_expr(src_str, **defn)
        dis(expr.code)

        result = await expr()
        self.assertEqual([1, 2, 3], list(result))


class TestCompileSyntaxMatch(DefineSyntaxCompileHelpers):

    async def test_compile_syntax_match_value_pattern(self):
        src_str = """
        (compile/define double
          (compile/syntax-match double
            [(double (VALUE x)) (+ x x)]))
        """
        expr, mod = self.compile_expr(src_str)
        dis(expr.code)

        await expr()
        defn = {'compile/double': vars(mod)['double']}

        src_str = """
        (double 21)
        """
        expr, mod = self.compile_expr(src_str, **defn)
        dis(expr.code)

        result = await expr()
        self.assertEqual(42, result)


    async def test_compile_syntax_match_repeat_value_elem_and_min_count_kwarg(self):
        src_str = """
        (compile/define zot
          (compile/syntax-match zot
            [(zot (REPEAT xs (VALUE e) min: 0)) (+ 1 1)]))
        """
        expr, mod = self.compile_expr(src_str)
        await expr()
        defn = {'compile/zot': vars(mod)['zot']}

        src_str = """
        (zot)
        """
        expr, mod = self.compile_expr(src_str, **defn)
        result = await expr()
        self.assertEqual(2, result)


    async def test_compile_syntax_match_repeat_literal_elem_and_min_count_kwarg(self):
        src_str = """
        (compile/define zot
          (compile/syntax-match zot
            [(zot (REPEAT ys (LITERAL hello) min: 1)) (+ 1 1)]))
        """
        expr, mod = self.compile_expr(src_str)
        await expr()
        defn = {'compile/zot': vars(mod)['zot']}

        src_str = """
        (zot hello)
        """
        expr, mod = self.compile_expr(src_str, **defn)
        result = await expr()
        self.assertEqual(2, result)


class TestCompileMacroMatch(DefineSyntaxCompileHelpers):

    async def test_compile_macro_match_value_pattern(self):
        src_str = """
        (compile/define ident
          (compile/macro-match ident
            [(ident (VALUE x)) x]))
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


    async def test_compile_macro_match_repeat_literal_elem_and_min_count_kwarg(self):
        src_str = """
        (compile/define zot
          (compile/macro-match zot
            [(zot (REPEAT ys (LITERAL hello) min: 1)) (+ 1 1)]))
        """
        expr, mod = self.compile_expr(src_str)
        await expr()
        defn = {'compile/zot': vars(mod)['zot']}

        src_str = """
        (zot hello)
        """
        expr, mod = self.compile_expr(src_str, **defn)
        result = await expr()
        self.assertEqual(2, result)


class TestCompileSpecialRules(DefineSyntaxCompileHelpers):

    async def test_compile_special_rules_identity(self):
        src_str = """
        (compile/define ident
          (compile/special-rules ident []
            [(c (ident x) t) x.datum]))
        """
        expr, mod = self.compile_expr(src_str)
        dis(expr.code)

        await expr()
        ident = vars(mod)['ident']
        self.assertIsInstance(ident, Syntax)
        defn = {'compile/ident': ident}

        src_str = """
        (ident 42)
        """
        expr, mod = self.compile_expr(src_str, **defn)
        dis(expr.code)

        result = await expr()
        self.assertEqual(42, result)


    async def test_compile_special_rules_bare_symbol_inner_pattern(self):
        # Bare inner x binds the whole source form (ident 42); returning x
        # would re-expand forever. Take the argument with (car (cdr x)).

        src_str = """
        (compile/define ident
          (compile/special-rules ident []
            [(c x t) (car (cdr x.datum))]))
        """
        expr, mod = self.compile_expr(src_str)
        dis(expr.code)

        await expr()
        ident = vars(mod)['ident']
        self.assertIsInstance(ident, Syntax)
        defn = {'compile/ident': ident}

        src_str = """
        (ident 42)
        """
        expr, mod = self.compile_expr(src_str, **defn)
        dis(expr.code)

        result = await expr()
        self.assertEqual(42, result)


class TestCompileSpecialMatch(DefineSyntaxCompileHelpers):

    async def test_compile_special_match_value_pattern(self):
        src_str = """
        (compile/define ident
          (compile/special-match ident
            [(compiler (ident (VALUE x)) t)
             x.datum]))
        """
        expr, mod = self.compile_expr(src_str)
        dis(expr.code)

        await expr()
        ident = vars(mod)['ident']
        self.assertIsInstance(ident, Syntax)
        defn = {'compile/ident': ident}

        src_str = """
        (ident 42)
        """
        expr, mod = self.compile_expr(src_str, **defn)
        dis(expr.code)

        result = await expr()
        self.assertEqual(42, result)


    async def test_compile_special_match_bare_symbol_inner_pattern(self):
        # Same as test_compile_special_rules_bare_symbol_inner_pattern:
        # full-form bind; expand to the operand, not the whole form.

        src_str = """
        (compile/define ident
          (compile/special-match ident
            [(c x t)
             (car (cdr x.datum))]))
        """
        expr, mod = self.compile_expr(src_str)
        dis(expr.code)

        await expr()
        ident = vars(mod)['ident']
        self.assertIsInstance(ident, Syntax)
        defn = {'compile/ident': ident}

        src_str = """
        (ident 42)
        """
        expr, mod = self.compile_expr(src_str, **defn)
        dis(expr.code)

        result = await expr()
        self.assertEqual(42, result)


# The end.
