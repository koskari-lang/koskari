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
Exception propagation across frame boundaries.

A raise with no handler in the current frame crosses into the caller
section (and onward) rather than leaving the eval loop; it only exits at
the chain root or a generator boundary.
"""

from . import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_specials


class TestRaiseAcrossFrames(SpecialsTestCase):

    async def test_raise_crosses_bare_frame(self):
        """
        A raise inside a called lambda with nothing else on its chain
        section reaches the caller's try.
        """

        def boom(v):
            raise ValueError("boom")

        src_str = """
        (try
          (call (lambda [] (boom 1)))
          ([ValueError] "caught"))
        """
        expr, mod = self.compile_expr(
            src_str, boom=boom, ValueError=ValueError)
        result = await expr()
        self.assertEqual(result, "caught")


    async def test_raise_crosses_frame_with_open_extents(self):
        """
        A raise inside a callee with extent nodes still open above its
        frame (here the let's NS) unwinds them, crosses the frame, and
        reaches the caller's try with the original exception intact.

        Guards three collapsed defects: the foreign tail-call frame pop
        clobbering the exception when extents were open, the raise walk
        stopping at a frame boundary instead of crossing, and the
        try-extent lookup re-matching from inside its own handler.
        """

        def boom(v):
            raise ValueError("boom")

        src_str = """
        (try
          (call
            (lambda []
              (let [[x 1]]
                (boom x))))
          ([ValueError] "caught"))
        """
        expr, mod = self.compile_expr(
            src_str, boom=boom, ValueError=ValueError)
        result = await expr()
        self.assertEqual(result, "caught")


    async def test_reraise_from_unmatched_handler_propagates(self):
        """
        A handler whose exception check does not match re-raises; the
        re-raise must not re-enter its own try extent (which would loop)
        and instead reaches the enclosing handler.
        """

        def boom(v):
            raise ValueError("boom")

        src_str = """
        (try
          (try
            (boom 1)
            ([KeyError] "wrong"))
          ([ValueError] "caught"))
        """
        expr, mod = self.compile_expr(
            src_str, boom=boom, KeyError=KeyError, ValueError=ValueError)
        result = await expr()
        self.assertEqual(result, "caught")


    async def test_crossed_frames_recorded_in_dynchain_meta(self):
        """
        Frames crossed during a raise walk are recorded into the
        exception's __koskari_dynchain__ metadata before being popped,
        marked "crossed".
        """

        def boom(v):
            raise ValueError("boom")

        src_str = """
        (begin
          (call
            (lambda []
              (let [[x 1]]
                (boom x))))
          5  ; preventing tail-call promotion
        )
        """
        expr, mod = self.compile_expr(src_str, boom=boom)
        try:
            await expr()
        except ValueError as exc:
            chain = getattr(exc, "__koskari_dynchain__", None)
            self.assertIsNotNone(chain)
            crossed = [m for m in chain if m.get("crossed")]
            self.assertGreaterEqual(len(crossed), 1)
            for meta in chain:
                self.assertIn("name", meta)
                self.assertIn("pc", meta)
        else:
            self.fail("ValueError did not propagate")


# The end.
