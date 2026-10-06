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
tests.reproducers.test_python_traceback_frames

The Python traceback of an exception that passed through Koskari
frames holds one entry per Koskari frame, at the line that was
executing, outermost first: when the exception leaves the evaluator
to Python through a demand entry, an import or a coroutine, and when
a Koskari try catches it, so handler code reading e.__traceback__ or
calling traceback.format_exc sees the Koskari frames too.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import os
import sys
import traceback
import unittest

import pytest

from koskari.cli.host import run_file_async
from koskari.module import new_module


pytestmark = pytest.mark.phase_reproducers


_SCRIPTS = os.path.join(os.path.dirname(__file__), 'scripts')
_FRAMES = os.path.join(_SCRIPTS, 'traceback_frames.ki')


def koskari_frames(tb):
    return [(os.path.basename(f.f_code.co_filename), line, f.f_code.co_name)
            for f, line in traceback.walk_tb(tb)
            if f.f_code.co_filename.endswith('.ki')]


class TestPythonTracebackFrames(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        mod = new_module('traceback_frames')
        await run_file_async(mod, _FRAMES)
        return vars(mod)


    def frames_of(self, thunk):
        # assertRaises detaches the traceback from the exception it
        # keeps, so the frames are read inside the except arm
        try:
            thunk()
        except ZeroDivisionError as exc:
            return koskari_frames(exc.__traceback__)
        self.fail("no ZeroDivisionError")


    async def test_demand_call(self):
        mod = await self.load()
        self.assertEqual(
            [('traceback_frames.ki', 22, 'douter'),
             ('traceback_frames.ki', 11, 'inner')],
            self.frames_of(lambda: mod['douter'](3)))


    async def test_coroutine_call(self):
        mod = await self.load()
        try:
            await mod['outer'](3)
        except ZeroDivisionError as exc:
            frames = koskari_frames(exc.__traceback__)
        else:
            self.fail("no ZeroDivisionError")
        self.assertEqual(
            [('traceback_frames.ki', 17, 'outer'),
             ('traceback_frames.ki', 11, 'inner')],
            frames)


    async def test_import(self):
        sys.path.insert(0, _SCRIPTS)
        try:
            sys.modules.pop('traceback_topfail', None)
            frames = self.frames_of(lambda: __import__('traceback_topfail'))
        finally:
            sys.path.remove(_SCRIPTS)
            sys.modules.pop('traceback_topfail', None)
        self.assertEqual(
            [('traceback_topfail.ki', 2, 'traceback_topfail')], frames)


    async def test_handler_sees_koskari_frames(self):
        mod = await self.load()
        for name, raiser in (
                ('python_raise', 'JSONDecodeError'),
                ('c_raise', 'ValueError'),
                ('koskari_raise', 'ValueError')):
            with self.subTest(name):
                tb, text = await mod['handler_frames'](mod[name])
                frames = koskari_frames(tb)
                self.assertIn(('traceback_frames.ki', 27, 'handler_frames'),
                              frames, text)
                self.assertTrue(
                    any(f[2] == name for f in frames), (frames, text))
                self.assertIn('traceback_frames.ki', text)
                self.assertIn(raiser, text)


# The end.
