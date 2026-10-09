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
tests.reproducers.test_stepper_reentry

A body that reaches the asend awaitable driving it and calls send or
throw on it is refused, and the refused value or exception is gone:
the next resume of the awaitable delivers nothing but the resume.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
define g-throws-at-self [box susp]:
    define st box.step
    try:
        (st.throw ValueError)
    except [ValueError]:
        None
    (await susp)
    (yield "ok")

define g-sends-to-self [box susp]:
    define st box.step
    try:
        (st.send "hijack")
    except [ValueError]:
        None
    (await susp)
    (yield "ok")
"""


class Holder:
    """
    Carries the stepper into the body as an attribute: a call result
    is settled by the ambient await, an attribute read is not
    """

    step = None


class Suspend:
    """
    An awaitable that suspends once and records what the resume sent
    """

    def __init__(self):
        self.got = "unresumed"

    def __await__(self):
        self.got = yield "suspended"


class TestStepperReentry(unittest.IsolatedAsyncioTestCase):

    async def asyncSetUp(self):
        module = new_module("stepper_reentry")
        vars(module)["__file__"] = "<stepper_reentry>"
        await load_module_source_async(module, SRC)
        self.ns = vars(module)

    def drive(self, name):
        box = Holder()
        susp = Suspend()
        agen = self.ns[name](box, susp)
        step = agen.asend(None)
        box.step = step

        self.assertEqual("suspended", next(step))
        with self.assertRaises(StopIteration) as caught:
            next(step)
        return caught.exception.value, susp.got

    async def test_throw_from_the_body_does_not_reach_the_resume(self):
        self.assertEqual(("ok", None), self.drive("g-throws-at-self"))

    async def test_send_from_the_body_does_not_reach_the_resume(self):
        self.assertEqual(("ok", None), self.drive("g-sends-to-self"))


# The end.
