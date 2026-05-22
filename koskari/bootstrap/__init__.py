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
:ai-assistant: Composer via Cursor
"""


import operator as _op
import sys
from decimal import Decimal as decimal
from fractions import Fraction as fraction
from os.path import dirname, join
from typing import Any, Dict, Optional

from ..lib import (AttributeRef, ConstantRef, Datum, DynamicRef, ItemRef,
                   MemoizedRef, Module, ModuleNamespace, ParamRef, Ref, apply,
                   build_dict, build_list, build_set, build_tuple, car, cdr,
                   cons, defer, demand, demand_apply, demand_call, deref,
                   gensym, keyword, kos_repr, nil, pair, parameter, setcar,
                   setcdr, symbol, values)
from ..types import (TypePredicate, as_coroutine, build_proper, is_nil,
                     is_None, is_proper, is_recursive, unpack)

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

# cache of the builtins dict, None is a sentinel for uninitialized
_BUILTINS_CACHE: Optional[Dict[str, Any]] = None

# order-specific sources for constructing the builtins
_BUILTINS_SOURCES = (
    # baseline define-syntax meta-macros
    '_define_syntax.ki',

    # additional features
    '_conditionals.ki',
    '_variables.ki',
    '_class.ki',
    '_iter.ki',

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

    from .. import exceptions as _koskari_exceptions  # noqa: F401
    from .. import help as _koskari_help  # noqa: F401
    from .. import syntax as _koskari_syntax  # noqa: F401
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

    # pair types and operations
    _ty('defer', defer)
    _ty('demand', demand)
    _ty('keyword', keyword)
    _ty('pair', pair)
    _ty('symbol', symbol)
    _ty('values', values)
    _ty('datum', Datum)

    _bind('apply', apply)
    _bind('as-coroutine', as_coroutine)
    _bind('build-proper', build_proper)
    _bind('car', car)
    _bind('cdr', cdr)
    _bind('cons', cons)
    _bind('demand-apply', demand_apply)
    _bind('demand-call', demand_call)
    _bind('deref', deref)
    _bind('gensym', gensym)
    _bind('nil', nil)
    _bind('nil?', is_nil)
    _bind('parameter', parameter)
    _bind('proper?', is_proper)
    _bind('recursive?', is_recursive)
    _bind('set-car', setcar)
    _bind('set-cdr', setcdr)
    _bind('unpack', unpack)

    # builders
    _bind('#dict', build_dict)
    _bind('#list', build_list)
    _bind('#set', build_set)
    _bind('#str', lambda *args: "".join(args))
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
    _bind('None?', is_None)
    _bind('True', True)
    _bind('False', False)
    _bind('Ellipsis', Ellipsis)
    _bind('...', ...)

    # common builtins
    _bind('abs', abs)
    _bind('all', all)
    _bind('any', any)
    _bind('callable', callable)
    _bind('chr', chr)
    _bind('dir', dir)
    _bind('enumerate', enumerate)
    _bind('filter', filter)
    _bind('format', format)
    _bind('getattr', getattr)
    _bind('hasattr', hasattr)
    _bind('hash', hash)
    _bind('id', id)
    _bind('input', input)
    _bind('isinstance', isinstance)
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
    _bind('repr', kos_repr)
    _bind('reversed', reversed)
    _bind('setattr', setattr)
    _bind('sorted', sorted)
    _bind('super', super)
    _bind('issubclass', issubclass)
    _bind('sum', sum)
    _bind('vars', vars)
    _bind('zip', zip)

    import koskari.lib as kl
    for name in ('*', '+', '-', '/', '//', '&', '|', '^',
                 'in', 'not-in'):
        _bind(name, getattr(kl, name))
    for (name, alias) in (('==', 'equal?'), ('!=', 'not-equal?'),
                          ('<', 'lt?'), ('<=', 'le?'),
                          ('>', 'gt?'), ('>=', 'ge?')):
        _bind(name, getattr(kl, name))
        _bind(alias, getattr(kl, name))

    _bind('**', _op.pow)
    _bind('@', _op.matmul)
    _bind('<<', _op.lshift)
    _bind('>>', _op.rshift)

    for sym, fn in _BUILTIN_REGISTRY.items():
        _bind(sym, fn)

    # Python's builtins, exceptions (all subclasses of BaseException)
    import builtins as _builtins
    for _name in dir(_builtins):
        _val = getattr(_builtins, _name)
        if isinstance(_val, type) and issubclass(_val, BaseException):
            _bind(_name, _val)

    return bltns


async def make_builtins():
    """
    Assemble the full builtins dict (bootstrap + basics.ki).

    Loads basics.ki using bootstrap as its builtins, merges both into a
    single dict, caches the result, and returns it.  Subsequent calls
    return the cached dict.

    :return: dict of builtin name -> value
    """

    global _BUILTINS_CACHE
    if _BUILTINS_CACHE is not None:
        return _BUILTINS_CACHE

    import koskari

    if koskari.builtins is None:
        from ..module import load_module_async
        bootstrap = make_bootstrap()
        ns = ModuleNamespace("koskari.basics", {})
        module = Module(ns)

        for name, val in bootstrap.items():
            ns[name] = val

        ns['__name__'] = 'koskari.builtins'
        ns['__package__'] = 'koskari'

        for src in _BUILTINS_SOURCES:
            ns['__file__'] = join(dirname(__file__), src)
            await load_module_async(module)

        # final fake setup
        sys.modules['koskari.builtins'] = module
        koskari.builtins = module

    for key, val in vars(module).items():
        if not key.startswith('__'):
            try:
                bootstrap[key] = val.deref()
            except Exception:
                pass

    _BUILTINS_CACHE = bootstrap
    return bootstrap


def make_builtins_demand():
    """
    Assemble the full builtins dict (bootstrap + basics.ki) via the demand path.

    Same as make_builtins but uses load_module_demand to load basics.ki so no
    event loop or await is required.  Shares _BUILTINS_CACHE with make_builtins;
    whichever path runs first populates the cache.

    :return: dict of builtin name -> value
    """

    global _BUILTINS_CACHE
    if _BUILTINS_CACHE is not None:
        return _BUILTINS_CACHE

    import koskari

    if koskari.builtins is None:
        from ..module import load_module_demand
        bootstrap = make_bootstrap()
        ns = ModuleNamespace("koskari.basics", {})
        module = Module(ns)

        for name, val in bootstrap.items():
            ns[name] = val

        ns['__name__'] = 'koskari.builtins'
        ns['__package__'] = 'koskari'

        for src in _BUILTINS_SOURCES:
            ns['__file__'] = join(dirname(__file__), src)
            load_module_demand(module)

        # final fake setup
        sys.modules['koskari.builtins'] = module
        koskari.builtins = module

    for key, val in vars(module).items():
        if not key.startswith('__'):
            try:
                bootstrap[key] = val.deref()
            except Exception:
                pass

    _BUILTINS_CACHE = bootstrap
    return bootstrap


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
