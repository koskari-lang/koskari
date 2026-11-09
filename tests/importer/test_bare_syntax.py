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
Tests for bare syntax forms: define-syntax and extend-syntax with a
bare-syntax kind, lone-atom expansion, bare-only alias behavior in form
position, and the bootstrap bare control words.

Requires import koskari.builtins so bootstrap definitions are loaded.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import unittest

import pytest

from koskari.compile import Compiler, CompilerMode
from koskari.exceptions import CompilerError, SyntaxMatchError
from koskari.lib import Module, ModuleNamespace, OpCode
from koskari.reader import default_reader, source_str


pytestmark = pytest.mark.phase_basics


class BareSyntaxHelpers(unittest.IsolatedAsyncioTestCase):
    """
    Compile and evaluate forms sequentially in one shared module, so a
    syntax defined by one form is bound before a later form expands it
    """

    def make_module(self):
        import koskari.builtins as builtins
        mod_ns = ModuleNamespace("test", vars(builtins))
        module = Module(mod_ns)
        compiler = Compiler("<unittest>", module, filename=__file__)
        return module, mod_ns, compiler


    async def run_forms(self, *srcs):
        """
        Read, compile, and evaluate each source string in order in a
        shared module namespace; return the last form's value
        """

        module, mod_ns, compiler = self.make_module()
        result = None
        for lineno, src in enumerate(srcs, 1):
            source = default_reader.read(
                source_str(src, __file__, line_no=lineno))
            fragment = compiler.full_compile(source)
            expr = fragment.as_expression(mod_ns)
            result = await expr()
        return result


BARE_ALIAS = """
(syntax adding (bare-syntax me +))
"""

BARE_QUOTED = """
(syntax greeting (bare-syntax me (quote hello)))
"""


class TestBareAtom(BareSyntaxHelpers):


    async def test_bare_atom_expands(self):
        result = await self.run_forms(
            BARE_QUOTED,
            "greeting")
        self.assertEqual(str(result), "hello")


    async def test_extend_syntax_attaches_bare(self):
        result = await self.run_forms(
            "(syntax pick (syntax-rules [] [(pick x) x]))",
            "(extend-syntax pick (bare-syntax me (quote alone)))",
            "pick")
        self.assertEqual(str(result), "alone")


class TestBareOnlyAlias(BareSyntaxHelpers):
    """
    A bare-only syntax used in form position is an alias: the head
    expands and the tail rides along
    """


    async def test_alias_in_form_position(self):
        result = await self.run_forms(
            BARE_ALIAS,
            "(adding 1 2 3)")
        self.assertEqual(result, 6)


    async def test_alias_in_nested_head_position(self):
        result = await self.run_forms(
            "(def mk [a] (lambda inner [b c] (+ a b c)))",
            "(syntax mk! (bare-syntax me mk))",
            "((mk! 1) 2 3)")
        self.assertEqual(result, 6)


    async def test_alias_expanding_to_attr_form(self):
        result = await self.run_forms(
            '(syntax dash (bare-syntax me (attr "-" join)))',
            '(dash #["a" "b"])')
        self.assertEqual(result, "a-b")


class TestMixedBareAndCall(BareSyntaxHelpers):
    """
    A syntax with call arms never falls back to its bare arm: a form no
    call arm matches is a malformed use and raises
    """


    async def test_call_arm_and_bare_arm_both_work(self):
        result = await self.run_forms(
            "(syntax pick (syntax-rules [] [(pick x) x]))",
            "(extend-syntax pick (bare-syntax me (quote alone)))",
            "(pick 42)")
        self.assertEqual(result, 42)


    async def test_no_matching_call_arm_raises(self):
        with self.assertRaises(SyntaxMatchError):
            await self.run_forms(
                "(syntax pick (syntax-rules [] [(pick x) x]))",
                "(extend-syntax pick (bare-syntax me (quote alone)))",
                "(pick 1 2)")


    async def test_ellipsis_over_slot_is_refused(self):
        # the slot is one symbol, never a sequence, so an ellipsis
        # after it is refused when the bare arm is defined
        with self.assertRaises(CompilerError) as caught:
            await self.run_forms(
                "(syntax bs (bare-syntax me (quote (got me ...))))")
        self.assertIn("ellipsis", str(caught.exception))


