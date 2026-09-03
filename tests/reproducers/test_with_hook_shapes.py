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
run once.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import asyncio

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

    def __init__(self):
        self.calls = []

    async def __aenter__(self):
        await asyncio.sleep(0)
        self.calls.append("enter")
        return "aenter-result"

    async def __aexit__(self, typ, val, tb):
        await asyncio.sleep(0)
        self.calls.append("exit")
        return False


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


    async def test_python_async_manager_binding(self):
        """
        A Python __aenter__ coroutine is awaited in place; its value
        is the binding and the body runs once
        """

        mgr = AsyncManager()
        expr, _ = self.compile_expr("(with [c ctx] c)", ctx=mgr)
        self.assertEqual(await expr(), "aenter-result")
        self.assertEqual(mgr.calls, ["enter", "exit"])


    async def test_python_async_manager_dyn_with(self):
        mgr = AsyncManager()
        expr, _ = self.compile_expr("(dyn-with [c ctx] c)", ctx=mgr)
        self.assertEqual(await expr(), "aenter-result")
        self.assertEqual(mgr.calls, ["enter", "exit"])


# The end.
