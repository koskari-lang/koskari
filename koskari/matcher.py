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
koskari.matcher

Pattern matching for Koskari source forms. Matchers consume source and bind
matches into a result dict. Supports both Python builder objects and tagged
tuple interpretation for future define-syntax integration.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from typing import Any, Dict, Optional, Protocol, Tuple

from .lib import car, cdr, cons, keyword, nil, symbol
from .types import is_pair, is_symbol, unpack


__all__ = (
    'Matcher',
    'matcher',

    'ANY_ORDER',
    'BIND',
    'BIND_PAIR',
    'BIND_SYM',
    'LITERAL',
    'LITERAL_KW',
    'LITERAL_SYM',
    'MATCH',
    'OPT',
    'PAIR',
    'REST',
    'extract_binder_symbols',
    'from_tuple',
    'is_binder_spec',
    'is_scope_region_spec',
    'match',
    'match_tuple',
)


MATCHERS = {}


def matcher(name):
    """
    Decorator to register a matcher class
    """

    def decorator(cls):
        MATCHERS[name] = cls
        return cls
    return decorator


class Matcher(Protocol):
    """
    Protocol for matcher types. Accept source, optionally mutate result with
    bindings, return (remainder, success). When specs is provided, capturing
    matchers record themselves in specs.
    """

    def match(
            self,
            source: Any,
            literals: frozenset,
            result: Dict[str, Any],
            specs: Optional[Dict[str, Any]] = None) -> Tuple[Optional[Any], bool]:
        """
        Try to match source. On success: mutate result with bindings, return
        (remainder, True). On failure: return (None, False). When specs is not
        None, capturing matchers add specs[key] = self.
        """
        ...


# Tag symbols for serialization
_tag_match = symbol('matcher/match')
_tag_literal = symbol('matcher/literal')
_tag_bind = symbol('matcher/bind')
_tag_bind_sym = symbol('matcher/bind-sym')
_tag_bind_pair = symbol('matcher/bind-pair')
_tag_rest = symbol('matcher/rest')
_tag_pair = symbol('matcher/pair')
_tag_opt = symbol('matcher/opt')
_tag_any_order = symbol('matcher/any-order')


def _slot_key(slot):
    """
    Return str key for a slot (symbol or str)
    """

    return str(slot)


def _ensure_pair(source):
    """
    Convert iterable to pair; return pair or nil unchanged
    """

    if source is nil or is_pair(source):
        return source
    return cons(*tuple(source), nil) if source else nil


def _is_tagged_tuple(tup):
    """
    True if tup is a non-empty tuple/list with symbol as first element.
    """

    if not isinstance(tup, (tuple, list)) or not tup:
        return False

    return is_symbol(tup[0])


def _resolve_spec(spec, literals):
    """
    Resolve spec to matcher: object with match(), or tuple to interpret
    """

    if hasattr(spec, 'match') and callable(getattr(spec, 'match')):
        return spec

    if _is_tagged_tuple(spec):
        return spec

    return None


@matcher(_tag_literal)
class _LiteralMatcher:
    """
    Match exact value
    """

    def __init__(self, value):
        self.value = value


    @classmethod
    def from_payload(cls, payload, deserialize):
        if len(payload) != 1:
            raise ValueError('literal expects 1 payload, got %d' % len(payload))
        return cls(payload[0])


    def match(self, source, literals, result, specs=None):
        if source is nil:
            return (None, False)
        head, tail = car(source), cdr(source)
        if head == self.value:
            return (tail, True)
        return (None, False)


    def to_tuple(self):
        return (_tag_literal, self.value)


@matcher(_tag_bind)
class _BindMatcher:
    """
    Capture one element
    """

    def __init__(self, slot):
        self.slot = slot
        self._key = _slot_key(slot)


    @classmethod
    def from_payload(cls, payload, deserialize):
        if len(payload) != 1:
            raise ValueError('bind expects 1 payload, got %d' % len(payload))
        return cls(payload[0])


    def match(self, source, literals, result, specs=None):
        if source is nil:
            return (None, False)
        head, tail = car(source), cdr(source)
        result[self._key] = head
        if specs is not None:
            specs[self._key] = self
        return (tail, True)


    def to_tuple(self):
        return (_tag_bind, symbol(self._key) if isinstance(self.slot, str) else self.slot)