class TestHeadPositionLowering(BareSyntaxHelpers):
    """
    Expansion is legal in operator position: a pair head whose own head
    is a syntax lowers, and a symbol outcome re-dispatches the whole
    form as a fresh syntax use
    """


    async def test_head_expands_to_syntax_symbol(self):
        # ((sel 1) 2 3): the head expands to the symbol +, and the
        # rebuilt (+ 2 3) dispatches as the arithmetic special
        result = await self.run_forms(
            "(syntax sel (syntax-rules [] [(sel x) +]))",
            "((sel 1) 2 3)")
        self.assertEqual(result, 5)


    async def test_nested_heads_restart_per_level(self):
        # (((foo2 1) 2) 3) lowers to ((bar2 2) 3), restarts, lowers to
        # (+ 3), restarts, and compiles as the special
        result = await self.run_forms(
            "(syntax foo2 (syntax-rules [] [(foo2 x) bar2]))",
            "(syntax bar2 (syntax-rules [] [(bar2 x) +]))",
            "(((foo2 1) 2) 3)")
        self.assertEqual(result, 3)


    async def test_head_expanding_to_call_form(self):
        # the head expands to a form rather than a symbol; the rebuilt
        # outer form calls its value
        result = await self.run_forms(
            "(def mk [a] (lambda inner [b] (+ a b)))",
            "(define-syntax curried (macro-rules []"
            "  [(curried x) `(call mk ,x)]))",
            "((curried 1) 2)")
        self.assertEqual(result, 3)


    async def test_plain_function_head_is_not_attempted(self):
        # a pair head whose own head is no syntax lowers straight to the
        # call path: ((mk 1) 2) calls the returned closure
        result = await self.run_forms(
            "(def mk [a] (lambda inner [b] (+ a b)))",
            "((mk 1) 2)")
        self.assertEqual(result, 3)


    async def test_special_head_compiles_once_per_level(self):
        # a head whose own head is a special (lambda) is not a lowering
        # candidate: it reaches the call path raw and compiles once, so
        # ((lambda [] ...)) nested n deep completes n inner codes plus
        # the expression's own, never 2^(n+1) - 1
        module, mod_ns, compiler = self.make_module()
        src = "1"
        depth = 8
        for _ in range(depth):
            src = f"((lambda [] {src}))"
        source = default_reader.read(source_str(src, __file__))

        completions = []
        orig = Compiler.complete

        def counting(self_, *args, **kwargs):
            completions.append(1)
            return orig(self_, *args, **kwargs)

        Compiler.complete = counting
        try:
            fragment = compiler.full_compile(source)
            expr = fragment.as_expression(mod_ns)
        finally:
            Compiler.complete = orig
        self.assertEqual(1, await expr())
        self.assertEqual(depth + 1, len(completions))


    async def test_method_call_heads_unaffected(self):
        # the natural method-call spelling: the dot suffix reads as an
        # attr head, which reaches the call path raw and routes to
        # call-attr
        result = await self.run_forms(
            '("-".join #["a" "b"])')
        self.assertEqual(result, "a-b")


class TestBareControlWords(BareSyntaxHelpers):
    """
    Bootstrap bare forms for the statement words: the lone symbol
    expands to the call form
    """


    async def test_bare_return(self):
        result = await self.run_forms(
            "(def f [x] (begin (when x return) 99))",
            "#[(f True) (f False)]")
        self.assertEqual(result, [None, 99])


    async def test_bare_continue(self):
        result = await self.run_forms(
            "(var out #[])",
            "(for-each [x #[1 2 3]]"
            "  (when (== x 2) continue)"
            "  (.append out x))",
            "out")
        self.assertEqual(result, [1, 3])


    async def test_bare_break(self):
        result = await self.run_forms(
            "(var out #[])",
            "(for-each [x #[1 2 3]]"
            "  (when (== x 2) break)"
            "  (.append out x))",
            "out")
        self.assertEqual(result, [1])


    async def test_bare_yield(self):
        result = await self.run_forms(
            "(def gen [] (begin yield (yield 2)))",
            "(list (gen))")
        self.assertEqual(result, [None, 2])


    async def test_bare_raise_reraises(self):
        result = await self.run_forms(
            '(try'
            '  (try (raise (ValueError "boom"))'
            '    ([e ValueError] raise))'
            '  ([e2 ValueError] "caught"))')
        self.assertEqual(result, "caught")


    async def test_bare_pass(self):
        result = await self.run_forms(
            "(def f [] pass)",
            "(f)")
        self.assertIsNone(result)


CALL_ATTR_FAMILY = (
    OpCode.CALL_ATTR,
    OpCode.CALL_ATTR_NAMES,
    OpCode.CALL_ATTR_STAR,
    OpCode.CALL_VAR_ATTR,
    OpCode.CALL_VAR_ATTR_NAMES,
    OpCode.CALL_VAR_ATTR_STAR,
    OpCode.CALL_FAST_ATTR,
    OpCode.CALL_FAST_ATTR_NAMES,
    OpCode.CALL_FAST_ATTR_STAR,
    OpCode.TAIL_CALL_ATTR,
    OpCode.TAIL_CALL_ATTR_NAMES,
    OpCode.TAIL_CALL_ATTR_STAR,
    OpCode.TAIL_CALL_VAR_ATTR,
    OpCode.TAIL_CALL_VAR_ATTR_NAMES,
    OpCode.TAIL_CALL_VAR_ATTR_STAR,
    OpCode.TAIL_CALL_FAST_ATTR,
    OpCode.TAIL_CALL_FAST_ATTR_NAMES,
    OpCode.TAIL_CALL_FAST_ATTR_STAR,
)


