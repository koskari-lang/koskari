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
Integration tests for define with syntax and macro transformers.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import pytest

from koskari.compile import Compiler
from koskari.exceptions import CompilerError
from koskari.lib import OpCode, build_list, build_tuple, cons, nil, symbol
from koskari.syntax import (
    Syntax, build_macro, build_special, build_syntax, parse_match_pattern,
    parse_rule_pattern)
from tests.dis.dis import dis

from . import SpecialsTestCase


pytestmark = pytest.mark.phase_specials


class DefineSyntaxCompileHelpers(SpecialsTestCase):

    def compile_expr(self, src_str, **defns):
        defns.setdefault('build-syntax', build_syntax)
        defns.setdefault('build-macro', build_macro)
        defns.setdefault('build-special', build_special)
        defns.setdefault('parse-match-pattern', parse_match_pattern)
        defns.setdefault('parse-rule-pattern', parse_rule_pattern)
        return super().compile_expr(src_str, **defns)


    def compile_to_code(
            self, src_str, lineno=1,
            resolution_mode=None, resolution_instance=None,
            **defns):

        defns.setdefault('build-syntax', build_syntax)
        defns.setdefault('build-macro', build_macro)
        defns.setdefault('build-special', build_special)
        defns.setdefault('parse-match-pattern', parse_match_pattern)
        defns.setdefault('parse-rule-pattern', parse_rule_pattern)
        return super().compile_to_code(
            src_str, lineno=lineno,
            resolution_mode=resolution_mode,
            resolution_instance=resolution_instance,
            **defns)