@matcher(_tag_bind_sym)
class _BindSymMatcher:
    """
    Capture one element only when it is a symbol
    """

    def __init__(self, slot):
        self.slot = slot
        self._key = _slot_key(slot)


    @classmethod
    def from_payload(cls, payload, deserialize):
        if len(payload) != 1:
            raise ValueError('bind-sym expects 1 payload, got %d' % len(payload))
        return cls(payload[0])


    def match(self, source, literals, result, specs=None):
        if source is nil:
            return (None, False)
        head, tail = car(source), cdr(source)
        if not is_symbol(head):
            return (None, False)
        result[self._key] = head
        if specs is not None:
            specs[self._key] = self
        return (tail, True)


    def to_tuple(self):
        return (_tag_bind_sym, symbol(self._key) if isinstance(self.slot, str) else self.slot)


@matcher(_tag_bind_pair)
class _BindPairMatcher:
    """
    Capture one element only when it is a pair
    """

    def __init__(self, slot):
        self.slot = slot
        self._key = _slot_key(slot)


    @classmethod
    def from_payload(cls, payload, deserialize):
        if len(payload) != 1:
            raise ValueError('bind-pair expects 1 payload, got %d' % len(payload))
        return cls(payload[0])


    def match(self, source, literals, result, specs=None):
        if source is nil:
            return (None, False)
        head, tail = car(source), cdr(source)
        if not is_pair(head):
            return (None, False)
        result[self._key] = head
        if specs is not None:
            specs[self._key] = self
        return (tail, True)


    def to_tuple(self):
        return (_tag_bind_pair, symbol(self._key) if isinstance(self.slot, str) else self.slot)


@matcher(_tag_rest)
class _RestMatcher:
    """
    Capture remainder of list
    """

    def __init__(self, slot):
        self.slot = slot
        self._key = _slot_key(slot)


    @classmethod
    def from_payload(cls, payload, deserialize):
        if len(payload) != 1:
            raise ValueError('rest expects 1 payload, got %d' % len(payload))
        return cls(payload[0])


    def match(self, source, literals, result, specs=None):
        result[self._key] = source
        if specs is not None:
            specs[self._key] = self
        return (nil, True)


    def to_tuple(self):
        return (_tag_rest, symbol(self._key) if isinstance(self.slot, str) else self.slot)


@matcher(_tag_pair)
class _PairMatcher:
    """
    Match pair; car against car_spec, cdr bound per cdr_spec
    """

    def __init__(self, car_spec, cdr_spec):
        self.car_spec = car_spec
        self.cdr_spec = cdr_spec


    @classmethod
    def from_payload(cls, payload, deserialize):
        if len(payload) != 2:
            raise ValueError('pair expects 2 payload, got %d' % len(payload))
        return cls(deserialize(payload[0]), deserialize(payload[1]))


    def match(self, source, literals, result, specs=None):
        if source is nil:
            return (None, False)
        elem = car(source)
        tail = cdr(source)
        if not is_pair(elem):
            return (None, False)

        elem_car, elem_cdr = car(elem), cdr(elem)
        car_matcher = _resolve_spec(self.car_spec, literals)
        cdr_matcher = _resolve_spec(self.cdr_spec, literals)

        if car_matcher is not None:
            rem, ok = _do_match(car_matcher, cons(elem_car, nil), literals, result, specs)
            if not ok or rem is not nil:
                return (None, False)
        else:
            if elem_car != self.car_spec:
                return (None, False)

        if cdr_matcher is not None:
            rem, ok = _do_match(cdr_matcher, cons(elem_cdr, nil), literals, result, specs)
            if not ok:
                return (None, False)

        elif is_symbol(self.cdr_spec):
            key = _slot_key(self.cdr_spec)
            result[key] = elem_cdr
            if specs is not None:
                specs[key] = self

        else:
            return (None, False)

        return (tail, True)


    def to_tuple(self):
        car_t = (self.car_spec.to_tuple()
                 if hasattr(self.car_spec, 'to_tuple') else (_tag_literal, self.car_spec))
        cdr_t = (self.cdr_spec.to_tuple()
                 if hasattr(self.cdr_spec, 'to_tuple') else (_tag_bind, self.cdr_spec))
        return (_tag_pair, car_t, cdr_t)


