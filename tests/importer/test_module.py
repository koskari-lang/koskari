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
Tests for koskari.importer.module: new_module, init_module.
"""


import unittest

from koskari.importer.module import init_module, new_module
from koskari.lib import Module


class TestNewModule(unittest.IsolatedAsyncioTestCase):

    async def test_returns_module_instance(self):
        mod = await new_module('mymod')
        self.assertIsInstance(mod, Module)


    async def test_name_stored_in_namespace(self):
        mod = await new_module('mymod')
        self.assertEqual('mymod', getattr(mod, '__name__', None))


    async def test_package_stored_when_given(self):
        mod = await new_module('mypkg.mymod', package='mypkg')
        self.assertEqual('mypkg', getattr(mod, '__package__', None))


    async def test_package_absent_when_not_given(self):
        mod = await new_module('mymod')
        ns = vars(mod)
        self.assertTrue(ns.ref('__package__').empty)


    async def test_builtins_present(self):
        mod = await new_module('mymod')
        ns = vars(mod)
        for name in ('cons', 'car', 'cdr', 'nil', 'pair', 'symbol'):
            self.assertFalse(ns.ref(name).empty,
                             "builtin %r missing from new module namespace" % name)


    async def test_nil_is_nil(self):
        from koskari.lib import nil
        mod = await new_module('mymod')
        ns = vars(mod)
        self.assertIs(nil, ns.ref('nil').deref())


class TestInitModule(unittest.IsolatedAsyncioTestCase):

    async def test_file_stored(self):
        mod = await new_module('mymod')
        init_module(mod, '/some/path/mymod.ki')
        self.assertEqual('/some/path/mymod.ki', getattr(mod, '__file__', None))


    async def test_path_set_for_init_ki(self):
        mod = await new_module('mypkg')
        init_module(mod, '/some/path/mypkg/__init__.ki')
        self.assertEqual(['/some/path/mypkg'], getattr(mod, '__path__', None))


    async def test_path_not_set_for_plain_module(self):
        mod = await new_module('mymod')
        init_module(mod, '/some/path/mymod.ki')
        ns = vars(mod)
        self.assertTrue(ns.ref('__path__').empty)


    async def test_spec_and_loader_stored(self):
        mod = await new_module('mymod')

        class FakeLoader:
            pass

        class FakeSpec:
            loader = FakeLoader()
            submodule_search_locations = None

        init_module(mod, '/some/path/mymod.ki', spec=FakeSpec())
        self.assertIsInstance(getattr(mod, '__spec__', None), FakeSpec)
        self.assertIsInstance(getattr(mod, '__loader__', None), FakeLoader)


# The end.
