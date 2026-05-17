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
koskari.help.syntax

Registry help renderer for koskari.syntax.Syntax

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Composer via Cursor
"""


from ..syntax import Syntax
from ..lib import kos_repr
from .tools import help_for


def format_accept_pattern(pattern, head):
    """
    Render one accepted-arm pattern for help text and SyntaxMatchError
    """

    from ..matcher import CALLEDBY, MATCH

    if isinstance(pattern, MATCH):
        children = pattern._children
    else:
        try:
            return kos_repr(pattern)
        except Exception:
            return "<unrenderable pattern>"

    if children and isinstance(children[0], CALLEDBY):
        head = head or "(CALLEDBY)"
        try:
            rest = " ".join(kos_repr(c) for c in children[1:])
        except Exception:
            rest = "<unrenderable pattern>"
        if rest:
            return f"({head} {rest})"
        return f"({head})"

    try:
        body = " ".join(kos_repr(c) for c in children)
    except Exception:
        return "<unrenderable pattern>"
    return f"({body})"


@help_for(Syntax)
def help_for_syntax(obj, formatter):
    """
    Fill formatter with Syntax dispatcher documentation
    """

    doc = getattr(obj, '__doc__', None)
    if doc:
        formatter.paragraph(doc)

    if obj.transforms:
        acc = formatter.subsection('Accepts')
        head = str(obj.name)
        for transform in obj.transforms:
            acc.item(format_accept_pattern(transform.pattern, head))


# The end.