@matcher(_tag_opt)
class _OptMatcher:
    """
    Optionally match elem
    """

    def __init__(self, elem):
        self.elem = elem


    @classmethod
    def from_payload(cls, payload, deserialize):
        if len(payload) != 1:
            raise ValueError('opt expects 1 payload, got %d' % len(payload))
        return cls(deserialize(payload[0]))


    def match(self, source, literals, result, specs=None):
        matcher = _resolve_spec(self.elem, literals)
        if matcher is None:
            return (source, True)
        rem, ok = _do_match(matcher, source, literals, result, specs)
        if ok:
            return (rem, True)
        return (source, True)


    def to_tuple(self):
        elem_t = (self.elem.to_tuple()
                  if hasattr(self.elem, 'to_tuple') else self.elem)
        return (_tag_opt, elem_t)


@matcher(_tag_match)
class _MatchMatcher:
    """
    Sequence of specs in order
    """

    def __init__(self, *specs):
        self.specs = specs


    @classmethod
    def from_payload(cls, payload, deserialize):
        return cls(*[deserialize(p) for p in payload])


    def match(self, source, literals, result, specs=None):
        current = source
        for spec in self.specs:
            matcher = _resolve_spec(spec, literals)
            if matcher is None:
                return (None, False)
            current, ok = _do_match(matcher, current, literals, result, specs)
            if not ok:
                return (None, False)
        return (current, True)


    def to_tuple(self):
        parts = []
        for s in self.specs:
            parts.append(s.to_tuple() if hasattr(s, 'to_tuple') else s)
        return (_tag_match,) + tuple(parts)


@matcher(_tag_any_order)
class _AnyOrderMatcher:
    """
    All specs must match, in any order
    """

    def __init__(self, *specs):
        self.specs = specs


    @classmethod
    def from_payload(cls, payload, deserialize):
        return cls(*[deserialize(p) for p in payload])


    def match(self, source, literals, result, specs=None):
        elements = list(unpack(source)) if source is not nil else []
        pool = list(elements)
        for spec in self.specs:
            matcher = _resolve_spec(spec, literals)
            if matcher is None:
                return (None, False)
            matched = False
            for i, elem in enumerate(pool):
                elem_pair = cons(elem, nil)
                rem, ok = _do_match(matcher, elem_pair, literals, result, specs)
                if ok and (rem is nil or (is_pair(rem) and cdr(rem) is nil)):
                    del pool[i]
                    matched = True
                    break
            if not matched:
                return (None, False)
        return (nil, True)


    def to_tuple(self):
        parts = []
        for s in self.specs:
            parts.append(s.to_tuple() if hasattr(s, 'to_tuple') else s)
        return (_tag_any_order,) + tuple(parts)


def from_tuple(tup):
    """
    Deserialize tagged tuple into matcher object

    :param tup: tagged tuple (tag, ...payload)
    :return: matcher object with match() and to_tuple()
    :raises ValueError: when tup is not a valid matcher tuple
    """

    if not _is_tagged_tuple(tup):
        raise ValueError('not a valid matcher tuple: %r' % (tup,))

    tag = tup[0]
    payload = tup[1:]
    cls = MATCHERS.get(tag)
    if cls is None:
        raise ValueError('unknown matcher tag: %r' % (tag,))

    return cls.from_payload(payload, from_tuple)


def _do_match(matcher, source, literals, result, specs=None):
    """
    Dispatch to matcher object; deserialize tuple first if needed
    """

    if hasattr(matcher, 'match') and callable(getattr(matcher, 'match')):
        return matcher.match(source, literals, result, specs)

    if _is_tagged_tuple(matcher):
        obj = from_tuple(matcher)
        return obj.match(source, literals, result, specs)

    return (None, False)


