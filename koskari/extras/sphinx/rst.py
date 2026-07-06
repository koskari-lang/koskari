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
koskari.extras.sphinx.rst

reST fragments for Koskari autodoc documenters

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from koskari.help.layout import format_layout_peer
from koskari.help.syntax import format_accept_pattern


def syntax_accepts_lines(syntax_obj):
    """
    Build reST lines for a Syntax object's accepted patterns

    :param syntax_obj: koskari.syntax.Syntax instance
    :return: list of str (may be empty)
    """

    if not syntax_obj.transforms:
        return []

    head = str(syntax_obj.name)
    lines = ['', '**Accepts**', '']
    for transform in syntax_obj.transforms:
        lines.append('* %s' % format_accept_pattern(transform.pattern, head))
    return lines


def layout_peers_lines(layout_obj):
    """
    Build reST lines for a LayoutHandler's peer heads

    :param layout_obj: LayoutHandler instance
    :return: list of str (may be empty)
    """

    if not layout_obj.peers:
        return []

    lines = ['', '**Peers**', '']
    for peer in layout_obj.peers:
        lines.append('* %s' % format_layout_peer(peer))
    return lines


# The end.
