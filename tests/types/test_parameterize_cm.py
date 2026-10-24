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
tests.types.test_parameterize_cm

Contract tests for the Python-side parameterize context manager

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import asyncio
import unittest

import pytest

from koskari.lib import Domain, ParamRef, Parameterize, parameter, symbol
from koskari.types import parameterize


pytestmark = pytest.mark.phase_lib


class TestParameterizeCM(unittest.TestCase):


    def test_binds_in_thread_default_when_none_active(self):
        default = Domain.current_domain()
        with parameterize(pcm_key="hello"):
            self.assertIs(Domain.current_domain(), default)
            self.assertEqual(parameter("pcm_key", None), "hello")
        self.assertIs(Domain.current_domain(), default)
        self.assertIsNone(parameter("pcm_key", None))


    def test_reuses_active_domain(self):
        with Domain() as dom:
            with parameterize(pcm_key=1):
                self.assertIs(Domain.current_domain(), dom)
                self.assertEqual(parameter("pcm_key", None), 1)
            self.assertIs(Domain.current_domain(), dom)


    def test_restores_prior_value(self):
        with Domain():
            ref = ParamRef("pcm_key")
            ref.store("outer")
            with parameterize(pcm_key="inner"):
                self.assertEqual(ref.deref(), "inner")
            self.assertEqual(ref.deref(), "outer")


    def test_clears_previously_unbound(self):
        with Domain():
            ref = ParamRef("pcm_key")
            self.assertTrue(ref.empty)
            with parameterize(pcm_key="transient"):
                self.assertEqual(ref.deref(), "transient")
            self.assertTrue(ref.empty)


    def test_restores_on_exception(self):
        with Domain():
            ref = ParamRef("pcm_key")
            ref.store("outer")
            with self.assertRaises(RuntimeError):
                with parameterize(pcm_key="inner"):
                    raise RuntimeError("boom")
            self.assertEqual(ref.deref(), "outer")


    def test_multiple_bindings(self):
        with parameterize(pcm_a=1, pcm_b=2):
            self.assertEqual(parameter("pcm_a", None), 1)
            self.assertEqual(parameter("pcm_b", None), 2)


    def test_mapping_form_takes_symbols_and_dashed_names(self):
        with parameterize({symbol("pcm-dashed"): 1, "pcm-other": 2}):
            self.assertEqual(parameter("pcm-dashed", None), 1)
            self.assertEqual(parameter(symbol("pcm-other"), None), 2)
        self.assertIsNone(parameter("pcm-dashed", None))


    def test_mapping_and_keywords_combine(self):
        with parameterize({"pcm-a": 1}, pcm_b=2):
            self.assertEqual(parameter("pcm-a", None), 1)
            self.assertEqual(parameter("pcm_b", None), 2)


    def test_native_type_is_the_manager(self):
        self.assertIsInstance(parameterize(pcm_key=1), Parameterize)


    def test_nested_inner_shadows_and_restores(self):
        with Domain():
            with parameterize(pcm_key=1):
                with parameterize(pcm_key=2):
                    self.assertEqual(parameter("pcm_key"), 2)
                self.assertEqual(parameter("pcm_key"), 1)
            self.assertIsNone(parameter("pcm_key", None))


    def test_store_inside_block_writes_the_block_cell(self):
        """
        A store inside the block writes the block's cell; the root scope
        is untouched once the block exits
        """

        with Domain():
            ref = ParamRef("pcm_key")
            with parameterize(pcm_key=1):
                ref.store(2)
                self.assertEqual(ref.deref(), 2)
            self.assertTrue(ref.empty)


    def test_out_of_order_exit_removes_only_its_own_binding(self):
        with Domain():
            ref = ParamRef("pcm_key")
            a = parameterize(pcm_key=1)
            b = parameterize(pcm_key=2)
            a.__enter__()
            b.__enter__()
            a.__exit__(None, None, None)
            self.assertEqual(ref.deref(), 2)
            b.__exit__(None, None, None)
            self.assertTrue(ref.empty)


    def test_out_of_order_exit_uncovers_the_next_binding_down(self):
        with Domain():
            ref = ParamRef("pcm_key")
            ref.store("outer")
            a = parameterize(pcm_key=1)
            b = parameterize(pcm_key=2, pcm_b="b")
            c = parameterize(pcm_key=3)
            a.__enter__()
            b.__enter__()
            c.__enter__()
            b.__exit__(None, None, None)
            self.assertEqual(ref.deref(), 3)
            self.assertIsNone(parameter("pcm_b", None))
            c.__exit__(None, None, None)
            self.assertEqual(ref.deref(), 1)
            a.__exit__(None, None, None)
            self.assertEqual(ref.deref(), "outer")


    def test_nested_exit_restores_in_order(self):
        with Domain():
            ref = ParamRef("pcm_key")
            a = parameterize(pcm_key=1)
            b = parameterize(pcm_key=2)
            a.__enter__()
            b.__enter__()
            b.__exit__(None, None, None)
            self.assertEqual(ref.deref(), 1)
            a.__exit__(None, None, None)
            self.assertTrue(ref.empty)


    def test_reentry_while_entered_is_refused(self):
        span = parameterize(pcm_key=1)
        with span:
            with self.assertRaises(RuntimeError):
                span.__enter__()
        self.assertIsNone(parameter("pcm_key", None))


    def test_exit_without_enter_is_refused(self):
        with self.assertRaises(RuntimeError):
            parameterize(pcm_key=1).__exit__(None, None, None)


    def test_block_can_be_entered_again_after_exit(self):
        span = parameterize(pcm_key=1)
        with span:
            ParamRef("pcm_key").store(5)
        with span:
            self.assertEqual(parameter("pcm_key"), 1)
        self.assertIsNone(parameter("pcm_key", None))


    def test_bad_key_type_is_refused_at_construction(self):
        with self.assertRaises(TypeError):
            parameterize({1: 2})