def LITERAL(value):
    """
    Match exact value
    """

    return _LiteralMatcher(value)


def LITERAL_KW(s):
    """
    Match keyword built from string
    """

    return _LiteralMatcher(keyword(s))


def LITERAL_SYM(s):
    """
    Match symbol built from string
    """

    return _LiteralMatcher(symbol(s))


def BIND(slot):
    """
    Capture one element into slot
    """

    return _BindMatcher(slot)


def BIND_SYM(slot):
    """
    Capture one element into slot only when it is a symbol
    """

    return _BindSymMatcher(slot)


def BIND_PAIR(slot):
    """
    Capture one element into slot only when it is a pair
    """

    return _BindPairMatcher(slot)


def REST(slot):
    """
    Capture remainder of list into slot
    """

    return _RestMatcher(slot)


def PAIR(car_spec, cdr_spec):
    """
    Match pair; car matches car_spec, cdr bound per cdr_spec
    """

    return _PairMatcher(car_spec, cdr_spec)


def OPT(elem):
    """
    Optionally match elem
    """

    return _OptMatcher(elem)


def MATCH(*specs):
    """
    Sequence of specs in order
    """

    return _MatchMatcher(*specs)


def ANY_ORDER(*specs):
    """
    All specs must match, in any order
    """

    return _AnyOrderMatcher(*specs)


def match(
        source,
        patterns,
        literals=None,
        with_specs=False):
    """
    Try each pattern against source. Return dict of bindings on first match,
    else None

    :param source: pair (proper list) or iterable to unpack to list
    :param patterns: single matcher, tagged tuple, or sequence of patterns
    :param literals: set or frozenset of symbols to match literally; default
        empty frozenset
    :param with_specs: if True, return (result_dict, specs_dict) on match
    :return: dict mapping slot names to values; or (dict, dict) if with_specs
        True; or None on no match
    """

    lit = frozenset(literals) if literals is not None else frozenset()
    src = _ensure_pair(source)

    if not isinstance(patterns, (list, tuple)):
        patterns = [patterns]

    for pat in patterns:
        result = {}
        specs = {} if with_specs else None
        matcher = _resolve_spec(pat, lit)
        if matcher is None:
            continue
        rem, ok = _do_match(matcher, src, lit, result, specs)
        if ok:
            if with_specs:
                return (result, specs)
            return result

    return None


def is_binder_spec(spec):
    """
    True if spec is a binder matcher (BIND_SYM, BINDINGS, FORMALS)

    Used by hygiene discovery to identify captures that introduce bindings.
    """

    if spec is None:
        return False
    name = type(spec).__name__
    return name in ('_BindSymMatcher', '_BindingsMatcher')


def is_scope_region_spec(spec):
    """
    True if spec captures a region usable as a body (REST, EXPR)

    Used by hygiene discovery for scope inference.
    """

    if spec is None:
        return False
    name = type(spec).__name__
    return name in ('_RestMatcher', '_ExprMatcher')


def extract_binder_symbols(spec, value):
    """
    Given a binder spec and its matched value, yield symbol names introduced
    """

    if spec is None or value is None:
        return
    name = type(spec).__name__
    if name == '_BindSymMatcher':
        if is_symbol(value):
            yield value
    elif name == '_BindingsMatcher':
        if not isinstance(value, (tuple, list)) or len(value) != 2:
            return
        names, _values = value
        for n in names or ():
            if is_symbol(n):
                yield n


def match_tuple(
        source,
        tup,
        literals=None):
    """
    Deserialize tagged tuple to matcher, then match against source. Convenience
    for the common from_tuple + match pattern.

    :param source: pair (proper list) or iterable to unpack to list
    :param tup: tagged tuple pattern
    :param literals: set or frozenset of symbols to match literally; default
        empty frozenset
    :return: dict mapping slot names to values, or None
    """

    matcher = from_tuple(tup)
    return match(source, matcher, literals=literals)


# The end.
