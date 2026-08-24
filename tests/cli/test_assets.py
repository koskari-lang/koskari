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
Tests for koskari.assets: the packaged apple-mark icon.
"""


import unittest

import pytest

from koskari.assets import (ICON_PNG_SIZES, icon_path, icon_png_bytes,
                            icon_svg_bytes, icon_svg_text)


pytestmark = pytest.mark.phase_cli


_PNG_MAGIC = b'\x89PNG\r\n\x1a\n'


class TestIconSvg(unittest.TestCase):

    def test_svg_text(self):
        text = icon_svg_text()
        self.assertTrue(text.startswith('<svg'))
        self.assertIn('</svg>', text)


    def test_svg_bytes(self):
        data = icon_svg_bytes()
        self.assertTrue(data.startswith(b'<svg'))


class TestIconPng(unittest.TestCase):

    def test_packaged_sizes(self):
        self.assertEqual((32, 256), ICON_PNG_SIZES)


    def test_png_bytes_at_each_size(self):
        for size in ICON_PNG_SIZES:
            data = icon_png_bytes(size)
            self.assertTrue(data.startswith(_PNG_MAGIC))


    def test_unknown_size_raises(self):
        with self.assertRaises(ValueError):
            icon_png_bytes(999)


class TestIconPath(unittest.TestCase):

    def test_svg_path(self):
        with icon_path() as path:
            self.assertTrue(path.is_file())
            self.assertTrue(path.read_bytes().startswith(b'<svg'))


    def test_png_path(self):
        with icon_path(size=32) as path:
            self.assertTrue(path.is_file())
            self.assertTrue(path.read_bytes().startswith(_PNG_MAGIC))


    def test_unknown_size_raises(self):
        with self.assertRaises(ValueError):
            icon_path(size=999)


# The end.