class TestParameterizeTasks(unittest.IsolatedAsyncioTestCase):
    """
    Blocks belong to the running task
    """

    async def test_interleaved_tasks_do_not_see_each_other(self):
        a_in = asyncio.Event()
        b_in = asyncio.Event()
        a_out = asyncio.Event()
        seen = {}

        async def task_a():
            with parameterize(pcm_key="a"):
                a_in.set()
                await b_in.wait()
                seen["a while b open"] = parameter("pcm_key")
            seen["a after exit"] = parameter("pcm_key", None)
            a_out.set()

        async def task_b():
            await a_in.wait()
            with parameterize(pcm_key="b"):
                b_in.set()
                await a_out.wait()
                seen["b after a exited"] = parameter("pcm_key")
            seen["b after exit"] = parameter("pcm_key", None)

        await asyncio.gather(task_a(), task_b())
        self.assertEqual(seen, {
            "a while b open": "a",
            "a after exit": None,
            "b after a exited": "b",
            "b after exit": None,
        })
        self.assertIsNone(parameter("pcm_key", None))


    async def test_task_created_inside_block_keeps_its_bindings(self):
        release = asyncio.Event()

        async def child():
            await release.wait()
            return parameter("pcm_key", None)

        with parameterize(pcm_key="inherited"):
            task = asyncio.create_task(child())
            await asyncio.sleep(0)
        self.assertIsNone(parameter("pcm_key", None))
        release.set()
        self.assertEqual(await task, "inherited")


    async def test_child_block_does_not_reach_the_parent(self):
        entered = asyncio.Event()
        release = asyncio.Event()

        async def child():
            with parameterize(pcm_key="child"):
                entered.set()
                await release.wait()

        task = asyncio.create_task(child())
        await entered.wait()
        self.assertIsNone(parameter("pcm_key", None))
        release.set()
        await task


    async def test_exit_on_another_task_is_refused(self):
        span = parameterize(pcm_key=1)
        entered = asyncio.Event()
        release = asyncio.Event()

        async def holder():
            span.__enter__()
            entered.set()
            await release.wait()
            span.__exit__(None, None, None)

        holding = asyncio.create_task(holder())
        await entered.wait()
        with self.assertRaises(RuntimeError):
            span.__exit__(None, None, None)
        self.assertIsNone(parameter("pcm_key", None))
        release.set()
        await holding


    async def test_child_task_cannot_exit_an_inherited_block(self):
        span = parameterize(pcm_key="parent")

        async def child():
            span.__exit__(None, None, None)

        with span:
            with self.assertRaises(RuntimeError):
                await asyncio.create_task(child())
            self.assertEqual(parameter("pcm_key"), "parent")
        self.assertIsNone(parameter("pcm_key", None))


    async def test_child_keeps_its_view_through_a_parent_out_of_order_exit(
            self):
        outer = parameterize(pcm_key="a", pcm_other="b")
        inner = parameterize(pcm_key="b2")
        release = asyncio.Event()

        async def child():
            await release.wait()
            return parameter("pcm_key", None), parameter("pcm_other", None)

        outer.__enter__()
        inner.__enter__()
        task = asyncio.create_task(child())
        await asyncio.sleep(0)
        outer.__exit__(None, None, None)
        self.assertEqual(parameter("pcm_key"), "b2")
        self.assertIsNone(parameter("pcm_other", None))
        inner.__exit__(None, None, None)
        self.assertIsNone(parameter("pcm_key", None))
        release.set()
        self.assertEqual(await task, ("b2", "b"))


    async def test_reentered_block_does_not_change_a_child_view(self):
        span = parameterize(pcm_key="first")
        release = asyncio.Event()

        async def child():
            await release.wait()
            return parameter("pcm_key", None)

        with span:
            task = asyncio.create_task(child())
            await asyncio.sleep(0)
        with parameterize(pcm_key="outer"):
            with span:
                self.assertEqual(parameter("pcm_key"), "first")
                release.set()
                self.assertEqual(await task, "first")


# The end.