class TestHeadLoweringFusion(BareSyntaxHelpers):
    """
    Fusion tripwires: an attr head must reach the call path raw, so
    calling a method fuses to a CALL_ATTR-family opcode whether the
    attr form is written directly or produced by head lowering
    """


    async def compile_last(self, *srcs):
        """
        Evaluate all but the last form in a shared module, then compile
        the last form and return its Code
        """

        module, mod_ns, compiler = self.make_module()
        fragment = None
        for lineno, src in enumerate(srcs, 1):
            source = default_reader.read(
                source_str(src, __file__, line_no=lineno))
            fragment = compiler.full_compile(source)
            if lineno < len(srcs):
                await fragment.as_expression(mod_ns)()
        return compiler.complete(fragment, mode=CompilerMode.EXPRESSION)


    def assert_fused(self, code):
        ops = [code.oparg(i)[0] for i in range(code.length())]
        fused = [op for op in ops if op in CALL_ATTR_FAMILY]
        self.assertTrue(fused, f"no CALL_ATTR-family opcode in {ops}")


    async def test_direct_method_call_fuses(self):
        code = await self.compile_last(
            '("-".join #["a" "b"])')
        self.assert_fused(code)


    async def test_explicit_attr_head_fuses(self):
        code = await self.compile_last(
            '((attr "-" join) #["a" "b"])')
        self.assert_fused(code)


    async def test_macro_lowered_attr_head_fuses(self):
        # the head lowers through a macro to an attr form; the rebuilt
        # form must still fuse
        code = await self.compile_last(
            "(define-syntax joinery (macro-rules []"
            "  [(joinery s) `(attr ,s join)]))",
            '((joinery "-") #["a" "b"])')
        self.assert_fused(code)


    async def test_begin_wrapped_template_fuses(self):
        # layout-defined templates wrap their body in a begin; the
        # begin-of-one dissolves so the attr head still reaches the
        # call path raw
        code = await self.compile_last(
            "(define-syntax joinery (macro-rules []"
            "  [(joinery s) `(begin (attr ,s join))]))",
            '((joinery "-") #["a" "b"])')
        self.assert_fused(code)


    async def test_bare_alias_attr_head_fuses(self):
        # a bare-only alias in form position rewrites the head; an attr
        # expansion must fuse like the hand-written spelling
        code = await self.compile_last(
            '(syntax dash (bare-syntax me (attr "-" join)))',
            '(dash #["a" "b"])')
        self.assert_fused(code)


    async def test_lowered_fusion_evaluates(self):
        # the fused path computes the same value as the direct spelling
        result = await self.run_forms(
            "(define-syntax joinery (macro-rules []"
            "  [(joinery s) `(begin (attr ,s join))]))",
            '((joinery "-") #["a" "b"])')
        self.assertEqual(result, "a-b")


BARE_WORD = """
(syntax word (bare-syntax me "hello"))
"""

BARE_BOX = """
(syntax box (bare-syntax me holder))
"""


class Holder:

    def __init__(self):
        self.n = 1


class TestBareAtomInObjectPosition(BareSyntaxHelpers):
    """
    A lone symbol with a bare arm expands wherever a special would
    otherwise read it as a variable: as the object of an attribute, of
    a method call, of a call, and of an in-place attribute op
    """

    async def test_attr_of_a_bare_atom(self):
        result = await self.run_forms(BARE_WORD, "word.upper")
        self.assertEqual(result, "hello".upper)

    async def test_method_call_on_a_bare_atom(self):
        result = await self.run_forms(BARE_WORD, "(.upper word)")
        self.assertEqual(result, "HELLO")

    async def test_call_of_a_bare_atom(self):
        result = await self.run_forms(
            "(syntax shout (bare-syntax me str.upper))",
            '(call shout "hello")')
        self.assertEqual(result, "HELLO")

    async def test_inplace_attr_on_a_bare_atom(self):
        module, mod_ns, compiler = self.make_module()
        holder = Holder()
        mod_ns.store("holder", holder)
        for src in (BARE_BOX, "(inplace-attr + box n 41)"):
            source = default_reader.read(source_str(src, __file__))
            await compiler.full_compile(source).as_expression(mod_ns)()
        self.assertEqual(holder.n, 42)

    async def test_literal_in_object_position_is_a_constant(self):
        result = await self.run_forms("None.__class__")
        self.assertIs(result, type(None))


# The end.
