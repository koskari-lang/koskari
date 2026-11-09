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
tests.reproducers.test_traceback_frame_injection

An uncaught Koskari exception reaches Python with the raising Koskari
frame in its traceback: a frame whose code names the Koskari file and
function and whose globals carry that frame's trace data, so a stock
traceback shows the Koskari location and the koskari formatter renders
each frame where it sits, Koskari frames on both sides of a Python
frame included. The entries are added as the raise crosses frames, so
a Koskari handler already sees them.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import sys
import traceback
import unittest

import pytest

from koskari.exceptions import format_exception_with_koskari_trace
from koskari.lib import demand, get_frame_depth_limit
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
(def boom [] (raise (ValueError "boom")))
(def outer [] (boom))
"""


class TestTracebackFrameInjection(unittest.IsolatedAsyncioTestCase):

    async def raise_it(self):
        mod = new_module("tb_inject")
        vars(mod)["__file__"] = "<tb_inject>"
        await load_module_source_async(mod, SRC, layout=False)
        try:
            await vars(mod)["outer"]()
        except ValueError as exc:
            return exc
        self.fail("no exception")

    async def test_raising_koskari_frame_is_in_the_traceback(self):
        exc = await self.raise_it()
        frames = [f for f, _ in traceback.walk_tb(exc.__traceback__)]
        koskari = [f for f in frames if "__koskari_trace__" in f.f_globals]
        self.assertEqual(len(koskari), 1)
        code = koskari[0].f_code
        self.assertEqual(code.co_filename, "<tb_inject>")
        self.assertEqual(code.co_name, "boom")

    async def test_formatter_renders_the_injected_frame(self):
        exc = await self.raise_it()
        text = "".join(format_exception_with_koskari_trace(
            type(exc), exc, exc.__traceback__))
        self.assertIn("<tb_inject>", text)
        self.assertIn("boom", text)

    async def test_formatter_renders_each_frame_once(self):
        exc = await self.raise_it()
        text = "".join(format_exception_with_koskari_trace(
            type(exc), exc, exc.__traceback__))
        self.assertNotIn("Koskari dynchain", text)
        self.assertEqual(1, text.count("in boom"), text)

    async def test_frame_carries_only_its_own_entry(self):
        exc = await self.raise_it()
        frames = [f for f, _ in traceback.walk_tb(exc.__traceback__)]
        entries = [f.f_globals["__koskari_trace__"] for f in frames
                   if "__koskari_trace__" in f.f_globals]
        self.assertEqual(len(entries), 1)
        self.assertIsInstance(entries[0], dict)
        self.assertEqual(entries[0]["name"], "boom")


INTERLEAVED_SRC = """
(def inner [] (raise (ValueError "boom")))
(def outer [relay] (relay inner))
"""


def relay(fn):
    """
    A Python frame between two Koskari frames
    """

    return demand(fn)()


class TestInterleavedFrames(unittest.IsolatedAsyncioTestCase):
    """
    A raise that crosses Koskari, Python and Koskari again lists each
    Koskari frame in its true place against the Python frame between
    """

    async def raise_it(self):
        mod = new_module("tb_interleaved")
        vars(mod)["__file__"] = "<tb_interleaved>"
        await load_module_source_async(mod, INTERLEAVED_SRC, layout=False)
        try:
            await vars(mod)["outer"](relay)
        except ValueError as exc:
            return exc
        self.fail("no exception")

    async def test_traceback_holds_both_segments(self):
        exc = await self.raise_it()
        names = []
        for frame, _lineno in traceback.walk_tb(exc.__traceback__):
            entry = frame.f_globals.get("__koskari_trace__")
            if entry is not None:
                names.append(entry["name"])
            elif frame.f_code.co_name == "relay":
                names.append("relay")
        self.assertEqual(names, ["outer", "relay", "inner"])

    async def test_formatter_renders_the_segments_in_order(self):
        exc = await self.raise_it()
        text = "".join(format_exception_with_koskari_trace(
            type(exc), exc, exc.__traceback__))
        self.assertEqual(1, text.count("in outer"), text)
        self.assertEqual(1, text.count("in relay"), text)
        self.assertEqual(1, text.count("in inner"), text)
        self.assertLess(text.index("in outer"), text.index("in relay"), text)
        self.assertLess(text.index("in relay"), text.index("in inner"), text)


RECROSS_SRC = """
(def leaf [] (raise (ValueError "x")))
(def mid [] (+ 1 (leaf)))
(def h [e] (raise e))
(def g [e] (+ 1 (h e)))
(def reraise-via []
  (+ 1 (try (mid)
            ([ValueError as: e]
             (g e)))))
