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
koskari.compile.specials.begin

Special form handler for compile/begin.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from ...lib import pair
from ..compiler import ByteCodeFragment, Compiler
from ...matcher import CALLEDBY, MATCH, REST
from ...bootstrap import builtin
from ...syntax import special
from ...types import get_position, is_nil, is_pair
from ..helpers import helper_begin


_DEFAULT_PATTERNS = (MATCH(CALLEDBY(), REST("rest")),)


@builtin("compile/begin")
@special("compile/begin", patterns=_DEFAULT_PATTERNS)
def special_begin(
        compiler: Compiler,
        source: pair,
        tc: bool = False) -> ByteCodeFragment:

    called_by, body = source

    if not is_nil(body) and is_pair(body) and is_nil(body.cdr):
        # begin of a single form is that form: return it for
        # recompilation, so wrappers (layout rule bodies especially)
        # never obscure the form they hold -- a lowered head stays
        # recognizable to call's patterns, and expansion output stays
        # honest
        return body.car

    fragment = compiler.fragment(called_by)
    # thread the form's own position to diagnostics about positionless
    # atoms in the body
    return helper_begin(fragment, body, tc, get_position(source))


# The end.
