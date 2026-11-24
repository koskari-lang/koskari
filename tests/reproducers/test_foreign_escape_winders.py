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
tests.reproducers.test_foreign_escape_winders

Full-stack reproducer: a continuation captured in one evaluation and
called from a coroutine or generator that Python steps in another. The
signal leaves the escaping context's dyn-wind and dyn-with extents,
each exit running once, and reaches the owner without any handler in
the escaping context taking it.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import asyncio
import os
import unittest

import pytest

from koskari.module import init_module, load_module_async, new_module


pytestmark = pytest.mark.phase_reproducers

_SCRIPT = os.path.join(
    os.path.dirname(__file__), 'scripts', 'foreign_escape_winders.ki')


def via_coroutine(fn, log, k):
    """
    Call a Koskari function from Python and step its coroutine to its
    return

    :return: the coroutine's value
    """

    c = fn(log, k)
    try:
        while True:
            c.send(None)
    except StopIteration as done:
        return done.value


def via_generator(fn, log, k):
    """
    Call a Koskari generator function from Python and iterate it to
    its end

    :return: the values it yielded
    """

    return list(fn(log, k))


class TestForeignEscapeWinders(unittest.TestCase):
    """
    A foreign continuation signal leaving the extents of the context
    that called it
    """

    def setUp(self):
        mod = new_module('tests.reproducers.foreign_escape_winders')
        init_module(mod, os.path.abspath(_SCRIPT))
        asyncio.run(load_module_async(mod))
        self.mod = mod


    def run_owner(self, helper, fn):
        """
        Run the owner, which captures k and hands it to helper and fn

        :return: the owner's value
        """

        with self.assertRaises(StopIteration) as done:
            self.mod.owner([], helper, fn).send(None)
        return done.exception.value


    def test_escape_from_a_dyn_wind(self):
        log = self.run_owner(via_coroutine, self.mod.wound)
        self.assertEqual(log, ["in", "out", "wound"])


    def test_escape_from_nested_dyn_winds(self):
        log = self.run_owner(via_coroutine, self.mod.nested)
        self.assertEqual(log, ["in1", "in2", "out2", "out1", "nested"])


    def test_escape_past_a_root_handler(self):
        """
        A try catching BaseException at the escaping coroutine's root
        does not take the signal
        """

        log = self.run_owner(via_coroutine, self.mod.caught)
        self.assertEqual(log, ["in", "out", "wound"])


    def test_escape_past_a_root_finally(self):
        """
        A continuation transfer does not run a finally: neither the
        bare handler nor the finally body runs
        """

        log = self.run_owner(via_coroutine, self.mod.finished)
        self.assertEqual(log, ["in", "out", "wound"])


    def test_escape_from_a_generator(self):
        """
        The generator leaves its dyn-wind at its yield and enters it
        again at the next step, then the signal leaves it once more
        """

        log = self.run_owner(via_generator, self.mod.generated)
        self.assertEqual(log, ["in", "out", "in", "out", "generated"])


    def test_escape_from_a_dyn_with_whose_exit_is_koskari(self):
        """
        The manager's __exit__ is a Koskari frame, whose return delivers
        the signal
        """

        log = self.run_owner(via_coroutine, self.mod.managed)
        self.assertEqual(log, ["enter", "exit", "managed"])


# The end.
