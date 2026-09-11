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


from ...bootstrap import builtin
from ...lib import nil
from ...matcher import BODY, CALLEDBY, MATCH, VALUE
from ...syntax import special
from ..compiler import ByteCodeFragment, Compiler
from ..helpers import helper_begin


_BEGIN_PATTERNS = [
    MATCH(CALLEDBY("called_by")),
    MATCH(CALLEDBY("called_by"), VALUE("only")),
    MATCH(CALLEDBY("called_by"), BODY("body")),
]


@builtin("compile/begin")
@special("compile/begin", patterns=_BEGIN_PATTERNS, unpack="match")
def special_begin(
        compiler: Compiler,
        caps: dict) -> ByteCodeFragment:

    if "only" in caps:
        # begin of a single form is that form: return it for
        # recompilation, so wrappers (layout rule bodies especially)
        # never obscure the form they hold -- a lowered head stays
        # recognizable to call's patterns, and expansion output stays
        # honest
        return caps["only"].datum

    called_by = caps["called_by"]
    body = caps["body"].datum if "body" in caps else nil

    fragment = compiler.fragment(called_by.datum)
    # thread the form's own position to diagnostics about positionless
    # atoms in the body
    return helper_begin(fragment, body, position=called_by.position)


# The end.