class TestCompileSyntaxRules(DefineSyntaxCompileHelpers):

    async def test_compile_syntax_rules_unless_macro(self):
        src_str = """
        (define unless
          (syntax-rules unless [else:]
            [(unless test body ... else: alt)
             (cond [test alt] [else: (begin body ...)])]
            [(unless test body ...)
             (cond [test None] [else: (begin body ...)])]))
        """
        expr, mod = self.compile_expr(src_str)
        dis(expr.code)

        await expr()
        defn = {'compile/unless': vars(mod)['unless']}

        src_str = """
        (begin
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
        (define example
          (syntax-rules example [#list]
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


    async def test_compile_syntax_rules_nested_ellipsis_to_list(self):
        """
        ``(grouped (a b ...) ...)`` / ``(#list (#list a b ...) ...)`` carries a
        rank-2 variable ``b`` under two ellipses; the use-site
        ``(grouped (1 2 3) (4 5))`` reconstructs the grouping as nested
        ``#list`` values.
        """

        src_str = """
        (define grouped
          (syntax-rules grouped [#list]
            [(grouped (a b ...) ...)
             (#list (#list a b ...) ...)]))
        """
        expr, mod = self.compile_expr(src_str)
        dis(expr.code)

        await expr()
        defn = {
            'compile/grouped': vars(mod)['grouped'],
            '#list': build_list,
        }

        src_str = """
        (grouped (1 2 3) (4 5))
        """
        expr, mod = self.compile_expr(src_str, **defn)
        dis(expr.code)

        result = await expr()
        self.assertEqual(
            [[1, 2, 3], [4, 5]], [list(g) for g in result])


    async def test_compile_syntax_rules_pair_under_two_ellipses(self):
        """
        ``(k (a b) ...) ...`` captures the pair's variables under two
        ellipses; the template rebuilds every pair inside every group
        """

        src_str = """
        (define keyed
          (syntax-rules keyed [#list #tuple]
            [(keyed (k (a b) ...) ...)
             (#list (#list k (#tuple a b) ...) ...)]))
        """
        expr, mod = self.compile_expr(src_str)
        await expr()
        defn = {
            'compile/keyed': vars(mod)['keyed'],
            '#list': build_list,
            '#tuple': build_tuple,
        }

        src_str = """
        (keyed ("x" (1 2) (3 4)) ("y" (5 6)))
        """
        expr, mod = self.compile_expr(src_str, **defn)
        result = await expr()
        self.assertEqual(
            [["x", (1, 2), (3, 4)], ["y", (5, 6)]],
            [list(g) for g in result])


    async def test_compile_syntax_rules_bare_pair_under_two_ellipses(self):
        src_str = """
        (define pairs
          (syntax-rules pairs [#list #tuple]
            [(pairs ((a b) ...) ...)
             (#list (#list (#tuple a b) ...) ...)]))
        """
        expr, mod = self.compile_expr(src_str)
        await expr()
        defn = {
            'compile/pairs': vars(mod)['pairs'],
            '#list': build_list,
            '#tuple': build_tuple,
        }

        src_str = """
        (pairs ((1 2) (3 4)) ((5 6)))
        """
        expr, mod = self.compile_expr(src_str, **defn)
        result = await expr()
        self.assertEqual(
            [[(1, 2), (3, 4)], [(5, 6)]], [list(g) for g in result])


    async def test_compile_syntax_rules_no_matching_rule_raises(self):
        src_str = """
        (define only-one
          (syntax-rules only-one []
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


class TestCompileMacroRules(DefineSyntaxCompileHelpers):

    async def test_compile_macro_rules_identity(self):
        src_str = """
        (define ident
          (macro-rules ident []
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
        ``function-compose``-style macros that iterate ``reverse`` over
        the repeated forms.
        """

        src_str = """
        (define ellipsis-repro
          (macro-rules ellipsis-repro []
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


    async def test_compile_macro_rules_pair_under_ellipsis(self):
        """
        ``(a b) ...`` binds each of the pair's variables to a list, one
        entry per iteration, the way a repeated symbol does
        """

        src_str = """
        (define pairs
          (macro-rules pairs []
            [(pairs (a b) ...)
             `(#list ,@a ,@b)]))
        """
        expr, mod = self.compile_expr(src_str)
        await expr()
        defn = {
            'compile/pairs': vars(mod)['pairs'],
            '#list': build_list,
        }

        src_str = """
        (pairs (1 2) (3 4))
        """
        expr, mod = self.compile_expr(src_str, **defn)
        result = await expr()
        self.assertEqual([1, 3, 2, 4], list(result))


    async def test_compile_special_rules_pair_under_ellipsis(self):
        """
        A special arm gets the pair's variables as lists of the captured
        datums, one entry per iteration
        """

        src_str = """
        (define pairs
          (special-rules pairs []
            [(c (pairs (a b) ...))
             `(#list ,(len a) ,(len b) ,(attr (item b 1) datum))]))
        """
        expr, mod = self.compile_expr(src_str)
        await expr()
        defn = {
            'compile/pairs': vars(mod)['pairs'],
            '#list': build_list,
        }

        src_str = """
        (pairs (1 2) (3 4))
        """
        expr, mod = self.compile_expr(src_str, **defn)
        result = await expr()
        self.assertEqual([2, 2, 4], list(result))


class TestCompileSyntaxMatch(DefineSyntaxCompileHelpers):

    async def test_compile_syntax_match_value_pattern(self):
        src_str = """
        (define double
          (syntax-match double
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
        (define zot
          (syntax-match zot
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
        (define zot
          (syntax-match zot
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
        (define ident
          (macro-match ident
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
        (define zot
          (macro-match zot
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


    async def test_compile_macro_match_tail_slot_absent_is_none(self):
        """
        A slot that did not fire arrives as None: the formals are the
        pattern's slots with a None default, not required positionals
        """

        src_str = """
        (define tailed
          (macro-match tailed
            [(tailed (VALUE a) (TAIL rest))
             `(#list ,a.datum ,(is rest None))]))
        """
        expr, mod = self.compile_expr(src_str)
        await expr()
        defn = {'compile/tailed': vars(mod)['tailed'], '#list': build_list}

        expr, mod = self.compile_expr("(tailed 1)", **defn)
        self.assertEqual([1, True], list(await expr()))

        expr, mod = self.compile_expr("(tailed 1 2 3)", **defn)
        self.assertEqual([1, False], list(await expr()))


    async def test_compile_macro_match_bindings_slot_is_one_formal(self):
        """
        BINDINGS claims its slot in both of its branches; the arm gets it
        once
        """

        src_str = """
        (define bound
          (macro-match bound
            [(bound (BINDINGS bs) (VALUE a))
             `(#list ,(len bs) ,a.datum)]))
        """
        expr, mod = self.compile_expr(src_str)
        await expr()
        defn = {'compile/bound': vars(mod)['bound'], '#list': build_list}

        expr, mod = self.compile_expr("(bound ((x 1) (y 2)) 3)", **defn)
        self.assertEqual([2, 3], list(await expr()))


class TestCompileSpecialRules(DefineSyntaxCompileHelpers):

    async def test_compile_special_rules_identity(self):
        src_str = """
        (define ident
          (special-rules ident []
            [(c (ident x)) x.datum]))
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
        (define ident
          (special-rules ident []
            [(c x) (car (cdr x.datum))]))
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
        (define ident
          (special-match ident
            [(compiler (ident (VALUE x)))
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


    async def test_compile_special_match_tail_slot_absent_is_none(self):
        src_str = """
        (define tailed
          (special-match tailed
            [(compiler (tailed (VALUE a) (TAIL rest)))
             `(#list ,a.datum ,(is rest None))]))
        """
        expr, mod = self.compile_expr(src_str)
        await expr()
        defn = {'compile/tailed': vars(mod)['tailed'], '#list': build_list}

        expr, mod = self.compile_expr("(tailed 1)", **defn)
        self.assertEqual([1, True], list(await expr()))

        expr, mod = self.compile_expr("(tailed 1 2 3)", **defn)
        self.assertEqual([1, False], list(await expr()))


    async def test_compile_special_match_bare_symbol_inner_pattern(self):
        # Same as test_compile_special_rules_bare_symbol_inner_pattern:
        # full-form bind; expand to the operand, not the whole form.

        src_str = """
        (define ident
          (special-match ident
            [(c x)
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


class TestExpansionCycleGuard(DefineSyntaxCompileHelpers):
    """
    The full_compile rewrite loop detects macro-expansion cycles by
    form equality and stops divergent growth at the settling limit,
    raising CompilerError instead of spinning
    """

    async def define_macro(self, src_str, name):
        expr, mod = self.compile_expr(src_str)
        await expr()
        return vars(mod)[name]

    async def test_self_expansion_cycle(self):
        spin = await self.define_macro("""
        (define spin
          (syntax-rules spin []
            [(spin x) (spin x)]))
        """, 'spin')

        with self.assertRaises(CompilerError) as caught:
            self.compile_expr("(spin 1)", **{'compile/spin': spin})

        self.assertIn("macro expansion cycle", str(caught.exception))
        self.assertIn("(spin 1)", str(caught.exception))

    async def test_two_step_cycle(self):
        spin = await self.define_macro("""
        (define spin
          (syntax-rules spin []
            [(spin x) (spun x)]))
        """, 'spin')
        spun = await self.define_macro("""
        (define spun
          (syntax-rules spun []
            [(spun x) (spin x)]))
        """, 'spun')

        defns = {'compile/spin': spin, 'compile/spun': spun}
        with self.assertRaises(CompilerError) as caught:
            self.compile_expr("(spin 1)", **defns)

        message = str(caught.exception)
        self.assertIn("macro expansion cycle", message)
        self.assertIn("(spin 1)", message)
        self.assertIn("(spun 1)", message)

    async def test_growing_expansion_hits_limit(self):
        grow = await self.define_macro("""
        (define grow
          (syntax-rules grow []
            [(grow x) (grow (grow x))]))
        """, 'grow')

        with self.assertRaises(CompilerError) as caught:
            self.compile_expr("(grow 1)", **{'compile/grow': grow})

        self.assertIn("did not settle", str(caught.exception))

    def test_rewrite_limit_bounds_chain(self):
        compiler = Compiler("<unittest>", None)
        compiler.rewrite_limit = 3
        seen = [cons(symbol("a"), i, nil) for i in range(3)]

        with self.assertRaises(CompilerError) as caught:
            compiler.check_rewrite_cycle(seen, cons(symbol("b"), nil))

        self.assertIn("did not settle", str(caught.exception))

    def test_rewrite_limit_zero_is_unlimited(self):
        compiler = Compiler("<unittest>", None)
        compiler.rewrite_limit = 0
        seen = [cons(symbol("a"), i, nil) for i in range(100)]

        compiler.check_rewrite_cycle(seen, cons(symbol("b"), nil))

        with self.assertRaises(CompilerError):
            compiler.check_rewrite_cycle(seen, cons(symbol("a"), 42, nil))

    async def test_settling_expansion_still_compiles(self):
        twice = await self.define_macro("""
        (define twice
          (syntax-rules twice []
            [(twice x) (+ x x)]))
        """, 'twice')

        expr, mod = self.compile_expr(
            "(twice 21)", **{'compile/twice': twice})
        result = await expr()
        self.assertEqual(42, result)


# The end.
