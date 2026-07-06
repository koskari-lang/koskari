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
koskari.extras.sphinx.partition

Partition koskari.builtins members for structured API documentation

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import inspect
from types import BuiltinFunctionType, FunctionType

from .autodoc import _binding_name


OMIT_MEMBER_NAMES = frozenset({
    'True',
    'False',
    'None',
    'nil',
    'NotImplemented',
    'Ellipsis',
    'ellipsis',
})

SKIP_TYPE_NAMES = frozenset({
    'bool',
    'NoneType',
    'NilType',
    'ellipsis',
})


def collect_builtins_members(builtins_module=None):
    """
    Return (name, value) pairs from koskari.builtins excluding dunder names

    :param builtins_module: module object, or None to import koskari.builtins
    :return: list of (str, object) tuples
    """

    if builtins_module is None:
        import koskari.builtins as builtins_module

    out = []
    for name in dir(builtins_module):
        if name.startswith('__'):
            continue
        try:
            value = getattr(builtins_module, name)
        except AttributeError:
            continue
        out.append((name, value))
    return out


def member_display_name(name, value):
    """
    Sortable documentation title for a builtins binding

    :param name: module attribute name
    :param value: bound object
    :return: str
    """

    kos_name = _binding_name(value)
    if kos_name is not None:
        return kos_name

    head = getattr(value, 'head', None)
    if head is not None:
        text = str(head)
        if text:
            return text

    return name


def _nil_type():
    """
    Return the type of the koskari nil singleton, or None if unavailable
    """

    try:
        from koskari.lib import nil
    except ImportError:
        return None
    return type(nil)


def should_omit_member(name, value):
    """
    True when a binding is a constant-resolution fallback, not API surface

    :param name: module attribute name
    :param value: bound object
    :return: bool
    """

    if name in OMIT_MEMBER_NAMES:
        return True

    if isinstance(value, bool):
        return True

    if value is None:
        return True

    nil_type = _nil_type()
    if nil_type is not None and isinstance(value, nil_type):
        return True

    if value is NotImplemented:
        return True

    try:
        if value is Ellipsis:
            return True
    except NameError:
        pass

    return False


def should_skip_type_documentation(typeobj):
    """
    True when a type object is only a constant/singleton carrier

    :param typeobj: type
    :return: bool
    """

    if typeobj in (bool, type(None)):
        return True

    if typeobj.__name__ in SKIP_TYPE_NAMES:
        return True

    nil_type = _nil_type()
    if nil_type is not None and typeobj is nil_type:
        return True

    return False


def _is_predicate(value):
    from koskari.types import BuiltinPredicate, TypePredicate

    return isinstance(value, (TypePredicate, BuiltinPredicate))


def _is_callable_member(value):
    from koskari.lib import Function

    if isinstance(value, Function):
        return True

    if inspect.isroutine(value):
        return True

    if isinstance(value, (FunctionType, BuiltinFunctionType)):
        return True

    return False


def _classify_member(name, value):
    """
    Return bucket key or None to skip unknown non-API values

    :param name: module attribute name
    :param value: bound object
    :return: str bucket key or None
    """

    from koskari.reader.layouts.helpers import LayoutHandler
    from koskari.syntax import Syntax

    if isinstance(value, Syntax):
        return 'compile'

    if isinstance(value, LayoutHandler):
        return 'reader'

    if _is_predicate(value):
        return 'runtime_callables'

    if isinstance(value, type):
        try:
            if issubclass(value, BaseException):
                return 'runtime_exceptions'
        except TypeError:
            pass
        if should_skip_type_documentation(value):
            return None
        return 'runtime_types'

    if _is_callable_member(value):
        return 'runtime_callables'

    return None


def _sort_members(pairs):
    """
    Sort (name, value) pairs by display name
    """

    return sorted(
        pairs,
        key=lambda item: (member_display_name(item[0], item[1]), item[0]))


def partition_builtins(members):
    """
    Partition builtins members into documentation sections

    :param members: iterable of (name, value) pairs
    :return: list of (section_title, subsections) where subsections is either
        a list of (rubric_title, member_pairs) for runtime, or member_pairs
        for compile/reader
    """

    buckets = {
        'runtime_callables': [],
        'runtime_types': [],
        'runtime_exceptions': [],
        'compile': [],
        'reader': [],
    }

    for name, value in members:
        if should_omit_member(name, value):
            continue
        bucket = _classify_member(name, value)
        if bucket is None:
            continue
        buckets[bucket].append((name, value))

    runtime_subsections = (
        ('Callables and predicates', _sort_members(buckets['runtime_callables'])),
        ('Types', _sort_members(buckets['runtime_types'])),
        ('Exceptions', _sort_members(buckets['runtime_exceptions'])),
    )

    return (
        ('Runtime features', runtime_subsections),
        ('Compile features', _sort_members(buckets['compile'])),
        ('Reader features', _sort_members(buckets['reader'])),
    )


# The end.
