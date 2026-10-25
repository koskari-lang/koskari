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
tests.lib.test_coroutine_await_iter_release

A Koskari coroutine waiting on a Python awaitable releases the
awaitable's iterator once the forward into it is over. That release
can run the iterator's __del__, and a send from there into the waiting
coroutine is refused with RuntimeError, as a send from inside the
forward is.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from tests import make_callable
from tests.lib.opcode import build_expr


pytestmark = pytest.mark.phase_lib


class ReenteringIter:
    """
    Await iterator which yields once, then completes. Its __del__ sends
    into the coroutine held in the box, once, and records the outcome.
    """

    def __init__(self, box, record, fail):
        self.box = box
        self.record = record
        self.fail = fail
        self.count = 0


    def __iter__(self):
        return self


    def send(self, value=None):
        if self.count == 0:
            self.count = 1
            return "yielded"

        if self.fail:
            # the traceback keeps this frame; drop self from it so the
            # coroutine's reference stays the last one
            del self
            raise ValueError("boom")

        raise StopIteration("sent-result")


    # one frame for either entry, so the del above leaves no frame in
    # the traceback holding self
    __next__ = send


    def throw(self, *args):
        raise StopIteration("thrown-result")


    def __del__(self):
        coro = self.box.pop("coro", None)
        if coro is None:
            return

        try:
            coro.send(None)
        except StopIteration as stop:
            self.record.append(stop.value)
        except BaseException as exc:
            self.record.append(exc)
        else:
            self.record.append("yielded")


class ReenteringAwaitable:
    """
    Awaitable whose __await__ builds a fresh ReenteringIter and keeps
    no reference to it
    """

    def __init__(self, box, record, fail=False):
        self.box = box
        self.record = record
        self.fail = fail


    def __await__(self):
        return ReenteringIter(self.box, self.record, self.fail)


def wrap(result):
    return ("done", result)


class TestCoroutineAwaitIterRelease(unittest.TestCase):
    """
    The re-entrancy guard holds through the release of the await
    iterator
    """

    def make_coro(self, fail=False):
        """
        A coroutine which awaits a ReenteringAwaitable and returns its
        result passed through wrap. Returns the coroutine, the box its
        iterator's __del__ reads, and the record it writes.
        """

        box = {}
        record = []
        code, ns = build_expr(
            [
                ["VAR", "wrap"],
                ["VAR", "aw"],
                ["AWAIT", 1],
                ["CALL", 1],
                ["RETURN", 1],
            ],
            values={
                "wrap": wrap,
                "aw": ReenteringAwaitable(box, record, fail),
            },
        )
        coro = make_callable(code, ns)()
        self.assertEqual(coro.send(None), "yielded")
        box["coro"] = coro
        return coro, box, record


    def assertRefused(self, record):
        self.assertEqual(len(record), 1)
        self.assertIsInstance(record[0], RuntimeError)
        self.assertIn("already being awaited", str(record[0]))


    def test_send_completion(self):
        """
        A send completing the await finishes the coroutine with the
        iterator's result; the send from its __del__ is refused
        """

        coro, box, record = self.make_coro()
        with self.assertRaises(StopIteration) as caught:
            coro.send(None)
        self.assertEqual(caught.exception.value, ("done", "sent-result"))
        self.assertRefused(record)


    def test_throw_completion(self):
        """
        A throw completing the await finishes the coroutine with the
        iterator's result; the send from its __del__ is refused
        """

        coro, box, record = self.make_coro()
        with self.assertRaises(StopIteration) as caught:
            coro.throw(ValueError("x"))
        self.assertEqual(caught.exception.value, ("done", "thrown-result"))
        self.assertRefused(record)


    def test_send_error(self):
        """
        An error from the iterator propagates out of the send; the send
        from its __del__ is refused
        """

        coro, box, record = self.make_coro(fail=True)
        with self.assertRaisesRegex(ValueError, "boom"):
            coro.send(None)
        self.assertRefused(record)


    def test_close(self):
        """
        close releases the iterator and returns None; the send from its
        __del__ is refused
        """

        coro, box, record = self.make_coro()
        self.assertIsNone(coro.close())
        self.assertRefused(record)


# The end.
