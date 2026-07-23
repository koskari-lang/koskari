# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.


"""
koskari.stdlib

Registration hooks for names merged into the Koskari builtins dict.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import operator as _op
from decimal import Decimal as decimal
from fractions import Fraction as fraction
from os.path import dirname, join
from typing import Any, Dict

from ..lib import (AttributeRef, ConstantRef, Datum, DynamicRef, ItemRef,
                   KosPartial, KosProperty, MemoizedRef, Module,
                   ModuleNamespace,
                   ParamRef, Ref, apply, build_dict, build_list, build_set,
                   build_tuple, car, cdr, cons, defer, demand, demand_apply,
                   demand_call, deref, gensym, getattr_star, getitem_star,
                   hasattr_star, hasitem_star, keyword, kos_repr, list_append,
                   bracket_cons, bracket_nil, nil, pair, parameter,
                   parameter_ref, setcar, setcdr, symbol, values)

from ..types import (TypePredicate, is_nil, is_None, is_proper, is_recursive,
                     build_proper, bracket_splice, unpack)


__all__ = (
    'builtin',
    'add_builtin',
    'add_builtin_type',
    'make_bootstrap',
    'make_builtins',
    'make_builtins_demand',
)


# registry filled via the `@builtin` decorator
_BUILTIN_REGISTRY: Dict[str, Any] = {}


# order-specific sources for constructing the builtins
_BUILTINS_SOURCES = (
    # baseline define-syntax meta-macros
    '_define_syntax.ki',

    # conditional expressions
    '_conditionals.ki',

    # loops, comprehensions
    '_iter.ki',

    # assignment, refs, bindings
    # requires: _iter.ki
    '_variables.ki',

    # help
    '_help.ki',

    # pairs
    # requires: _variables.ki
    '_pairs.ki',

    # function composition, decorators
    # requires: _iter.ki
    '_functions.ki',

    # classes
    '_class.ki',

    # pragmas
    # requires: _iter.ki, _help.ki
    '_pragma.ki',

    # assert
    # requires: _define_syntax.ki, _conditionals.ki
    '_assert.ki',

    # fences
    # requires: _define_syntax.ki, _conditionals.ki
    '_fences.ki',

    # final stub
    'basics.ki',
)


def make_bootstrap():
    """
    Assemble the minimal Python-only bootstrap builtins dict.

    Returns a plain dict of primitives, specials, layouts, and exceptions
    used to bootstrap basics.ki.  This dict is sufficient to load and
    execute .ki source; basics.ki is then loaded on top to compose the
    full builtins.

    :return: dict of builtin name -> value
    """

    # delayed imports to avoid circular dependencies, but we do need to ensure
    # they're loaded. They each have decorated builtins that self-register
    # into the _BUILTIN_REGISTRY.

    from .. import asyncio as _koskari_asyncio  # noqa: F401
    from .. import exceptions as _koskari_exceptions  # noqa: F401
    from .. import help as _koskari_help  # noqa: F401
    from .. import syntax as _koskari_syntax  # noqa: F401
    from .. import fences as _koskari_fences  # noqa: F401
    from ..compile import specials as _koskari_compile_specials  # noqa: F401
    from ..reader import layouts as _koskari_reader_layouts  # noqa: F401


    bltns = {}

    def _bind(name, val):
        bltns[name] = val

    def _ty(name, typeobj):
        _bind(name, typeobj)
        _bind(name + '?', TypePredicate(name + '?', typeobj))

    _ty('Ref', Ref)
    _ty('AttributeRef', AttributeRef)
    _ty('ItemRef', ItemRef)
    _ty('DynamicRef', DynamicRef)
    _ty('ConstantRef', ConstantRef)
    _ty('MemoizedRef', MemoizedRef)
    _ty('ParamRef', ParamRef)
    _bind('ref?', TypePredicate('ref?', Ref))

    # types from koskari.lib
    _ty('defer', defer)
    _ty('demand', demand)
    _ty('partial', KosPartial)
    _ty('keyword', keyword)
    _ty('pair', pair)
    _ty('symbol', symbol)
    _ty('values', values)
    _ty('datum', Datum)

    # functions from koskari.lib
    _bind('apply', apply)
    _bind('car', car)
    _bind('cdr', cdr)
    _bind('cons', cons)
    _bind('bracket-cons', bracket_cons)
    _bind('demand-apply', demand_apply)
    _bind('demand-call', demand_call)
    _bind('deref', deref)
    _bind('gensym', gensym)
    _bind('nil', nil)
    _bind('bracket-nil', bracket_nil)
    _bind('parameter', parameter)
    _bind('parameter-ref', parameter_ref)
    _bind('repr', kos_repr)
    _bind('set-car', setcar)
    _bind('set-cdr', setcdr)

    # builders
    _bind('#dict', build_dict)
    _bind('#list', build_list)
    _bind('#list-append', list_append)
    _bind('#set', build_set)
    _bind('#str', lambda *args: "".join(map(str, args)))
    _bind('#tuple', build_tuple)

    # Python standard types available by default
    _ty('bool', bool)
    _ty('bytes', bytes)
    _ty('classmethod', classmethod)
    _ty('complex', complex)
    _ty('decimal', decimal)
    _ty('dict', dict)
    _ty('float', float)
    _ty('fraction', fraction)
    _ty('frozenset', frozenset)
    _ty('int', int)
    _ty('list', list)
    _ty('object', object)
    _ty('property', KosProperty)
    _ty('range', range)
    _ty('set', set)
    _ty('staticmethod', staticmethod)
    _ty('slice', slice)
    _ty('str', str)
    _ty('tuple', tuple)
    _ty('type', type)

    # These are parsed as literals, not as named references, similar
    # to numbers and strings.
    _bind('None', None)
    _bind('True', True)
    _bind('False', False)
    _bind('Ellipsis', Ellipsis)
    _bind('...', ...)

    # utils from koskari.types
    _bind('nil?', is_nil)
    _bind('None?', is_None)
    _bind('proper?', is_proper)
    _bind('recursive?', is_recursive)
    _bind('build-proper', build_proper)
    _bind('bracket-splice', bracket_splice)
    _bind('unpack', unpack)

    # common Python builtins
    _bind('abs', abs)
    _bind('all?', all)
    _bind('any?', any)
    _bind('callable?', callable)
    _bind('chr', chr)
    _bind('dir', dir)
    _bind('enumerate', enumerate)
    _bind('filter', filter)
    _bind('format', format)
    _bind('getattr', getattr)
    _bind('getattr*', getattr_star)
    _bind('getitem*', getitem_star)
    _bind('hasattr?', hasattr)
    _bind('hasattr*', hasattr_star)
    _bind('hasitem*', hasitem_star)
    _bind('hash', hash)
    _bind('id', id)
    _bind('input', input)
    _bind('instance?', isinstance)
    _bind('iter', iter)
    _bind('len', len)
    _bind('map', map)
    _bind('max', max)
    _bind('min', min)
    _bind('next', next)
    _bind('open', open)
    _bind('ord', ord)
    _bind('print', print)
    _bind('py-help', help)
    _bind('py-repr', repr)
    _bind('reversed', reversed)
    _bind('round', round)
    _bind('setattr', setattr)
    _bind('sorted', sorted)
    _bind('super', super)
    _bind('subclass?', issubclass)
    _bind('sum', sum)
    _bind('vars', vars)
    _bind('zip', zip)

    import koskari.lib as kl
    for name in ('*', '+', '-', '/', '//', '&', '|', '^'):
        _bind(name, getattr(kl, name))
    for (name, alias) in (('==', 'equal?'), ('!=', 'not-equal?'),
                          ('<', 'lt?'), ('<=', 'le?'),
                          ('>', 'gt?'), ('>=', 'ge?'),
                          ('in', 'in?'), ('not-in', 'not-in?')):
        val = getattr(kl, name)
        _bind(name, val)
        _bind(alias, val)

    _bind("is?", _op.is_)
    _bind("is-not?", _op.is_not)
    _bind('**', _op.pow)
    _bind('@', _op.matmul)
    _bind('<<', _op.lshift)
    _bind('>>', _op.rshift)
    _bind('itemgetter', _op.itemgetter)
    _bind('attrgetter', _op.attrgetter)

    for sym, fn in _BUILTIN_REGISTRY.items():
        _bind(sym, fn)

    # Python's builtins, exceptions (all subclasses of BaseException)
    import builtins as py_builtins
    for name in dir(py_builtins):
        val = getattr(py_builtins, name)
        if isinstance(val, type) and issubclass(val, BaseException):
            _bind(name, val)

    return bltns


def _init_ns():
    """
    Create a ModuleNamespace pre-filled with the Python bootstrap registry
    """

    bootstrap = make_bootstrap()
    ns = ModuleNamespace("koskari.builtins", {})

    for name, val in bootstrap.items():
        ns[name] = val

    ns['__name__'] = 'koskari.builtins'
    ns['__package__'] = 'koskari'

    return ns


async def make_builtins():
    """
    Assemble the full builtins module (bootstrap registry plus basics.ki)

    Uses load_module_async to evaluate basics.ki.  Prefer importing
    koskari.builtins for normal use; this helper remains for callers that
    need a koskari Module handle assembled on the async path.

    :return: koskari Module with bootstrap and basics.ki bindings in its namespace
    """

    from ..module import detach_module_load_facets, load_module_async

    ns = _init_ns()
    module = Module(ns)

    # the builtins are one module loaded in installments: each file
    # runs with seal and detach deferred, so every load reuses the same
    # pragma, compiler, and reader facets
    for src in _BUILTINS_SOURCES:
        ns['__file__'] = join(dirname(__file__), src)
        await load_module_async(module, seal=False, detach=False)

    # one seal for the whole assembly: the shared compiler's registry
    # spans every file, so everything packs into a single arena and
    # code a later file displaced is dropped rather than pinned. No
    # gc.collect() here: a displaced definition caught in a reference
    # cycle would be swept in, wasting only its bytes in an arena that
    # is permanently resident anyway (measured: currently none are)
    ns['__compiler__'].seal()
    detach_module_load_facets(module)

    return module


def make_builtins_demand():
    """
    Assemble the full builtins module (bootstrap registry plus basics.ki)

    Same as make_builtins but uses load_module_demand so no event loop or
    await is required.  koskari.builtins calls this at import time via
    _setup().

    :return: koskari Module with bootstrap and basics.ki bindings in its namespace
    """

    from ..module import detach_module_load_facets, load_module_demand

    ns = _init_ns()
    module = Module(ns)

    # one module loaded in installments through shared facets, sealed
    # once at the end; see make_builtins
    for src in _BUILTINS_SOURCES:
        ns['__file__'] = join(dirname(__file__), src)
        load_module_demand(module, seal=False, detach=False)

    ns['__compiler__'].seal()
    detach_module_load_facets(module)

    return module


def builtin(name: str, *aliases: str):
    """
    Register the decorated object under explicit stdlib string keys

    Each name must be the bootstrap string for that binding (no inference).

    :param names: one or more builtin name strings
    :return: decorator that registers ``obj`` and returns it unchanged
    :raises TypeError: when ``names`` is empty or any entry is not a ``str``
    """

    def decorator(obj):
        _BUILTIN_REGISTRY[name] = obj
        for alias in aliases:
            _BUILTIN_REGISTRY[alias] = obj
        return obj

    return decorator


def add_builtin(name: str, obj: Any):
    _BUILTIN_REGISTRY[name] = obj


def add_builtin_type(name: str, typeobj: Any):
    add_builtin(name, typeobj)
    add_builtin(name + '?', TypePredicate(name + '?', typeobj))


# The end.
