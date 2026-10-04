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
Tests for the export list of koskari.exceptions

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import unittest

import pytest

from koskari import exceptions


pytestmark = pytest.mark.phase_infra


class TestExceptionsAll(unittest.TestCase):

    def test_every_defined_exception_is_exported(self):
        defined = [
            name for name, value in vars(exceptions).items()
            if isinstance(value, type)
            and issubclass(value, BaseException)
            and value.__module__ == exceptions.__name__]

        missing = [name for name in defined
                   if name not in exceptions.__all__]
        self.assertEqual(missing, [])


    def test_every_exported_name_exists(self):
        for name in exceptions.__all__:
            self.assertTrue(hasattr(exceptions, name), name)


# The end.
