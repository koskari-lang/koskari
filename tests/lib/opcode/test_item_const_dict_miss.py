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
tests.lib.opcode.test_item_const_dict_miss

A constant-key read of an exact dict that misses raises KeyError with
the key as its one argument, as dict subscription does, through both
the stack and the fast-slot families, and after the site has elected
its dict body.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import pytest

from tests.lib.opcode import CevalTest
from tests.lib.opcode.test_item_electors import const_load_site


pytestmark = pytest.mark.phase_lib


class TestItemConstDictMiss(CevalTest):

    async def check_miss(self, opname, key):
        fn, _ = const_load_site(key, opname)
        present = {key: "hit"}
        # the first call elects the dict body; the rest run it
        for _ in range(3):
            self.assertEqual(await fn(present), "hit")
        for _ in range(3):
            with self.assertRaises(KeyError) as caught:
                await fn({})
            self.assertEqual(caught.exception.args, (key,))
        self.assertEqual(await fn(present), "hit")

    async def test_str_key_miss(self):
        for opname in ('ITEM_CONST', 'FAST_ITEM_CONST'):
            await self.check_miss(opname, "k")

    async def test_tuple_key_miss_reports_one_key(self):
        for opname in ('ITEM_CONST', 'FAST_ITEM_CONST'):
            await self.check_miss(opname, (1, 2))


# The end.
