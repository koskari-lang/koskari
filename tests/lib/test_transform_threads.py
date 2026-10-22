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
tests.lib.test_transform_threads

A transform refuses a second iteration while a step is in flight, even
when that step is running a plain Python callable synchronously: a
second thread, or the mapped function itself, is refused with
ValueError instead of taking elements from the first iteration.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import threading
import unittest

import pytest

from koskari.lib import KosMap


pytestmark = pytest.mark.phase_lib


TIMEOUT = 10


class TestTransformBusy(unittest.TestCase):


    def test_second_thread_refused(self):
        inside = threading.Event()
        release = threading.Event()

        def fn(x):
            if x == 1:
                inside.set()
                release.wait(TIMEOUT)
            return x * 10

        m = KosMap(fn, [1, 2, 3])
        out = {}

        def run_a():
            out['a'] = list(iter(m))

        def run_b():
            try:
                if not inside.wait(TIMEOUT):
                    out['b'] = ('timeout', None)
                    return
                try:
                    out['b'] = ('value', next(iter(m)))
                except BaseException as exc:
                    out['b'] = (type(exc), str(exc))
            finally:
                release.set()

        ta = threading.Thread(target=run_a)
        tb = threading.Thread(target=run_b)
        ta.start()
        tb.start()
        tb.join(TIMEOUT)
        release.set()
        ta.join(TIMEOUT)

        self.assertFalse(ta.is_alive(), "thread A hung")
        self.assertFalse(tb.is_alive(), "thread B hung")

        self.assertEqual(out.get('b'),
                         (ValueError, "map is already being iterated"))
        self.assertEqual(out.get('a'), [10, 20, 30])


    def test_same_thread_reentry_refused(self):
        holder = {}

        def fn2(x):
            if x == 1:
                try:
                    holder['r'] = ('value', next(iter(holder['m'])))
                except BaseException as exc:
                    holder['r'] = (type(exc), str(exc))
            return x * 10

        m = KosMap(fn2, [1, 2, 3])
        holder['m'] = m

        self.assertEqual(list(iter(m)), [10, 20, 30])
        self.assertEqual(holder.get('r'),
                         (ValueError, "map is already being iterated"))


# The end.
