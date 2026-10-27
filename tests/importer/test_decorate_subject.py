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


import inspect
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

        expr, _mod = self.compile_expr("(decorate [] 42)")
        self.assertEqual(42, await expr())


    async def test_default_arm_invokes(self):
        """
        decorate-subject with a non-shape decorator invokes it on the
        subject
        """

        src = """
        (begin
          (def inc [v] (+ v 1))
          (decorate-subject inc 41))
        """
        expr, _mod = self.compile_expr(src)
        self.assertEqual(42, await expr())


    async def test_decorators_apply_outermost_first(self):
        """
        Stacked runtime decorators nest with the first listed outermost
        """

        src = """
        (begin
          (def d1 [v] f"1<{v}>")
          (def d2 [v] f"2<{v}>")
          (decorate [d1 d2] "x"))
        """
        expr, _mod = self.compile_expr(src)
        self.assertEqual("1<2<x>>", await expr())


    async def test_hoisting_binds_decorated_define(self):
        """
        A decorated define binds the wrapped function under its name
        """

        src = """
        (begin
          (def d1 [fn] (lambda [x] (fn (fn x))))
          (decorate [d1] (def add3 [x] (+ x 3)))
          (add3 1))
        """
        expr, _mod = self.compile_expr(src)
        self.assertEqual(7, await expr())


    async def test_decorator_over_demand_binds_decorated_value(self):
        """
        A decorator stacked over demand on a def binds the decorator's
        result, applied to the demand wrapper, under its name
        """

        src = """
        (begin
          (var seen #[])
          (def tag [x] (begin (seen.append (type x).__name__) "DECORATED"))
          (decorate [tag demand] (def cached [n] n))
          (#list seen cached))
        """
        expr, _mod = self.compile_expr(src)
        seen, cached = await expr()
        self.assertEqual(["demand"], seen)
        self.assertEqual("DECORATED", cached)


    async def test_decorator_over_coroutine_fn_binds_decorated_value(self):
        src = """
        (begin
          (var seen #[])
          (def tag [x] (begin (seen.append (type x).__name__) "DECORATED"))
          (decorate [tag coroutine-fn] (def co [n] n))
          (#list seen co))
        """
        expr, _mod = self.compile_expr(src)
        seen, co = await expr()
        self.assertEqual(["function"], seen)
        self.assertEqual("DECORATED", co)


    async def test_class_binds_decorated_value(self):
        src = """
        (begin
          (var seen #[])
          (def tag [x] (begin (seen.append (type x).__name__) x))
          (decorate [tag] (class DC [] (var x :: int 0)))
          (#list seen DC))
        """
        expr, _mod = self.compile_expr(src)
        seen, dc = await expr()
        self.assertEqual(["type"], seen)
        self.assertEqual("DC", dc.__name__)
        self.assertEqual(0, dc.x)


    async def test_define_global_binds_decorated_value(self):
        src = """
        (begin
          (var seen #[])
          (def tag [x] (begin (seen.append (type x).__name__) "DECORATED"))
          (decorate [tag] (global def gl [n] n))
          (#list seen gl))
        """
        expr, _mod = self.compile_expr(src)
        seen, gl = await expr()
        self.assertEqual(["function"], seen)
        self.assertEqual("DECORATED", gl)


    async def test_let_arm_closes_over_definition(self):
        """
        A body-less let grafts around the function, giving a private
        mutable binding that persists across calls
        """

        src = """
        (begin
          (decorate [(let [[c 0]])]
            (def counter [] (incr! c) c))
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
          (var c "module-c")
          (decorate [(let [[c 0]])]
            (def counter [] (incr! c) c))
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
        (decorate [(with [(cm)])]
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
          (var inside
            (decorate [(parameterize [[deco-vol-a "loud"]])]
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
          (decorate [(call-with [(cm)])]
            (def shout [w]
              (log.append w)
              w))
          (var r1 (shout "a"))
          (var r2 (shout "b"))
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
          (decorate [(call-parameterize [[deco-vol-b "loud"]])]
            (def speak [] deco-vol-b))
          #[(speak) deco-vol-b])
        """
        expr, _mod = self.compile_expr(src)
        self.assertEqual(["loud", "quiet"], await expr())


    async def test_call_with_wrapper_keeps_coroutine_function(self):
        """
        call-with over a coroutine function delivers a coroutine function
        whose Python call returns an awaitable, while a plain subject
        keeps a plain wrapper
        """

        log = []
        made = []
        src = """
        (begin
          (decorate [(call-with [(cm)]) coroutine-fn]
            (def co [n] (* n 2)))
          (decorate [(call-with [(cm)])]
            (def pl [n] n))
          #[co pl])
        """
        expr, _mod = self.compile_expr(
            src, cm=ContextRecorder(log, made), log=log)
        co, pl = await expr()
        self.assertTrue(inspect.iscoroutinefunction(co))
        r = co(4)
        self.assertTrue(inspect.isawaitable(r))
        self.assertEqual(8, await r)
        self.assertEqual(["enter", "exit"], log)
        self.assertFalse(inspect.iscoroutinefunction(pl))


    async def test_call_parameterize_wrapper_keeps_coroutine_function(self):
        """
        call-parameterize over a coroutine function delivers a coroutine
        function whose Python call returns an awaitable, while a plain
        subject keeps a plain wrapper
        """

        src = """
        (begin
          (define-parameter deco-vol-d "quiet")
          (decorate [(call-parameterize [[deco-vol-d "loud"]]) coroutine-fn]
            (def co [n] #[n deco-vol-d]))
          (decorate [(call-parameterize [[deco-vol-d "loud"]])]
            (def pl [n] n))
          #[co pl])
        """
        expr, _mod = self.compile_expr(src)
        co, pl = await expr()
        self.assertTrue(inspect.iscoroutinefunction(co))
        r = co(4)
        self.assertTrue(inspect.isawaitable(r))
        self.assertEqual([4, "loud"], await r)
        self.assertFalse(inspect.iscoroutinefunction(pl))


    async def test_call_with_wrapper_answers_for_original(self):
        """
        The delivered wrapper carries the original's identity metadata
        and the __wrapped__ chain
        """

        log = []
        made = []
        src = """
        (begin
          (decorate [(call-with [(cm)])]
            (define [shout w]
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
          (var a (function aname [x] x))
          (setattr a "mark" "m1")
          (def b [x] x)
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
          (decorate [(let [[n 0]])
                             (call-parameterize [[deco-vol-c "v1"]])]
            (def f []
              (incr! n)
              f"{n}:{deco-vol-c}"))
          #[(f) (f)])
        """
        expr, _mod = self.compile_expr(src)
        self.assertEqual(["1:v1", "2:v1"], await expr())


    async def test_entry_point_arm_binds_slot_and_name(self):
        """
        entry-point binds the subject as the module's __entry__ and
        delivers it unchanged, so the def binds the same function
        """

        src = """
        (begin
          (decorate [entry-point] (def main [] 7))
          #[(is __entry__ main) (main)])
        """
        expr, mod = self.compile_expr(src)
        self.assertEqual([True, 7], await expr())
        self.assertIs(vars(mod)["__entry__"], vars(mod)["main"])


    async def test_entry_point_under_runtime_decorator(self):
        """
        A runtime decorator outside entry-point sees the function the
        slot holds; the name binds what the outer decorator returns
        """

        src = """
        (begin
          (def tag [f] (setattr f "tagged" True) f)
          (decorate [tag entry-point] (def main [] 1))
          #[(is __entry__ main) main.tagged])
        """
        expr, _mod = self.compile_expr(src)
        self.assertEqual([True, True], await expr())


    async def test_entry_point_over_lambda_value(self):
        """
        entry-point over a value subject stores that value
        """

        src = """
        (begin
          (var f (decorate [entry-point] (lambda [] 2)))
          #[(is __entry__ f) (__entry__)])
        """
        expr, _mod = self.compile_expr(src)
        self.assertEqual([True, 2], await expr())


# The end.
