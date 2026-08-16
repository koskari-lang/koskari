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
Tests for compile/decorate-subject and the compile/decorate fold from
_functions.ki

compile/decorate peels one decorator per expansion into
compile/decorate-subject, whose default arm invokes the decorator
expression on the subject and whose shape arms handle body-less extent
forms: let, with, and parameterize graft the subject in as the form's
body, while call-with and call-parameterize wrap the subject in a
forwarding function that enters the extent per invocation.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import unittest

import pytest

from koskari.compile import Compiler
from koskari.lib import Module, ModuleNamespace
from koskari.reader import default_reader, source_str


pytestmark = pytest.mark.phase_basics


class ContextRecorder:
    """
    Context manager class recording instantiations and enter/exit
    events into shared lists
    """

    def __init__(self, log, made):
        self.log = log
        self.made = made


    def __call__(self):
        inst = _RecorderInstance(self.log)
        self.made.append(inst)
        return inst


class _RecorderInstance:

    def __init__(self, log):
        self.log = log


    def __enter__(self):
        self.log.append("enter")
        return self


    def __exit__(self, exc_type, exc_val, exc_tb):
        self.log.append("exit")
        return False


class TestDecorateSubject(unittest.IsolatedAsyncioTestCase):
    """
    Behavioral tests for the decorate fold and each decorate-subject
    arm. Requires import koskari.builtins so the macros are present for
    expansion.
    """

    def compile_expr(self, src_str, lineno=1, **defns):
        import koskari.builtins as builtins
        bltns = vars(builtins)
        if defns:
            bltns = dict(bltns)
            bltns.update(defns)

        mod_ns = ModuleNamespace("test", bltns)
        module = Module(mod_ns)
        source = default_reader.read(
            source_str(src_str, __file__, line_no=lineno))
        compiler = Compiler("<unittest>", module, filename=__file__)
        fragment = compiler.compile_expression(source)
        return fragment.as_expression(mod_ns), module


    async def test_empty_decorate_is_subject(self):
        """
        (decorate [] subject) evaluates to the subject unchanged
        """

        expr, _mod = self.compile_expr("(compile/decorate [] 42)")
        self.assertEqual(42, await expr())


    async def test_default_arm_invokes(self):
        """
        decorate-subject with a non-shape decorator invokes it on the
        subject
        """

        src = """
        (begin
          (define inc (lambda [v] (+ v 1)))
          (compile/decorate-subject inc 41))
        """
        expr, _mod = self.compile_expr(src)
        self.assertEqual(42, await expr())


    async def test_decorators_apply_outermost_first(self):
        """
        Stacked runtime decorators nest with the first listed outermost
        """

        src = """
        (begin
          (define d1 (lambda [v] f"1<{v}>"))
          (define d2 (lambda [v] f"2<{v}>"))
          (compile/decorate [d1 d2] "x"))
        """
        expr, _mod = self.compile_expr(src)
        self.assertEqual("1<2<x>>", await expr())


    async def test_hoisting_binds_decorated_define(self):
        """
        A decorated define binds the wrapped function under its name
        """

        src = """
        (begin
          (define d1 (lambda [fn] (lambda [x] (fn (fn x)))))
          (compile/decorate [d1] (define add3 [x] (+ x 3)))
          (add3 1))
        """
        expr, _mod = self.compile_expr(src)
        self.assertEqual(7, await expr())


    async def test_let_arm_closes_over_definition(self):
        """
        A body-less let grafts around the function, giving a private
        mutable binding that persists across calls
        """

        src = """
        (begin
          (compile/decorate [(let [[c 0]])]
            (define counter [] (incr! c) c))
          #[(counter) (counter) (counter)])
        """
        expr, _mod = self.compile_expr(src)
        self.assertEqual([1, 2, 3], await expr())


    async def test_let_arm_binding_stays_private(self):
        """
        The grafted let binding shadows and never leaks to module scope
        """

        src = """
        (begin
          (define c "module-c")
          (compile/decorate [(let [[c 0]])]
            (define counter [] (incr! c) c))
          (counter)
          (counter)
          #[(counter) c])
        """
        expr, _mod = self.compile_expr(src)
        self.assertEqual([3, "module-c"], await expr())


    async def test_with_arm_grafts_immediate_extent(self):
        """
        A body-less with over a plain form runs enter, body, exit now
        and delivers the body value
        """

        log = []
        made = []
        src = """
        (compile/decorate [(with [(cm)])]
          (begin
            (log.append "body")
            "val"))
        """
        expr, _mod = self.compile_expr(
            src, cm=ContextRecorder(log, made), log=log)
        self.assertEqual("val", await expr())
        self.assertEqual(["enter", "body", "exit"], log)


    async def test_parameterize_arm_grafts_immediate_extent(self):
        """
        A body-less parameterize over a plain form scopes the value to
        the form and restores the ambient value after
        """

        src = """
        (begin
          (define-parameter deco-vol-a "quiet")
          (define inside
            (compile/decorate [(parameterize [[deco-vol-a "loud"]])]
              (begin deco-vol-a)))
          #[inside deco-vol-a])
        """
        expr, _mod = self.compile_expr(src)
        self.assertEqual(["loud", "quiet"], await expr())


    async def test_call_with_arm_wraps_per_invocation(self):
        """
        call-with enters a fresh extent per call, forwards arguments,
        and delivers the wrapped function's value
        """

        log = []
        made = []
        src = """
        (begin
          (compile/decorate [(call-with [(cm)])]
            (define shout [w]
              (log.append w)
              w))
          (define r1 (shout "a"))
          (define r2 (shout "b"))
          #[r1 r2])
        """
        expr, _mod = self.compile_expr(
            src, cm=ContextRecorder(log, made), log=log)
        self.assertEqual(["a", "b"], await expr())
        self.assertEqual(
            ["enter", "a", "exit", "enter", "b", "exit"], log)
        self.assertEqual(2, len(made))
        self.assertIsNot(made[0], made[1])


    async def test_call_parameterize_arm_wraps_per_invocation(self):
        """
        call-parameterize makes the extent visible during the call and
        gone after it
        """

        src = """
        (begin
          (define-parameter deco-vol-b "quiet")
          (compile/decorate [(call-parameterize [[deco-vol-b "loud"]])]
            (define speak [] deco-vol-b))
          #[(speak) deco-vol-b])
        """
        expr, _mod = self.compile_expr(src)
        self.assertEqual(["loud", "quiet"], await expr())


    async def test_call_with_wrapper_answers_for_original(self):
        """
        The delivered wrapper carries the original's identity metadata
        and the __wrapped__ chain
        """

        log = []
        made = []
        src = """
        (begin
          (compile/decorate [(call-with [(cm)])]
            (define shout [w]
              \"\"\"
              original docstring
              \"\"\"
              w))
          shout)
        """
        expr, _mod = self.compile_expr(
            src, cm=ContextRecorder(log, made), log=log)
        fn = await expr()
        self.assertEqual("shout", fn.__name__)
        self.assertEqual("shout", fn.__qualname__)
        self.assertIn("original docstring", fn.__doc__)
        self.assertEqual("shout", fn.__wrapped__.__name__)


    async def test_decorate_wraps_merges_dict(self):
        """
        decorate-wraps copies identity metadata and __dict__ entries and
        sets __wrapped__ on the wrapper
        """

        src = """
        (begin
          (define a (function aname [x] x))
          (setattr a "mark" "m1")
          (define b (lambda [x] x))
          (decorate-wraps a b)
          #[(is b.__wrapped__ a) b.mark b.__name__])
        """
        expr, _mod = self.compile_expr(src)
        self.assertEqual([True, "m1", "aname"], await expr())


    async def test_let_stacks_with_call_parameterize(self):
        """
        A lexical graft outside a per-call extent: both apply on every
        invocation
        """

        src = """
        (begin
          (define-parameter deco-vol-c "v0")
          (compile/decorate [(let [[n 0]])
                             (call-parameterize [[deco-vol-c "v1"]])]
            (define f []
              (incr! n)
              f"{n}:{deco-vol-c}"))
          #[(f) (f)])
        """
        expr, _mod = self.compile_expr(src)
        self.assertEqual(["1:v1", "2:v1"], await expr())


# The end.
