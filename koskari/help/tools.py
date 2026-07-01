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
koskari.help.tools

HelpFormatter, registry dispatch, and kos-help builtins

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Composer via Cursor
"""


import pydoc

from ..lib import symbol
from ..bootstrap import builtin


_registry = {}


_HELP_PREFIXES = ('compile/', 'layout/', 'match/')


class HelpFormatter(object):
    """
    Mutable help document builder rendered via pydoc.TextDoc
    """


    def __init__(self, title=None):
        self.title = title
        self._textdoc = pydoc.TextDoc()
        self._paragraphs = []
        self._lines = []
        self._raw = []
        self._children = []


    def is_empty(self):
        """
        True when this formatter has no text or nested sections with content
        """

        if self._paragraphs or self._lines or self._raw:
            return False
        for child in self._children:
            if not child.is_empty():
                return False
        return True


    def paragraph(self, text):
        """
        Append a prose paragraph in the current section
        """

        if text:
            self._paragraphs.append(str(text))


    def item(self, text):
        """
        Append an indented line in the current section
        """

        if text:
            self._lines.append(str(text))


    def raw(self, text):
        """
        Append pre-formatted pydoc text without re-indenting
        """

        if text is None:
            return
        text = str(text).rstrip()
        if text:
            self._raw.append(text)


    def section(self, title):
        """
        Open a section and return a child formatter scoped to it
        """

        child = HelpFormatter(title=str(title))
        self._children.append(child)
        return child


    def _render_body(self):
        """
        Assemble section body before TextDoc.section wrapping
        """

        parts = []
        for chunk in self._raw:
            parts.append(chunk)
        for para in self._paragraphs:
            parts.append(para)
        if self._lines:
            parts.append('\n'.join(self._lines))
        for child in self._children:
            rendered = child.render()
            if rendered:
                parts.append(rendered.rstrip('\n'))
        return '\n\n'.join(parts)


    def render(self):
        """
        Render this formatter and nested sections to a single string
        """

        if self.title:
            body = self._render_body()
            if not body:
                return ''
            return self._textdoc.section(self.title, body)

        body = self._render_body()
        if not body:
            return ''
        return body + '\n'


def help_for(typeobj):
    """
    Decorator to register a help handler for a type
    """

    def decorator(handler):
        help_register(typeobj, handler)
        return handler
    return decorator


def help_register(typeobj, handler):
    """
    Register a type-specific help renderer

    :param typeobj: type object to match via MRO
    :param handler: callable ``(obj, formatter) -> None``
    """

    _registry[typeobj] = handler


def help_unregister(typeobj):
    """
    Remove a type from the help registry if present
    """

    _registry.pop(typeobj, None)


def help_registry_clear():
    """
    Clear all help registry entries
    """

    _registry.clear()


def help_registry_snapshot():
    """
    Return a shallow copy of the help registry dict
    """

    return dict(_registry)


def _default_help(obj, formatter):
    """
    Fallback help via pydoc.TextDoc.document
    """

    formatter.raw(pydoc.TextDoc().document(obj))


def _help_protocol(obj, formatter):
    """
    Invoke __kos_help__ or legacy __help__ on obj
    """

    for attr in ('__kos_help__', '__help__'):
        meth = getattr(obj, attr, None)
        if not callable(meth):
            continue
        try:
            meth(formatter)
        except TypeError:
            text = meth()
            if isinstance(text, str) and text.strip():
                formatter.paragraph(text.strip())
        return True
    return False


def help_into(formatter, obj):
    """
    Render help for obj into formatter using registry, protocol, or default

    :param formatter: HelpFormatter scoped to the current binding section
    :param obj: value to describe
    """

    for ty in type(obj).__mro__:
        handler = _registry.get(ty)
        if handler is not None:
            handler(obj, formatter)
            return
    if _help_protocol(obj, formatter):
        return
    _default_help(obj, formatter)


def _help_lookup_keys(name):
    """
    Namespace keys to try for a help subject name
    """

    name = str(name)
    if name.startswith(_HELP_PREFIXES):
        return (name,)
    return (name,
            'compile/' + name,
            'layout/' + name,
            'match/' + name)


def _namespace_get(namespace, key):
    """
    Fetch one binding from a namespace mapping
    """

    if hasattr(namespace, 'get'):
        return namespace.get(key)
    try:
        return namespace[key]
    except KeyError:
        return None


def kos_help_text_object(obj, heading=None):
    """
    Collect help text for an object
    """

    doc = HelpFormatter()
    sec = doc.section(heading or repr(obj))
    help_into(sec, obj)
    return doc.render()


def kos_help_text(name, namespace=None, heading=None):
    """
    Collect help text for name and related namespace keys

    :param name: subject name (symbol or string)
    :param namespace: mapping of names to bindings (required)
    :return: combined help sections, or empty string when nothing found
    :raises TypeError: when namespace is None
    """

    doc = HelpFormatter()

    if namespace is None:
        raise TypeError('namespace is required')

    for key in _help_lookup_keys(name):
        val = _namespace_get(namespace, key)
        if val is None:
            continue

        sec = doc.section(heading or key)
        help_into(sec, val)

    if doc.is_empty():
        return ''
    return doc.render()


@builtin("help")
def kos_help(element, namespace=None):
    """
    Display help for name in namespace via the system pager
    """

    if isinstance(element, (list, tuple)):
        units = element
    else:
        units = [element]

    col = []
    for unit in units:
        if isinstance(unit, (str, symbol)):
            col.append(kos_help_text(unit, namespace))
        else:
            col.append(kos_help_text_object(unit))

    pydoc.pager("\n".join(col))


# The end.
