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
Unit tests for tools.regen_version

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import importlib.util
import os
import re
import tempfile
import unittest
from datetime import datetime, timezone
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent.parent


def load_regen():
    """
    Load tools.regen_version as a module
    """

    path = ROOT / 'tools' / 'regen_version.py'
    spec = importlib.util.spec_from_file_location('regen_version', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


regen = load_regen()


# the version pattern of PEP 440, appendix B
PEP440 = re.compile(
    r'^v?(?:(?:[0-9]+!)?[0-9]+(?:\.[0-9]+)*'
    r'(?:[-_.]?(?:a|b|c|rc|alpha|beta|pre|preview)[-_.]?[0-9]*)?'
    r'(?:-[0-9]+|[-_.]?(?:post|rev|r)[-_.]?[0-9]*)?'
    r'(?:[-_.]?dev[-_.]?[0-9]*)?)'
    r'(?:\+[a-z0-9]+(?:[-_.][a-z0-9]+)*)?$',
    re.IGNORECASE)


class TestAssembleVersion(unittest.TestCase):

    def test_release_at_exact_tag(self):
        state = {
            'commit': 'abc1234',
            'exact_tag': 'v0.1.0',
            'dirty': False,
        }
        info = regen.assemble_version('0.1.0', state)
        self.assertEqual(info['release'], 'ga')
        self.assertEqual(info['build_kind'], 'release')

    def test_dev_dirty_adds_stamp(self):
        state = {
            'commit': 'abc1234',
            'exact_tag': None,
            'dirty': True,
        }
        fixed = datetime(2026, 6, 5, 18, 55, 53, tzinfo=timezone.utc)
        info = regen.assemble_version('0.1.0', state, now=fixed)
        self.assertEqual(info['release'], 'dev.abc1234.20260605T185553')
        self.assertEqual(info['build_kind'], 'dev')
        self.assertTrue(info['dirty'])

    def test_no_commit_is_ga(self):
        state = {
            'commit': None,
            'exact_tag': None,
            'dirty': False,
        }
        info = regen.assemble_version('0.1.0', state)
        self.assertEqual(info['release'], 'ga')
        self.assertEqual(info['build_kind'], 'release')

    def test_dev_snapshot_version(self):
        state = {
            'commit': 'abc1234',
            'exact_tag': 'v0.1.0.dev20260817',
            'dirty': False,
        }
        info = regen.assemble_version('0.1.0.dev20260817', state)
        self.assertEqual(info['version'], '0.1.0.dev20260817')
        self.assertEqual(info['version_info'], (0, 1, 0, 'ga'))
        self.assertEqual(info['release'], 'ga')
        self.assertEqual(info['build_kind'], 'release')


class TestVersionState(unittest.TestCase):

    def setUp(self):
        self._saved = dict(os.environ)

    def tearDown(self):
        os.environ.clear()
        os.environ.update(self._saved)

    def test_env_overlay_when_forced(self):
        os.environ['KOSKARI_FORCE_VERSION_REGEN'] = '1'
        os.environ['KOSKARI_COMMIT'] = 'deadbeef'
        os.environ['KOSKARI_DIRTY'] = '1'
        os.environ['KOSKARI_EXACT_TAG'] = 'v9.9.9'

        with tempfile.TemporaryDirectory() as tmp:
            state = regen.version_state(tmp)

        self.assertEqual(state['commit'], 'deadbeef')
        self.assertTrue(state['dirty'])
        self.assertEqual(state['exact_tag'], 'v9.9.9')

    def test_empty_env_values_clear_fields(self):
        os.environ['KOSKARI_FORCE_VERSION_REGEN'] = '1'
        os.environ['KOSKARI_COMMIT'] = ''
        os.environ['KOSKARI_DIRTY'] = '0'
        os.environ['KOSKARI_EXACT_TAG'] = ''

        with tempfile.TemporaryDirectory() as tmp:
            state = regen.version_state(tmp)

        self.assertIsNone(state['commit'])
        self.assertFalse(state['dirty'])
        self.assertIsNone(state['exact_tag'])

    def test_env_ignored_without_force(self):
        os.environ['KOSKARI_COMMIT'] = 'deadbeef'
        os.environ['KOSKARI_DIRTY'] = '1'
        os.environ['KOSKARI_EXACT_TAG'] = 'v9.9.9'

        with tempfile.TemporaryDirectory() as tmp:
            state = regen.version_state(tmp)

        self.assertIsNone(state['commit'])
        self.assertFalse(state['dirty'])
        self.assertIsNone(state['exact_tag'])


class TestReport(unittest.TestCase):

    def test_report_env_no_git(self):
        with tempfile.TemporaryDirectory() as tmp:
            text = regen.report_env(tmp)

        self.assertIn('KOSKARI_COMMIT=', text)
        self.assertIn('KOSKARI_DIRTY=0', text)
        self.assertIn('KOSKARI_EXACT_TAG=', text)
        self.assertIn('KOSKARI_VERSION=', text)
        self.assertTrue(text.endswith('\n'))

        for line in text.strip().split('\n'):
            self.assertRegex(line, r'^KOSKARI_[A-Z_]+=.*$')

    def test_report_env_version_without_git(self):
        with tempfile.TemporaryDirectory() as tmp:
            text = regen.report_env(tmp)

        self.assertIn('KOSKARI_VERSION=0.1.0+ga', text)

    def test_full_version(self):
        info = regen.assemble_version('0.1.0', {
            'commit': 'abc1234',
            'exact_tag': None,
            'dirty': False,
        })
        self.assertEqual(regen.full_version(info), '0.1.0+dev.abc1234')

    def test_full_version_is_pep440(self):
        fixed = datetime(2026, 6, 5, 18, 55, 53, tzinfo=timezone.utc)
        cases = [
            ('0.1.0', {'commit': 'abc1234', 'exact_tag': 'v0.1.0',
                       'dirty': False}),
            ('0.1.0', {'commit': 'abc1234', 'exact_tag': None,
                       'dirty': False}),
            ('0.1.0', {'commit': 'abc1234', 'exact_tag': None,
                       'dirty': True}),
            ('0.1.0.dev20260817', {'commit': 'abc1234',
                                   'exact_tag': 'v0.1.0.dev20260817',
                                   'dirty': False}),
        ]
        for version, state in cases:
            info = regen.assemble_version(version, state, now=fixed)
            self.assertRegex(regen.full_version(info), PEP440)

    def test_report_tag_oci_safe(self):
        tag = regen.report_tag(ROOT)

        self.assertNotIn('+', tag)
        self.assertRegex(tag, r'^[a-zA-Z0-9_.-]+$')
        self.assertTrue(tag.startswith('0.1.0'))

    def test_image_tag_dev_shape(self):
        state = {
            'commit': 'abc1234',
            'exact_tag': None,
            'dirty': True,
        }
        fixed = datetime(2026, 6, 5, 18, 55, 53, tzinfo=timezone.utc)
        info = regen.assemble_version('0.1.0', state)
        tag = regen.image_tag(info, now=fixed)
        self.assertEqual(tag, '0.1.0.dev20260605')

    def test_image_tag_release_is_bare_version(self):
        state = {
            'commit': 'abc1234',
            'exact_tag': 'v0.1.0',
            'dirty': False,
        }
        info = regen.assemble_version('0.1.0', state)
        self.assertEqual(regen.image_tag(info), '0.1.0')

    def test_floating_tag_dev(self):
        state = {
            'commit': 'abc1234',
            'exact_tag': None,
            'dirty': False,
        }
        info = regen.assemble_version('0.1.0', state)
        self.assertEqual(regen.floating_tag(info), '0.1.0.dev')

    def test_floating_tag_release_is_empty(self):
        state = {
            'commit': 'abc1234',
            'exact_tag': 'v0.1.0',
            'dirty': False,
        }
        info = regen.assemble_version('0.1.0', state)
        self.assertEqual(regen.floating_tag(info), '')


class TestParseBool(unittest.TestCase):

    def test_truthy_values(self):
        for val in ('1', 'true', 'TRUE', 'yes', 'on'):
            self.assertTrue(regen.parse_bool(val))

    def test_falsy_values(self):
        for val in ('0', 'false', 'no', 'off', ''):
            self.assertFalse(regen.parse_bool(val))


# The end.