(def outer-via [] (+ 1 (reraise-via)))
"""


def py_leaf():
    raise ValueError("x")


def py_mid():
    return 1 + py_leaf()


def py_h(e):
    raise e


def py_g(e):
    return 1 + py_h(e)


def py_reraise_via():
    try:
        return 1 + py_mid()
    except ValueError as e:
        return 1 + py_g(e)


def py_outer_via():
    return 1 + py_reraise_via()


class TestRecrossedHandlerFrame(unittest.IsolatedAsyncioTestCase):
    """
    A handler that calls a function which re-raises the bound exception
    sends the exception back through the handler's frame. The traceback
    lists that frame twice, at the handler's call and at the protected
    call, as CPython does for the same shape
    """

    async def raise_it(self):
        mod = new_module("tb_recross")
        vars(mod)["__file__"] = "<tb_recross>"
        await load_module_source_async(mod, RECROSS_SRC, layout=False)
        try:
            await vars(mod)["outer-via"]()
        except ValueError as exc:
            return exc
        self.fail("no exception")

    def python_names(self):
        try:
            py_outer_via()
        except ValueError as exc:
            return [f.f_code.co_name[3:].replace("_", "-")
                    for f, _ in traceback.walk_tb(exc.__traceback__)
                    if f.f_code.co_name.startswith("py_")]
        self.fail("no exception")

    async def test_handler_frame_is_listed_twice_in_python_order(self):
        exc = await self.raise_it()
        names = []
        lines = []
        for frame, _lineno in traceback.walk_tb(exc.__traceback__):
            entry = frame.f_globals.get("__koskari_trace__")
            if entry is not None:
                names.append(entry["name"])
                lines.append(entry["line"])
        expected = ["outer-via", "reraise-via", "g", "h",
                    "reraise-via", "mid", "leaf"]
        self.assertEqual(self.python_names(), expected)
        self.assertEqual(names, expected)
        self.assertEqual(lines[1], 9)
        self.assertEqual(lines[4], 7)


DEEP_SRC = """
(def boom [n] (if (<= n 0) (raise (ValueError "x")) (+ 1 (boom (- n 1)))))
(def caught [n] (try (boom n) ([ValueError as: e] e)))
"""


class TestDeepRaiseRecord(unittest.IsolatedAsyncioTestCase):
    """
    A raise through thousands of frames caught at the top leaves one
    traceback entry per frame, each frame once
    """

    async def test_one_entry_per_frame(self):
        mod = new_module("tb_deep")
        vars(mod)["__file__"] = "<tb_deep>"
        await load_module_source_async(mod, DEEP_SRC, layout=False)
        exc = await vars(mod)["caught"](3000)
        self.assertIsInstance(exc, ValueError)

        traced = [frame.f_globals["__koskari_trace__"]
                  for frame, _ in traceback.walk_tb(exc.__traceback__)
                  if "__koskari_trace__" in frame.f_globals]
        names = [entry["name"] for entry in traced]
        self.assertEqual(names, ["caught"] + ["boom"] * 3001)
        ids = [entry["frame"] for entry in traced]
        self.assertEqual(len(set(ids)), len(ids))
        self.assertTrue(all(entry["crossed"] for entry in traced))


HANDLER_SRC = """
(def leaf [] (raise (ValueError "x")))
(def mid [] (+ 1 (leaf)))
(def catcher [snap] (+ 1 (try (mid) ([ValueError as: e] (snap e)))))
"""


def py_catcher(snap):
    try:
        return 1 + py_mid()
    except ValueError as e:
        return 1 + snap(e)


class TestTracebackAtTheCatch(unittest.IsolatedAsyncioTestCase):
    """
    The traceback grows as the raise crosses frames, so inside a Koskari
    handler the handled exception's traceback already holds every frame
    the raise crossed and the handler's own frame, as a Python handler
    sees for the same shape
    """

    def snapshot(self, seen):
        def snap(e):
            exc_tb = sys.exc_info()[2]
            seen.append(exc_tb is e.__traceback__)
            seen.append([
                frame.f_globals["__koskari_trace__"]["name"]
                if "__koskari_trace__" in frame.f_globals
                else frame.f_code.co_name[3:]
                for frame, _ in traceback.walk_tb(exc_tb)])
            return 0
        return snap

    async def test_handler_sees_the_crossed_frames(self):
        mod = new_module("tb_catch")
        vars(mod)["__file__"] = "<tb_catch>"
        await load_module_source_async(mod, HANDLER_SRC, layout=False)

        seen = []
        self.assertEqual(await vars(mod)["catcher"](self.snapshot(seen)), 1)
        py_seen = []
        self.assertEqual(py_catcher(self.snapshot(py_seen)), 1)

        self.assertEqual(py_seen, [True, ["catcher", "mid", "leaf"]])
        self.assertEqual(seen, py_seen)


DEPTH_SRC = """
(def endless [n] (+ 1 (endless (+ n 1))))
"""


class TestRecursionErrorTraceback(unittest.IsolatedAsyncioTestCase):
    """
    A RecursionError refused at the frame depth limit carries one
    traceback entry for each frame on the chain it unwound
    """

    async def test_one_entry_per_frame_at_the_limit(self):
        mod = new_module("tb_depth")
        vars(mod)["__file__"] = "<tb_depth>"
        await load_module_source_async(mod, DEPTH_SRC, layout=False)

        try:
            await vars(mod)["endless"](0)
        except RecursionError as caught:
            exc_tb = caught.__traceback__
        else:
            self.fail("RecursionError not raised")

        traced = [frame.f_globals["__koskari_trace__"]
                  for frame, _ in traceback.walk_tb(exc_tb)
                  if "__koskari_trace__" in frame.f_globals]
        self.assertEqual(len(traced), get_frame_depth_limit())
        self.assertTrue(all(entry["name"] == "endless" for entry in traced))
        ids = [entry["frame"] for entry in traced]
        self.assertEqual(len(set(ids)), len(ids))


# The end.
