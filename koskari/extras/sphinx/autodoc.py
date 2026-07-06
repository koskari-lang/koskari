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
"""


from sphinx.ext.autodoc import (
    AttributeDocumenter,
    ClassDocumenter,
    DataDocumenter,
    DecoratorDocumenter,
    ExceptionDocumenter,
    FunctionDocumenter,
    MethodDocumenter,
    PropertyDocumenter,
)

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
    titles_allowed = True

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

    def format_signature(self, **kwargs):
        return ''


    def add_directive_header(self, sig):
        """
        Use a plain reST subsection; py:kosyntax cannot represent names with /
        """

        sourcename = self.get_sourcename()
        name = self.format_name()
        self.add_line('', sourcename)
        self.add_line(name, sourcename)
        self.add_line('~' * len(name), sourcename)


    def add_content(self, more_content=None):
        super().add_content(more_content)
        for line in syntax_accepts_lines(self.object):
            self.add_line(line, self.get_sourcename())


class KosLayoutDocumenter(DataDocumenter):
    """
    Document reader LayoutHandler instances with peer heads
    """

    objtype = 'kolayout'
    priority = _KOS_PRIORITY
    titles_allowed = True

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

    def format_signature(self, **kwargs):
        return ''


    def add_directive_header(self, sig):
        """
        Use a plain reST subsection; py:kolayout cannot represent names with /
        """

        sourcename = self.get_sourcename()
        name = self.format_name()
        self.add_line('', sourcename)
        self.add_line(name, sourcename)
        self.add_line('~' * len(name), sourcename)


    def add_content(self, more_content=None):
        super().add_content(more_content)
        for line in layout_peers_lines(self.object):
            self.add_line(line, self.get_sourcename())


def member_documenter_classes(registry):
    """
    Documenter classes used when scanning koskari.builtins members

    Sphinx 9's default autodoc registry may omit legacy stock documenters;
    include them explicitly so C callables, types, and exceptions render

    :param registry: Sphinx build registry
    :return: list of Documenter subclasses
    """

    classes = [
        ExceptionDocumenter,
        ClassDocumenter,
        KosFunctionDocumenter,
        KosSyntaxDocumenter,
        KosLayoutDocumenter,
        PropertyDocumenter,
        AttributeDocumenter,
        MethodDocumenter,
        FunctionDocumenter,
        DecoratorDocumenter,
        DataDocumenter,
    ]
    for cls in registry.documenters.values():
        if cls not in classes:
            classes.append(cls)
    return classes


def bind_builtin_member(documenter, name, value, builtins_module):
    """
    Attach a koskari.builtins binding without parse_name/import_object

    :param documenter: legacy Documenter instance
    :param name: module attribute name
    :param value: bound object
    :param builtins_module: loaded koskari.builtins module
    """

    full_name = 'koskari.builtins.%s' % name
    documenter.modname = 'koskari.builtins'
    documenter.objpath = [name]
    documenter.fullname = full_name
    documenter.object = value
    documenter.object_name = name
    documenter.parent = builtins_module
    documenter.module = builtins_module

    if isinstance(documenter, ClassDocumenter):
        if isinstance(value, type) and hasattr(value, '__name__'):
            documenter.doc_as_attr = documenter.objpath[-1] != value.__name__
        else:
            documenter.doc_as_attr = True


def pick_documenter(member, membername, isattr, parent, registry):
    """
    Choose the highest-priority documenter for a builtins member

    :param member: object to document
    :param membername: attribute name on koskari.builtins
    :param isattr: True when documented as attribute not routine
    :param parent: ModuleDocumenter instance for koskari.builtins
    :param registry: Sphinx build registry
    :return: Documenter subclass, or None
    """

    classes = [
        cls
        for cls in member_documenter_classes(registry)
        if cls.can_document_member(member, membername, isattr, parent)]
    if not classes:
        return None
    classes.sort(key=lambda cls: cls.priority)
    return classes[-1]


def register_autodocumenters(app):
    """
    Register Koskari autodocumenters on a Sphinx application

    :param app: Sphinx application instance
    """

    app.add_autodocumenter(KosFunctionDocumenter, override=False)
    app.add_autodocumenter(KosSyntaxDocumenter, override=False)
    app.add_autodocumenter(KosLayoutDocumenter, override=False)


# The end.
