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

Provides make_builtins(), which assembles the initial Python dict that
becomes the builtins layer of every ModuleNamespace created for a .ki
module.  The dict contains the core koskari primitive types and functions
that are available in every module without an explicit import.

This is the bootstrap layer only.  Once defsyntax and a sufficient subset
of the language are available, a basics.ki source file will be loaded on
top of this dict to compose the full builtins environment.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


from ..lib import (
    car,
    cdr,
    cons,
    deref,
    nil,
    pair,
    symbol,
    keyword,
    gensym,
    setcar,
    setcdr,
    build_unpack_pair,
    values,
)
from ..types import (
    TypePredicate,
    is_nil,
    is_proper,
    is_recursive,
    build_proper,
    unpack,
)

from ..compile.specials import SPECIALS
from ..reader.layouts import LAYOUTS


__all__ = ('make_builtins',)


def make_builtins():
    """
    Assemble the initial builtins dict for a Koskari module namespace.

    Returns a plain dict mapping name strings to values.  This dict is
    passed to ModuleNamespace as its builtins layer, making these names
    available in every .ki module without an explicit import.

    :return: dict of builtin name -> value
    """

    bltns = {}

    def _bind(name, val):
        bltns[name] = val

    def _ty(name, typeobj):
        _bind(name, typeobj)
        _bind(name + '?', TypePredicate(name + '?', typeobj))

    # pair types and operations
    _ty('pair', pair)
    _bind('nil', nil)
    _bind('nil?', is_nil)
    _bind('cons', cons)
    _bind('car', car)
    _bind('cdr', cdr)
    _bind('set-car', setcar)
    _bind('set-cdr', setcdr)
    _bind('build-unpack-pair', build_unpack_pair)
    _bind('proper?', is_proper)
    _bind('recursive?', is_recursive)
    _bind('build-proper', build_proper)
    _bind('unpack', unpack)
    _bind('deref', deref)

    # atom types and operations
    _ty('symbol', symbol)
    _ty('keyword', keyword)
    _bind('gensym', gensym)

    # values type (multiple return values)
    _ty('values', values)

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

    # common builtins
    _bind('None', None)
    _bind('True', True)
    _bind('False', False)
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
    _bind('dir', dir)
    _bind('open', open)
    _bind('range', range)

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


# The end.
