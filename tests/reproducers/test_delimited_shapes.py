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
test_delimited_shapes - the delimited-continuation shape matrix: where
a shift sits relative to its reset, k escaping its prompt, a shift
firing inside a resumed continuation, bindings read back inside a
resumed region, operands pending in the call around the reset, how k
is used, and the other dynamic extents a reset meets.

The shapes live in scripts/delimited_shapes.ki as one function each;
every test awaits one and checks its value against the Danvy/Filinski
reduction.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import os
import unittest

import pytest

from koskari.module import init_module, load_module_async, new_module


pytestmark = pytest.mark.phase_reproducers

_SCRIPT = os.path.join(os.path.dirname(__file__), 'scripts',
                       'delimited_shapes.ki')


class TestDelimitedShapes(unittest.IsolatedAsyncioTestCase):
    """
    Every reset/shift shape of the matrix, one function per shape
    """

    async def load_module(self):
        mod = new_module('tests.reproducers.delimited_shapes')
        init_module(mod, os.path.abspath(_SCRIPT))
        await load_module_async(mod)
        return mod


    async def check(self, name, expected):
        mod = await self.load_module()
        self.assertEqual(await getattr(mod, name)(), expected)


    # 1. where the shift sits relative to its reset

    async def test_pos_lexical(self):
        """shift lexically in reset body"""
        await self.check('shape_pos_lexical', 1)

    async def test_pos_arith(self):
        """shift lexical, one pending operand in region"""
        await self.check('shape_pos_arith', 6)

    async def test_pos_let(self):
        """shift lexical, inside a let in the region"""
        await self.check('shape_pos_let', 6)

    async def test_pos_nested_build(self):
        """shift lexical, pending list and + in region"""
        await self.check('shape_pos_nested_build', ['x', 6, 'y'])

    async def test_pos_iife(self):
        """shift one frame deep via immediately-invoked lambda"""
        await self.check('shape_pos_iife', 1)

    async def test_pos_callee(self):
        """shift one frame deep in a named function"""
        await self.check('shape_pos_callee', 1)

    async def test_pos_callee_arith(self):
        """shift one frame deep, pending operand in caller region"""
        await self.check('shape_pos_callee_arith', 6)

    async def test_pos_callee_own_arith(self):
        """shift one frame deep, pending operand in callee frame"""
        await self.check('shape_pos_callee_own_arith', 103)

    async def test_pos_callee2(self):
        """shift two frames deep"""
        await self.check('shape_pos_callee2', 1)

    async def test_xframe_abandon(self):
        """cross-frame shift that never invokes k"""
        await self.check('shape_xframe_abandon', 'abandoned')

    async def test_xframe_in_let(self):
        """cross-frame shift, reset nested in a let"""
        await self.check('shape_xframe_in_let', 6)

    async def test_xframe_in_wind(self):
        """cross-frame shift, reset nested in a dyn-wind"""
        await self.check('shape_xframe_in_wind', (6, ['in', 'out']))


    # 1b. k escaping its prompt

    async def test_remote_invoke(self):
        """k escapes as the reset's value, invoked later in the same form"""
        await self.check('shape_remote_invoke', 6)

    async def test_remote_invoke_twice(self):
        """escaped k invoked twice after its prompt unwound"""
        await self.check('shape_remote_invoke_twice', (6, 31))

    async def test_remote_invoke_wound(self):
        """escaped k replays a dyn-wind around the graft on invoke"""
        await self.check('shape_remote_invoke_wound',
                         (6, ['in', 'out', 'in', 'out']))


    # 1c. a shift firing inside a resumed continuation

    async def test_reentrant_two_shifts(self):
        """two shifts in one reset; the second fires inside the first's k"""
        await self.check('shape_reentrant_two_shifts', 11)

    async def test_reentrant_multishot(self):
        """outer k invoked twice; its continuation holds a second shift"""
        await self.check('shape_reentrant_multishot', 23)

    async def test_nested_resets(self):
        """nested resets, each with its own shift"""
        await self.check('shape_nested_resets', 6)

    async def test_reentrant_inner_abandon(self):
        """inner shift abandons inside invoked k; value delivered at the invoke"""
        await self.check('shape_reentrant_inner_abandon', 200)

    async def test_reentrant_k_of_k(self):
        """three-deep re-entrancy: k applied to its own result twice"""
        await self.check('shape_reentrant_k_of_k', 3)

    async def test_reentrant_nested_same_k(self):
        """k re-invoked from an inner handler while a graft is still live"""
        await self.check('shape_reentrant_nested_same_k', 310)

    async def test_reentrant_amb_product(self):
        """function amb x amb: multi-shot inside a multi-shot invocation"""
        await self.check('shape_reentrant_amb_product',
                         [(1, 10), (1, 20), (2, 10), (2, 20)])


    # 1d. bindings read back inside a resumed region

    async def test_bind_let_reread(self):
        """let binding read inside the region after a multi-shot shift"""
        await self.check('shape_bind_let_reread', 'b')

    async def test_bind_let_reread_fn(self):
        """the same shape inside a function frame"""
        await self.check('shape_bind_let_reread_fn', 'b')

    async def test_bind_let_accumulate(self):
        """let-bound accumulator mutated once per invocation"""
        await self.check('shape_bind_let_accumulate', ('done', 4))


    # 2. operands pending in the enclosing call

    async def test_encl_none(self):
        """reset as sole argument"""
        await self.check('shape_encl_none', [6])

    async def test_encl_clean_region(self):
        """one enclosing operand, nothing pending in region"""
        await self.check('shape_encl_clean_region', ['B', 5])

    async def test_encl_one(self):
        """one enclosing operand, one pending in region"""
        await self.check('shape_encl_one', ['B', 6])

    async def test_encl_two(self):
        """two enclosing operands, two pending in region"""
        await self.check('shape_encl_two', ['C', 'D', 8])

    async def test_encl_three(self):
        """three pending in region; a one-slot leak would read 6"""
        await self.check('shape_encl_three', ['B', 10])

    async def test_encl_nonarith(self):
        """the pending operand is a call arg, not a reducing op"""
        await self.check('shape_encl_nonarith', ['B', 5])

    async def test_encl_build_list(self):
        """reset inside a pending list build"""
        await self.check('shape_encl_build_list', ['E', 6])


    # 3. how k is used

    async def test_k_abandon(self):
        """k never invoked; shift body value wins"""
        await self.check('shape_k_abandon', 'abandoned')

    async def test_k_twice(self):
        """k invoked twice"""
        await self.check('shape_k_twice', 32)

    async def test_k_loop(self):
        """k invoked once per element of a list"""
        await self.check('shape_k_loop', [101, 102, 103])

    async def test_k_callee_loop(self):
        """k invoked per element, shift one frame deep"""
        await self.check('shape_k_callee_loop', [101, 102, 103])


    # 4. other dynamic extents

    async def test_try_inside(self):
        """try block inside the region, shift inside the try"""
        await self.check('shape_try_inside', 6)

    async def test_try_outside(self):
        """reset inside a try block"""
        await self.check('shape_try_outside', 6)

    async def test_dynwind_around(self):
        """dyn-wind around the shift, k invoked once: the shift's abort
        leaves it, the invoke enters and leaves its copy"""
        await self.check('shape_dynwind_around',
                         (6, ['in', 'out', 'in', 'out']))

    async def test_dynwind_multishot(self):
        """dyn-wind around the shift, k invoked twice: one in and out for
        the abort, one for each invoke"""
        await self.check('shape_dynwind_multishot', (32, 6))

    async def test_param_around(self):
        """parameterize around the shift"""
        await self.check('shape_param_around', 9)

    async def test_param_around_multishot(self):
        """parameterize spans the region; every invoke reads the bound value"""
        await self.check('shape_param_around_multishot', ('done', [9, 9]))

    async def test_param_inside_multishot(self):
        """parameterize inside the resumed part; entered and restored per
        invoke"""
        await self.check('shape_param_inside_multishot',
                         ('done', [9, 1, 9, 1]))

    async def test_param_ends_before_resume(self):
        """parameterize closes before the shift returns; the resume sees
        the default"""
        await self.check('shape_param_ends_before_resume', ('done', [1, 1]))


# The end.
