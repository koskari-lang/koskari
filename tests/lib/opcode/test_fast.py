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
Tests for FAST, FAST_BIND, FAST_CLEAR opcodes.

Invalid FAST slot indices are rejected at Code construction (see
validate_fast_slots in the native Code object). Tests that build Code must
use indices within the NamespaceSpec fast-slot range.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

from tests import make_callable
from tests.lib.opcode import CevalTest, build_code, build_expr

import pytest

pytestmark = pytest.mark.phase_lib


@pytest.mark.parametrize(
    "code_list",
    [
        pytest.param([["FAST", 0], ["RETURN", 1]], id="FAST"),
        pytest.param([["FAST_BIND", 0], ["RETURN", 1]], id="FAST_BIND"),
        pytest.param([["FAST_CLEAR", 0], ["RETURN", 1]], id="FAST_CLEAR"),
        pytest.param([["FAST_REF", 0], ["RETURN", 1]], id="FAST_REF"),
        pytest.param(
            [["CONST", 42], ["FAST_STORE", 0], ["RETURN", 1]],
            id="FAST_STORE"),
    ],
)
def test_fast_out_of_bounds_rejected_at_construction(code_list):
    """
    With no fast slots in the NamespaceSpec, any FAST* opcode is invalid
    at Code(...) time (ValueError), not at eval time
    """

    with pytest.raises(ValueError) as ctx:
        build_code(code_list)
    assert "FAST" in str(ctx.value)
    assert "limit" in str(ctx.value)


def test_rejected_code_releases_its_constants():
    """
    A Code rejected after its constant pool is built releases every
    pool entry; the constants' refcounts return to where they started
    """

    import sys
    marker = object()
    before = sys.getrefcount(marker)
    with pytest.raises(ValueError):
        build_code([["CONST", marker], ["FAST", 0], ["RETURN", 1]])
    assert sys.getrefcount(marker) == before

    # a rejection in a later validation pass takes the same exit
    with pytest.raises(RuntimeError):
        build_code([["CONST", marker], ["POP_LOOP", 0], ["RETURN", 1]])
    assert sys.getrefcount(marker) == before


def test_fast_uses_fast_flag():
    """
    Code with a root-scope FAST opcode sets uses_fast; code without does not
    """
    code_with, _ = build_expr(
        [["FAST", 0], ["RETURN", 1]],
        local_syms=("x",))
    assert code_with.uses_fast

    code_without = build_code([["CONST", 1], ["RETURN", 1]])
    assert not code_without.uses_fast


class TestFastOpcodes(CevalTest):
    """
    Tests for FAST, FAST_BIND, FAST_CLEAR, FAST_REF, FAST_STORE
    """

    async def test_fast_on_dict_namespace_raises_runtime_error(self):
        """
        FAST-slot code entered with a non-fast namespace raises RuntimeError
        at eval entry, not a crash in the opcode handler
        """
        code_obj, _ = build_expr(
            [["FAST", 0], ["RETURN", 1]],
            local_syms=("x",))
        ns = {}
        with self.assertRaises(RuntimeError) as ctx:
            await make_callable(code_obj, ns)()
        self.assertIn("fast slots", str(ctx.exception))

    async def test_fast_bind_binds_slot(self):
        """
        FAST_BIND binds value to fast slot; subsequent FAST yields value
        """
        code, ns = build_expr([
            ["CONST", 100],
            ["FAST_STORE", 0],
            ["FAST_REF", 0],
            ["FAST_BIND", 1],
            ["FAST", 1],
            ["RETURN", 1],
        ], local_syms=("x", "y"))

        result = await make_callable(code, ns)()
        self.assertEqual(result, 100)


# The end.
