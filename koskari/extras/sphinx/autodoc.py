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
koskari.extras.sphinx.autodoc

Custom Sphinx autodocumenters for Koskari object instances

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Composer via Cursor
"""


from sphinx.ext.autodoc import DataDocumenter, FunctionDocumenter

from .rst import layout_peers_lines, syntax_accepts_lines


_KOS_PRIORITY = 15


def _binding_name(obj):
    """
    Prefer Koskari symbol name over the Python module attribute
    """

    name = getattr(obj, 'name', None)
    if name is None:
        return None
    text = str(name)
    if text:
        return text
    return None


class KosFunctionDocumenter(FunctionDocumenter):
    """
    Document koskari.lib.Function like a Python function
    """

    priority = _KOS_PRIORITY

    @classmethod
    def can_document_member(cls, member, membername, isattr, parent):
        from koskari.lib import Function

        if isinstance(member, Function):
            return True
        return False

    def format_name(self):
        kos_name = _binding_name(self.object)
        if kos_name is not None:
            return kos_name
        return super().format_name()

    def format_signature(self, **kwargs):
        try:
            return super().format_signature(**kwargs)
        except (TypeError, ValueError):
            return ''


class KosSyntaxDocumenter(DataDocumenter):
    """
    Document koskari.syntax.Syntax with accepted pattern arms
    """

    objtype = 'kosyntax'
    priority = _KOS_PRIORITY

    @classmethod
    def can_document_member(cls, member, membername, isattr, parent):
        from koskari.syntax import Syntax

        if isinstance(member, Syntax):
            return True
        return False

    def format_name(self):
        kos_name = _binding_name(self.object)
        if kos_name is not None:
            return kos_name
        return super().format_name()

    def add_content(self, more_content, no_docstring=False):
        super().add_content(more_content, no_docstring)
        for line in syntax_accepts_lines(self.object):
            self.add_line(line, self.get_sourcename())


class KosLayoutDocumenter(DataDocumenter):
    """
    Document reader LayoutHandler instances with peer heads
    """

    objtype = 'kolayout'
    priority = _KOS_PRIORITY

    @classmethod
    def can_document_member(cls, member, membername, isattr, parent):
        from koskari.reader.layouts.helpers import LayoutHandler

        if isinstance(member, LayoutHandler):
            return True
        return False

    def format_name(self):
        head = getattr(self.object, 'head', None)
        if head is not None:
            text = str(head)
            if text:
                return text
        return super().format_name()

    def add_content(self, more_content, no_docstring=False):
        super().add_content(more_content, no_docstring)
        for line in layout_peers_lines(self.object):
            self.add_line(line, self.get_sourcename())


def register_autodocumenters(app):
    """
    Register Koskari autodocumenters on a Sphinx application

    :param app: Sphinx application instance
    """

    app.add_autodocumenter(KosFunctionDocumenter, override=True)
    app.add_autodocumenter(KosSyntaxDocumenter, override=True)
    app.add_autodocumenter(KosLayoutDocumenter, override=True)


# The end.
