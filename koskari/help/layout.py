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
koskari.help.layout

Registry help renderer for koskari.reader.layouts.LayoutHandler

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Composer via Cursor
"""


from ..reader.layouts.helpers import LayoutHandler
from .tools import help_for


def _peer_label(peer):
    """
    Render one peer head symbol for help text
    """

    return str(peer)


@help_for(LayoutHandler)
def help_for_layout_handler(obj, formatter):
    """
    Fill formatter with LayoutHandler documentation
    """

    doc = getattr(obj, '__doc__', None)
    if doc:
        formatter.paragraph(doc)

    if obj.peers:
        sec = formatter.subsection('Peers')
        for peer in obj.peers:
            sec.item(_peer_label(peer))


# The end.
