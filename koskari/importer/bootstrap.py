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
koskari.importer.builtins

Builtin environment for Koskari modules.

Provides make_bootstrap() (minimal Python-only layer) and make_builtins()
(merge of bootstrap and basics.ki).  The full builtins dict is passed to
ModuleNamespace for every .ki module.  make_builtins loads basics.ki
using bootstrap as its builtins, then merges both and caches the result.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""

from decimal import Decimal as decimal
from fractions import Fraction as fraction
from os.path import dirname, join

from .._sync import as_coroutine
from ..compile.specials import SPECIALS
from ..compile.syntax import build_syntax as build_syntax_rules
from ..lib import (AttributeRef, ConstantRef, DynamicRef, ItemRef, MemoizedRef,
                   Module, ModuleNamespace, ParamRef, Ref, apply, build_dict,
                   build_list, build_set, build_tuple, car, cdr, cons, defer,
                   demand_sync, deref, gensym, keyword, nil, pair, parameter,
                   setcar, setcdr, symbol, values)
from ..reader.layouts import LAYOUTS
from ..types import (TypePredicate, build_proper, is_nil, is_proper,
                     is_recursive, unpack)

__all__ = ('make_bootstrap', 'make_builtins', 'make_builtins_demand')


_BUILTINS_CACHE = None


def make_bootstrap():
    """
    Assemble the minimal Python-only bootstrap builtins dict.

    Returns a plain dict of primitives, specials, layouts, and exceptions
    used to bootstrap basics.ki.  This dict is sufficient to load and
    execute .ki source; basics.ki is then loaded on top to compose the
    full builtins.

    :return: dict of builtin name -> value
    """

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
    _ty('pair', pair)
    _bind('nil', nil)
    _bind('nil?', is_nil)
    _bind('cons', cons)
    _bind('car', car)
    _bind('cdr', cdr)
    _bind('set-car', setcar)
    _bind('set-cdr', setcdr)

    _bind('#list', build_list)
    _bind('#tuple', build_tuple)
    _bind('#set', build_set)
    _bind('#dict', build_dict)

    _bind('proper?', is_proper)
    _bind('recursive?', is_recursive)
    _bind('build-proper', build_proper)
    _bind('unpack', unpack)
    _bind('deref', deref)
    _bind('apply', apply)
    _bind('parameter', parameter)
    _bind('demand-sync', demand_sync)

    _bind('as-coroutine', as_coroutine)

    # atom types and operations
    _ty('symbol', symbol)
    _ty('keyword', keyword)
    _bind('gensym', gensym)

    # values type (multiple return values)
    _ty('values', values)
    _ty('defer', defer)

    # Python standard types available by default
    _ty('bool', bool)
    _ty('int', int)
    _ty('float', float)
    _ty('complex', complex)
    _ty('str', str)
    _ty('bytes', bytes)
    _ty('tuple', tuple)
    _ty('list', list)
    _ty('dict', dict)
    _ty('set', set)
    _ty('frozenset', frozenset)
    _ty('type', type)
    _ty('slice', slice)
    _ty('fraction', fraction)
    _ty('decimal', decimal)

    # These are parsed as literals, not as named references, similar
    # to numbers and strings.
    _bind('None', None)
    _bind('True', True)
    _bind('False', False)
    _bind('Ellipsis', Ellipsis)
    _bind('...', ...)

    # common builtins
    _bind('len', len)
    _bind('repr', repr)
    _bind('str', str)
    _bind('print', print)
    _bind('getattr', getattr)
    _bind('setattr', setattr)
    _bind('hasattr', hasattr)
    _bind('isinstance', isinstance)
    _bind('issubclass', issubclass)
    _bind('callable', callable)
    _bind('iter', iter)
    _bind('next', next)
    _bind('sorted', sorted)
    _bind('reversed', reversed)
    _bind('enumerate', enumerate)
    _bind('zip', zip)
    _bind('map', map)
    _bind('filter', filter)
    _bind('any', any)
    _bind('all', all)
    _bind('min', min)
    _bind('max', max)
    _bind('sum', sum)
    _bind('abs', abs)
    _bind('hash', hash)
    _bind('id', id)
    _bind('vars', vars)
    _bind('open', open)
    _bind('range', range)
    _bind('format', format)
    _bind('chr', chr)
    _bind('ord', ord)

    _bind('dir', dir)

    # todo: replace me with a better help built-in
    _bind('help', help)

    # syntax-rule constructor for define-syntax lowering
    _bind('build-syntax-rules', build_syntax_rules)

    import koskari.lib as kl
    for name in ('*', '+', '-', '/', '//', '&', '|', '^',
                 'in', 'not-in', ):
        _bind(name, getattr(kl, name))

    # Register all compiler specials so the Compiler can find them
    for sym, fn in SPECIALS.items():
        _bind(str(sym), fn)

    # Register all reader layout handlers so the LayoutReader can find them
    for sym, handler in LAYOUTS.items():
        _bind(str(sym), handler)

    # exceptions (all subclasses of BaseException from builtins)
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

    from . import module as _mod
    from .module import init_module, load_module_async

    bootstrap = make_bootstrap()
    ns = ModuleNamespace("koskari.basics", bootstrap)
    basics_module = Module(ns)

    _mod._ns_store(ns, '__name__', 'koskari.basics')

    basics_path = join(dirname(__file__), 'basics.ki')
    init_module(basics_module, basics_path)

    await load_module_async(basics_module)

    for key, val in vars(basics_module).items():
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

    from . import module as _mod
    from .module import init_module, load_module_demand

    bootstrap = make_bootstrap()
    ns = ModuleNamespace("koskari.basics", bootstrap)
    basics_module = Module(ns)

    _mod._ns_store(ns, '__name__', 'koskari.basics')

    basics_path = join(dirname(__file__), 'basics.ki')
    init_module(basics_module, basics_path)

    load_module_demand(basics_module)

    for key, val in vars(basics_module).items():
        if not key.startswith('__'):
            try:
                bootstrap[key] = val.deref()
            except Exception:
                pass

    _BUILTINS_CACHE = bootstrap
    return bootstrap


# The end.
