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
Tests for koskari.cli.term_graphics: terminal graphics protocol
detection and escape sequence encoding.
"""


import base64
import io
import re
import unittest
from unittest import mock

import pytest

from koskari.cli.term_graphics import (
    detect_protocol, emit_icon, render_iterm2_escape, render_kitty_escape)


pytestmark = pytest.mark.phase_cli


class _FakeStream(io.StringIO):

    def __init__(self, isatty=True):
        super().__init__()
        self._isatty = isatty


    def isatty(self):
        return self._isatty


class TestDetectProtocol(unittest.TestCase):

    def test_non_tty_never_matches(self):
        env = {'KITTY_WINDOW_ID': '1'}
        self.assertIsNone(detect_protocol(env=env, isatty=False))


    def test_no_signals(self):
        self.assertIsNone(detect_protocol(env={}, isatty=True))


    def test_kitty_window_id(self):
        env = {'KITTY_WINDOW_ID': '1'}
        self.assertEqual('kitty', detect_protocol(env=env, isatty=True))


    def test_kitty_term(self):
        env = {'TERM': 'xterm-kitty'}
        self.assertEqual('kitty', detect_protocol(env=env, isatty=True))


    def test_ghostty(self):
        env = {'GHOSTTY_RESOURCES_DIR': '/opt/ghostty'}
        self.assertEqual('kitty', detect_protocol(env=env, isatty=True))


    def test_iterm2(self):
        env = {'TERM_PROGRAM': 'iTerm.app'}
        self.assertEqual('iterm2', detect_protocol(env=env, isatty=True))


    def test_wezterm(self):
        env = {'TERM_PROGRAM': 'WezTerm'}
        self.assertEqual('iterm2', detect_protocol(env=env, isatty=True))


    def test_tmux_term_prefix_suppresses(self):
        env = {'TERM': 'screen-256color', 'TERM_PROGRAM': 'iTerm.app'}
        self.assertIsNone(detect_protocol(env=env, isatty=True))


    def test_tmux_env_var_suppresses(self):
        env = {'TMUX': '/tmp/tmux-1000/default,1,0',
               'TERM_PROGRAM': 'iTerm.app'}
        self.assertIsNone(detect_protocol(env=env, isatty=True))


    def test_override_forces_kitty(self):
        env = {'KOSKARI_TERM_GRAPHICS': 'kitty'}
        self.assertEqual('kitty', detect_protocol(env=env, isatty=True))


    def test_override_forces_iterm2(self):
        env = {'KOSKARI_TERM_GRAPHICS': 'iterm2'}
        self.assertEqual('iterm2', detect_protocol(env=env, isatty=True))


    def test_override_none_wins_over_signals(self):
        env = {'KOSKARI_TERM_GRAPHICS': 'none', 'KITTY_WINDOW_ID': '1'}
        self.assertIsNone(detect_protocol(env=env, isatty=True))


    def test_override_bypasses_multiplexer_guard(self):
        env = {'KOSKARI_TERM_GRAPHICS': 'kitty', 'TMUX': '/tmp/x'}
        self.assertEqual('kitty', detect_protocol(env=env, isatty=True))


    def test_override_unrecognized_falls_back_to_heuristics(self):
        env = {'KOSKARI_TERM_GRAPHICS': 'bogus', 'KITTY_WINDOW_ID': '1'}
        self.assertEqual('kitty', detect_protocol(env=env, isatty=True))


_KITTY_CHUNK_RE = re.compile(r'\x1b_G(.*?);(.*?)\x1b\\')


class TestRenderKittyEscape(unittest.TestCase):

    def _chunks(self, escape):
        return _KITTY_CHUNK_RE.findall(escape)


    def test_small_payload_single_chunk(self):
        data = b'small png bytes'
        escape = render_kitty_escape(data)
        chunks = self._chunks(escape)
        self.assertEqual(1, len(chunks))
        keys, payload = chunks[0]
        self.assertIn('a=T', keys)
        self.assertIn('f=100', keys)
        self.assertIn('q=2', keys)
        self.assertIn('m=0', keys)
        self.assertEqual(data, base64.b64decode(payload))


    def test_large_payload_chunks_at_4096_and_reassembles(self):
        data = bytes(range(256)) * 40  # 10240 bytes, well over one chunk
        escape = render_kitty_escape(data)
        chunks = self._chunks(escape)
        self.assertGreater(len(chunks), 1)

        first_keys, _ = chunks[0]
        self.assertIn('a=T', first_keys)
        self.assertIn('f=100', first_keys)
        self.assertIn('m=1', first_keys)

        for keys, _ in chunks[1:-1]:
            self.assertEqual('m=1', keys)
        last_keys, _ = chunks[-1]
        self.assertEqual('m=0', last_keys)

        reassembled = base64.b64decode(''.join(p for _, p in chunks))
        self.assertEqual(data, reassembled)


_ITERM2_RE = re.compile(
    r'\x1b\]1337;File=name=([^;]+);size=(\d+);inline=1:(.+)\x07\Z')


class TestRenderIterm2Escape(unittest.TestCase):

    def test_format_and_roundtrip(self):
        data = b'some png bytes'
        escape = render_iterm2_escape(data, name='icon.png')
        match = _ITERM2_RE.match(escape)
        self.assertIsNotNone(match)
        name_b64, size, payload_b64 = match.groups()
        self.assertEqual('icon.png', base64.b64decode(name_b64).decode())
        self.assertEqual(len(data), int(size))
        self.assertEqual(data, base64.b64decode(payload_b64))


class TestEmitIcon(unittest.TestCase):
    # os.environ is patched (cleared) in every case here so the outcome
    # does not depend on the terminal actually running the test suite.

    def test_non_tty_writes_nothing(self):
        stream = _FakeStream(isatty=False)
        env = {'KOSKARI_TERM_GRAPHICS': 'kitty'}
        with mock.patch.dict('os.environ', env, clear=True):
            wrote = emit_icon(stream=stream)
        self.assertFalse(wrote)
        self.assertEqual('', stream.getvalue())


    def test_tty_without_signals_writes_nothing(self):
        stream = _FakeStream(isatty=True)
        with mock.patch.dict('os.environ', {}, clear=True):
            wrote = emit_icon(stream=stream)
        self.assertFalse(wrote)
        self.assertEqual('', stream.getvalue())


    def test_tty_with_kitty_override_writes_kitty_escape(self):
        stream = _FakeStream(isatty=True)
        env = {'KOSKARI_TERM_GRAPHICS': 'kitty'}
        with mock.patch.dict('os.environ', env, clear=True):
            wrote = emit_icon(stream=stream)
        self.assertTrue(wrote)
        self.assertTrue(stream.getvalue().startswith('\x1b_G'))


    def test_tty_with_iterm2_override_writes_iterm2_escape(self):
        stream = _FakeStream(isatty=True)
        env = {'KOSKARI_TERM_GRAPHICS': 'iterm2'}
        with mock.patch.dict('os.environ', env, clear=True):
            wrote = emit_icon(stream=stream)
        self.assertTrue(wrote)
        self.assertTrue(stream.getvalue().startswith('\x1b]1337;File='))


# The end.
