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
Context-manager hook shapes under the with special form

A Koskari-defined __enter__ or __exit__ runs as an installed frame
above the with node, and the node completes (binds the entered value,
merges the exit verdict) only when that frame returns. A call made
inside that method returns into the method's own frame and must not
complete the node early. A Python awaitable __aenter__ installs no
frame; the awaited value must bind on revive and the body must then
run once. An __enter__ that raises leaves the manager unentered, so
__exit__ never runs and the exception reaches the enclosing handlers.
The protocol methods resolve on the manager's type the way Python
resolves special methods: an instance attribute is not consulted, a
descriptor other than a plain function binds through its __get__, and
a type with neither protocol is a TypeError.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio

from koskari.lib import KosPartial as partial
from tests.compile.specials import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_reproducers


class Boom(Exception):
    pass


def boom():
    raise Boom("body")


def helper():
    return "helper-result"


class AsyncManager:

    def __init__(self, enter_raises=False):
        self.calls = []
        self.enter_raises = enter_raises

    async def __aenter__(self):
        self.calls.append("enter-start")
        await asyncio.sleep(0)
        self.calls.append("enter")
        if self.enter_raises:
            raise Boom("enter")
        return "aenter-result"

    async def __aexit__(self, typ, val, tb):
        self.calls.append("exit-start")
        await asyncio.sleep(0)
        self.calls.append("exit")
        return True


class PlainManager:

    def __init__(self):
        self.calls = []

    def __enter__(self):
        self.calls.append("enter")
        return self

    def __exit__(self, typ, val, tb):
        self.calls.append(("exit", typ))
        return False


class DescriptorManager:
    """
    __enter__ is a staticmethod and __exit__ a classmethod, so both go
    through a descriptor's __get__ rather than the plain-function path
    """

    calls = []

    @staticmethod
    def __enter__():
        DescriptorManager.calls.append("enter")
        return "static-enter"

    @classmethod
    def __exit__(cls, typ, val, tb):
        cls.calls.append(("exit", cls.__name__))
        return False


class NoProtocol:
    pass


PARTIAL_CALLS = []


def partial_enter():
    PARTIAL_CALLS.append("enter")
    return "partial-enter"


def partial_exit(typ, val, tb):
    PARTIAL_CALLS.append(("exit", typ))
    return False


class PartialManager:
    """
    The protocol methods are Koskari partials over Python functions: a
    Koskari callable that completes in place without installing a frame
    """

    __enter__ = partial(partial_enter)
    __exit__ = partial(partial_exit)


class TestWithHookShapes(SpecialsTestCase):

    async def koskari_manager(self, swallow):
        """
        A class whose __enter__ and __exit__ are Koskari functions
        that each call a helper before producing their result
        """

        enter_src = """
        (lambda [self]
          (define h (helper))
          "enter-result")
        """
        exit_src = f"""
        (lambda [self typ val tb]
          (define h (helper))
          {"True" if swallow else "False"})
        """

        enter_expr, _ = self.compile_expr(enter_src, helper=helper)
        exit_expr, _ = self.compile_expr(exit_src, helper=helper)
        return type("Manager", (), {
            "__enter__": await enter_expr(),
            "__exit__": await exit_expr(),
        })


    async def test_enter_calls_helper_binding(self):
        """
        A call inside __enter__ returns into __enter__, and the with
        binding receives what __enter__ itself returned
        """

        manager = await self.koskari_manager(swallow=False)
        expr, _ = self.compile_expr("(with [c ctx] c)", ctx=manager())
        self.assertEqual(await expr(), "enter-result")


    async def test_enter_calls_helper_no_binding(self):
        manager = await self.koskari_manager(swallow=False)
        expr, _ = self.compile_expr("(with [(ctx)] 5)", ctx=manager)
        self.assertEqual(await expr(), 5)


    async def test_exit_calls_helper_propagates(self):
        """
        A call inside __exit__ returns into __exit__; the verdict is
        __exit__'s own return, so the body's exception propagates
        """

        manager = await self.koskari_manager(swallow=False)
        expr, _ = self.compile_expr("(with [c ctx] (boom))",
                                    ctx=manager(), boom=boom)
        with self.assertRaises(Boom):
            await expr()


    async def test_exit_calls_helper_swallows(self):
        manager = await self.koskari_manager(swallow=True)
        expr, _ = self.compile_expr("(with [c ctx] (boom))",
                                    ctx=manager(), boom=boom)
        self.assertIsNone(await expr())


    async def test_exit_calls_helper_body_value(self):
        manager = await self.koskari_manager(swallow=False)
        expr, _ = self.compile_expr("(with [c ctx] 7)", ctx=manager())
        self.assertEqual(await expr(), 7)


    async def raising_enter_manager(self, calls):
        """
        A class whose Koskari __enter__ raises and whose Koskari
        __exit__ records that it ran and would swallow anything
        """

        enter_src = """
        (lambda [self]
          (calls.append "enter")
          (boom))
        """
        exit_src = """
        (lambda [self typ val tb]
          (calls.append "exit")
          True)
        """

        enter_expr, _ = self.compile_expr(enter_src, calls=calls, boom=boom)
        exit_expr, _ = self.compile_expr(exit_src, calls=calls)
        return type("Manager", (), {
            "__enter__": await enter_expr(),
            "__exit__": await exit_expr(),
        })


    async def test_enter_raises_skips_exit(self):
        """
        An __enter__ that raises leaves the manager unentered: the
        exception propagates and __exit__ never runs
        """

        calls = []
        manager = await self.raising_enter_manager(calls)
        expr, _ = self.compile_expr("(with [c ctx] (calls.append \"body\"))",
                                    ctx=manager(), calls=calls)
        with self.assertRaises(Boom):
            await expr()
        self.assertEqual(calls, ["enter"])


    async def test_enter_raises_skips_exit_dyn_with(self):
        calls = []
        manager = await self.raising_enter_manager(calls)
        expr, _ = self.compile_expr(
            "(dyn-with [c ctx] (calls.append \"body\"))",
            ctx=manager(), calls=calls)
        with self.assertRaises(Boom):
            await expr()
        self.assertEqual(calls, ["enter"])


    async def test_python_async_enter_raises_skips_exit(self):
        mgr = AsyncManager(enter_raises=True)
        expr, _ = self.compile_expr("(with [c ctx] c)", ctx=mgr)
        with self.assertRaises(Boom):
            await expr()
        self.assertEqual(mgr.calls, ["enter-start", "enter"])


    async def test_python_plain_manager(self):
        mgr = PlainManager()
        expr, _ = self.compile_expr("(with [c ctx] c)", ctx=mgr)
        self.assertIs(await expr(), mgr)
        self.assertEqual(mgr.calls, ["enter", ("exit", None)])


    async def test_python_plain_manager_exception(self):
        mgr = PlainManager()
        expr, _ = self.compile_expr("(with [c ctx] (boom))",
                                    ctx=mgr, boom=boom)
        with self.assertRaises(Boom):
            await expr()
        self.assertEqual(mgr.calls, ["enter", ("exit", Boom)])


    async def test_descriptor_manager(self):
        DescriptorManager.calls.clear()
        expr, _ = self.compile_expr("(with [c ctx] c)",
                                    ctx=DescriptorManager())
        self.assertEqual(await expr(), "static-enter")
        self.assertEqual(DescriptorManager.calls,
                         ["enter", ("exit", "DescriptorManager")])


    async def test_instance_attribute_not_a_protocol(self):
        """
        Special methods resolve on the type: an __enter__ set on the
        instance does not make it a context manager
        """

        mgr = NoProtocol()
        mgr.__enter__ = lambda: "instance"
        mgr.__exit__ = lambda typ, val, tb: False
        expr, _ = self.compile_expr("(with [c ctx] c)", ctx=mgr)
        with self.assertRaises(TypeError):
            await expr()


    async def test_no_protocol_is_type_error(self):
        expr, _ = self.compile_expr("(with [c ctx] c)", ctx=NoProtocol())
        with self.assertRaises(TypeError) as caught:
            await expr()
        self.assertIn("context manager protocol", str(caught.exception))


    async def test_partial_manager_completes_in_place(self):
        """
        A Koskari callable that returns without a frame of its own has
        completed: the binding and the exit verdict are its results,
        not a pending call
        """

        PARTIAL_CALLS.clear()
        expr, _ = self.compile_expr("(with [c ctx] #(c 7))",
                                    ctx=PartialManager())
        self.assertEqual(await expr(), ("partial-enter", 7))
        self.assertEqual(PARTIAL_CALLS, ["enter", ("exit", None)])


    async def test_partial_manager_exception(self):
        PARTIAL_CALLS.clear()
        expr, _ = self.compile_expr("(with [c ctx] (boom))",
                                    ctx=PartialManager(), boom=boom)
        with self.assertRaises(Boom):
            await expr()
        self.assertEqual(PARTIAL_CALLS, ["enter", ("exit", Boom)])


    async def test_python_async_manager_binding(self):
        """
        A Python __aenter__ coroutine is awaited in place; its value
        is the binding and the body runs once
        """

        mgr = AsyncManager()
        expr, _ = self.compile_expr("(with [c ctx] c)", ctx=mgr)
        self.assertEqual(await expr(), "aenter-result")
        self.assertEqual(mgr.calls,
                         ["enter-start", "enter", "exit-start", "exit"])


    async def test_python_async_manager_dyn_with(self):
        mgr = AsyncManager()
        expr, _ = self.compile_expr("(dyn-with [c ctx] c)", ctx=mgr)
        self.assertEqual(await expr(), "aenter-result")
        self.assertEqual(mgr.calls,
                         ["enter-start", "enter", "exit-start", "exit"])


    async def test_single_shot_exit_once_across_capture(self):
        """
        A continuation captured inside a with body and invoked after the
        block completed re-enters the body through a clone of the node;
        the clone shares the exit-done fact, so __exit__ runs once
        """

        mgr = PlainManager()
        saved = []

        def stash(k):
            saved.append(k)

        def invoke_saved():
            k = saved.pop()
            return k(99)

        src = """
        (begin
          (define result
            (with [c ctx] (capture/cc k (begin (stash k) 1))))
          (cond [saved (invoke_saved)] [else: result]))
        """
        expr, _ = self.compile_expr(src, ctx=mgr, stash=stash,
                                    invoke_saved=invoke_saved, saved=saved)
        self.assertEqual(await expr(), 99)
        self.assertEqual(mgr.calls, ["enter", ("exit", None)])


    def test_throw_into_awaiting_aenter(self):
        """
        A throw at the revive of a suspended __aenter__ reaches the
        pending node: the manager was never entered, so __aexit__ does
        not run and the exception propagates
        """

        mgr = AsyncManager()
        expr, _ = self.compile_expr("(with [c ctx] c)", ctx=mgr)
        coro = expr()
        coro.send(None)
        with self.assertRaises(Boom):
            coro.throw(Boom("thrown"))
        self.assertEqual(mgr.calls, ["enter-start"])


    def test_throw_into_awaiting_aexit(self):
        """
        A throw at the revive of a suspended __aexit__ discharges the
        manager: __aexit__ does not run again and the thrown exception
        propagates
        """

        mgr = AsyncManager()
        expr, _ = self.compile_expr("(with [c ctx] c)", ctx=mgr)
        coro = expr()
        coro.send(None)
        coro.send(None)
        with self.assertRaises(Boom):
            coro.throw(Boom("thrown"))
        self.assertEqual(mgr.calls, ["enter-start", "enter", "exit-start"])


# The end.
