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
from .types import get_position, is_nil, is_pair, is_symbol, unpack


__all__ = (
    'BindingMatcher',
    'BindingsResult',
    'BindingResult',
    'FormalsResult',
    'MatchResult',
    'Matcher',
    'matcher',
    'RepeatResult',

    'ANY_ORDER',
    'BIND',
    'DEFINE',
    'EXPR',
    'LITERAL',
    'LITERAL_KW',
    'LITERAL_SYM',
    'MATCH',
    'OPT',
    'PAIR',
    'REPEAT',
    'REST',
    'SYMBOL',
    'VALUE',
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


    def to_tuple(self):
        """
        Return tagged tuple representation of matcher.
        """
        ...


    @classmethod
    def from_tuple(cls, tup):
        """
        Deserialize tagged tuple to matcher.
        """
        ...


class BindingMatcher(Matcher):
    """
    Base for matchers that introduce bindings. Subclasses implement
    extract_binder_symbols and get_recurse_targets for hygiene discovery.
    """

    def extract_binder_symbols(self, value):
        """
        Yield symbol names introduced by this binder. Override in subclasses.
        """

        yield from ()


    def get_recurse_targets(self, value):
        """
        Return iterable of values to recurse into for nested binding discovery,
        or None if none. Override in subclasses.
        """

        return None


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
_tag_expr = symbol('matcher/expr')
_tag_define = symbol('matcher/define')
_tag_value = symbol('matcher/value')
_tag_repeat = symbol('matcher/repeat')


class MatchResult:
    """
    Base result for simple captures. Every capture includes source (raw value)
    and source_pos (position when source is a pair).
    """

    __slots__ = ('source', 'source_pos')

    def __init__(self, source, source_pos=None):
        self.source = source
        self.source_pos = source_pos


class FormalsResult(MatchResult):
    """
    Result for FORMALS: source, source_pos, plus positional, defaults, kwonly,
    star, starstar. Supports unpacking for backward compat.
    """

    __slots__ = ('positional', 'defaults', 'kwonly', 'star', 'starstar')

    def __init__(
            self,
            source,
            source_pos=None,
            positional=None,
            defaults=None,
            kwonly=None,
            star=None,
            starstar=None):
        super().__init__(source, source_pos)
        self.positional = positional if positional is not None else []
        self.defaults = defaults if defaults is not None else []
        self.kwonly = kwonly if kwonly is not None else []
        self.star = star
        self.starstar = starstar


    def __iter__(self):
        return iter((self.positional, self.defaults, self.kwonly,
                     self.star, self.starstar))


class BindingsResult(MatchResult):
    """
    Result for BINDINGS: source, source_pos, plus names, values. Supports
    unpacking for backward compat.
    """

    __slots__ = ('names', 'values')


    def __init__(self, source, source_pos=None, names=None, values=None):
        super().__init__(source, source_pos)
        self.names = names if names is not None else []
        self.values = values if values is not None else []


    def __iter__(self):
        return iter((self.names, self.values))


class BindingResult(MatchResult):
    """
    Result for BINDING: source, source_pos, plus name (symbol or None),
    value (the expression).
    """

    __slots__ = ('name', 'value')


    def __init__(self, source, source_pos=None, name=None, value=None):
        super().__init__(source, source_pos)
        self.name = name
        self.value = value


class RepeatResult(MatchResult):
    """
    Result for REPEAT: source (consumed list as pair), source_pos, matches.
    matches: list of (result_dict, specs_dict) per repetition.
    """

    __slots__ = ('matches',)

    def __init__(self, source, source_pos=None, matches=None):
        super().__init__(source, source_pos)
        self.matches = matches if matches is not None else []


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




def _capture_head(source, key, result, matcher_obj, specs=None, predicate=None):
    """
    Capture source head into result[key], optionally guarded by predicate.
    """

    if source is nil:
        return (None, False)
    head, tail = source
    if predicate is not None and not predicate(head):
        return (None, False)
    result[key] = MatchResult(head, get_position(source))
    if specs is not None:
        specs[key] = matcher_obj
    return (tail, True)


def _resolve_spec(spec):
    """
    Resolve spec to matcher: object with match(), or tuple to interpret
    """

    if hasattr(spec, 'match') and callable(getattr(spec, 'match')):
        return spec

    if _is_tagged_tuple(spec):
        return spec

    return None


@matcher(_tag_literal)
class _LiteralMatcher(Matcher):
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
        head, tail = source
        if head == self.value:
            return (tail, True)
        return (None, False)


    def to_tuple(self):
        return (_tag_literal, self.value)


@matcher(_tag_bind)
class _BindMatcher(Matcher):
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
        return _capture_head(source, self._key, result, self, specs)


    def to_tuple(self):
        return (_tag_bind, symbol(self._key) if isinstance(self.slot, str) else self.slot)


@matcher(_tag_bind_sym)
class _SymbolMatcher(BindingMatcher):
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
        return _capture_head(source, self._key, result, self, specs, is_symbol)


    def extract_binder_symbols(self, value):
        raw = value.source if hasattr(value, 'source') else value
        if is_symbol(raw):
            yield raw


    def to_tuple(self):
        return (_tag_bind_sym, symbol(self._key) if isinstance(self.slot, str) else self.slot)


@matcher(_tag_define)
class _DefineMatcher(BindingMatcher):
    """
    Capture one element only when it is a symbol; binder for hygiene

    Single-introduced-name form like (define X Y). Does not declare scope
    for its value slot.
    """

    def __init__(self, slot):
        self.slot = slot
        self._key = _slot_key(slot)


    @classmethod
    def from_payload(cls, payload, deserialize):
        if len(payload) != 1:
            raise ValueError('define expects 1 payload, got %d' % len(payload))
        return cls(payload[0])


    def match(self, source, literals, result, specs=None):
        return _capture_head(source, self._key, result, self, specs, is_symbol)


    def extract_binder_symbols(self, value):
        raw = value.source if hasattr(value, 'source') else value
        if is_symbol(raw):
            yield raw


    def to_tuple(self):
        return (_tag_define, symbol(self._key) if isinstance(self.slot, str) else self.slot)


@matcher(_tag_value)
class _ValueMatcher(Matcher):
    """
    Capture one element; value fed in, not a scope region for hygiene
    """

    def __init__(self, slot):
        self.slot = slot
        self._key = _slot_key(slot)


    @classmethod
    def from_payload(cls, payload, deserialize):
        if len(payload) != 1:
            raise ValueError('value expects 1 payload, got %d' % len(payload))
        return cls(payload[0])


    def match(self, source, literals, result, specs=None):
        return _capture_head(source, self._key, result, self, specs)


    def to_tuple(self):
        return (_tag_value, symbol(self._key) if isinstance(self.slot, str) else self.slot)


@matcher(_tag_rest)
class _RestMatcher(Matcher):
    """
    Capture remainder of list
    """

    is_scope_region_spec = True

    def __init__(self, slot):
        self.slot = slot
        self._key = _slot_key(slot)


    @classmethod
    def from_payload(cls, payload, deserialize):
        if len(payload) != 1:
            raise ValueError('rest expects 1 payload, got %d' % len(payload))
        return cls(payload[0])


    def match(self, source, literals, result, specs=None):
        if source is nil:
            return (None, False)
        pos = get_position(source) if is_pair(source) else None
        result[self._key] = MatchResult(source, pos)
        if specs is not None:
            specs[self._key] = self
        return (nil, True)


    def to_tuple(self):
        return (_tag_rest, symbol(self._key) if isinstance(self.slot, str) else self.slot)


@matcher(_tag_pair)
@matcher(_tag_bind_pair)
class _PairMatcher(Matcher):
    """
    Capture one element only when it is a pair
    """

    def __init__(self, slot):
        self.slot = slot
        self._key = _slot_key(slot)


    @classmethod
    def from_payload(cls, payload, deserialize):
        if len(payload) != 1:
            raise ValueError('pair expects 1 payload, got %d' % len(payload))
        return cls(payload[0])


    def match(self, source, literals, result, specs=None):
        return _capture_head(source, self._key, result, self, specs, is_pair)


    def to_tuple(self):
        return (_tag_pair, symbol(self._key) if isinstance(self.slot, str) else self.slot)


@matcher(_tag_expr)
class _ExprMatcher(Matcher):
    """
    Capture one element (single form); scope region for hygiene
    """

    is_scope_region_spec = True

    def __init__(self, slot):
        self.slot = slot
        self._key = _slot_key(slot)


    @classmethod
    def from_payload(cls, payload, deserialize):
        if len(payload) != 1:
            raise ValueError('expr expects 1 payload, got %d' % len(payload))
        return cls(payload[0])


    def match(self, source, literals, result, specs=None):
        return _capture_head(source, self._key, result, self, specs)


    def to_tuple(self):
        return (_tag_expr, symbol(self._key) if isinstance(self.slot, str) else self.slot)


@matcher(_tag_opt)
class _OptMatcher(Matcher):
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
        matcher = _resolve_spec(self.elem)
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


@matcher(_tag_repeat)
class _RepeatMatcher(BindingMatcher):
    """
    Match sub-matcher 1+ times (or min+), bind slot to RepeatResult with
    list of (result_dict, specs_dict) per repetition.
    """

    def __init__(self, slot, elem, min_count=1):
        self.slot = slot
        self._key = _slot_key(slot)
        self.elem = elem
        self.min_count = min_count


    @classmethod
    def from_payload(cls, payload, deserialize):
        if len(payload) < 2:
            raise ValueError('repeat expects at least 2 payload, got %d' % len(payload))
        slot = payload[0]
        elem = deserialize(payload[1])
        min_count = payload[2] if len(payload) >= 3 else 1
        return cls(slot, elem, min_count)


    def match(self, source, literals, result, specs=None):
        if source is nil:
            return (None, False)

        matcher = _resolve_spec(self.elem)
        if matcher is None:
            return (None, False)

        matches = []
        current = source

        while current is not nil:
            sub_result = {}
            sub_specs = {}
            rem, ok = _do_match(matcher, current, literals, sub_result, sub_specs)
            if not ok:
                break
            matches.append((sub_result, sub_specs))
            current = rem

        if len(matches) < self.min_count:
            return (None, False)

        elements = []
        s = source
        while s is not current and s is not nil:
            elements.append(car(s))
            s = cdr(s)
        consumed_source = cons(*elements, nil) if elements else nil

        pos = get_position(source) if is_pair(source) else None
        result[self._key] = RepeatResult(consumed_source, pos, matches)
        if specs is not None:
            specs[self._key] = self
        return (current, True)


    def extract_binder_symbols(self, value):
        for res, sp in value.matches:
            for key, spec in sp.items():
                if is_binder_spec(spec) and key in res:
                    yield from extract_binder_symbols(spec, res[key])


    def get_recurse_targets(self, value):
        for res, sp in value.matches:
            for key, spec in sp.items():
                targets = getattr(spec, 'get_recurse_targets', lambda v: None)(res.get(key))
                if targets is not None:
                    yield from targets


    def to_tuple(self):
        elem_t = (self.elem.to_tuple()
                  if hasattr(self.elem, 'to_tuple') else self.elem)
        if self.min_count != 1:
            return (_tag_repeat,
                    symbol(self._key) if isinstance(self.slot, str) else self.slot,
                    elem_t,
                    self.min_count)
        return (_tag_repeat,
                symbol(self._key) if isinstance(self.slot, str) else self.slot,
                elem_t)


@matcher(_tag_match)
class _MatchMatcher(Matcher):
    """
    Sequence of specs in order. When sub-expr and element is a pair,
    destructures (car, cdr) and matches against that.
    """

    def __init__(self, *specs, key=None):
        self.specs = specs
        self.key = key


    @classmethod
    def from_payload(cls, payload, deserialize):
        specs = [deserialize(p) for p in payload]
        return cls(*specs)


    def match(self, source, literals, result, specs=None):
        if source is not nil:
            elem, rest = source
            if (rest is nil and is_pair(elem) and not is_nil(elem)
                    and len(self.specs) >= 2):
                pair_source = cons(car(elem), cons(cdr(elem), nil))
                current = pair_source
                for spec in self.specs:
                    matcher = _resolve_spec(spec)
                    if matcher is None:
                        return (None, False)
                    current, ok = _do_match(matcher, current, literals, result, specs)
                    if not ok:
                        return (None, False)
                if self.key is not None:
                    key = _slot_key(self.key)
                    result[key] = MatchResult(elem, get_position(source))
                    if specs is not None:
                        specs[key] = self
                return (rest, True)
        current = source
        for spec in self.specs:
            matcher = _resolve_spec(spec)
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
class _AnyOrderMatcher(Matcher):
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
            matcher = _resolve_spec(spec)
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


def SYMBOL(slot):
    """
    Capture one element into slot only when it is a symbol
    """

    return _SymbolMatcher(slot)


def DEFINE(slot):
    """
    Capture one element into slot only when it is a symbol; binder for hygiene

    Single-introduced-name form like (define X Y). Does not declare scope
    for its value slot.
    """

    return _DefineMatcher(slot)


def VALUE(slot):
    """
    Capture one element into slot; value fed in, not a scope region for hygiene
    """

    return _ValueMatcher(slot)


def PAIR(slot):
    """
    Capture one element into slot only when it is a pair
    """

    return _PairMatcher(slot)


def REST(slot):
    """
    Capture remainder of list into slot; scope region for hygiene
    """

    return _RestMatcher(slot)


def REPEAT(slot, elem, min_count=1):
    """
    Match sub-matcher min_count or more times, bind slot to RepeatResult
    with list of (result_dict, specs_dict) per repetition.
    """

    return _RepeatMatcher(slot, elem, min_count)


def EXPR(slot):
    """
    Capture one element (single form); scope region for hygiene
    """

    return _ExprMatcher(slot)


def OPT(elem):
    """
    Optionally match elem
    """

    return _OptMatcher(elem)


def MATCH(*specs, key=None):
    """
    Sequence of specs in order. When sub-expr and element is a pair,
    destructures. Use key= to capture the matched pair.
    """

    return _MatchMatcher(*specs, key=key)


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
        matcher = _resolve_spec(pat)
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
    True if spec is a binder matcher (BindingMatcher subclasses)

    Used by hygiene discovery to identify captures that introduce bindings.
    """

    return spec is not None and isinstance(spec, BindingMatcher)


def is_scope_region_spec(spec):
    """
    True if spec captures a region usable as a body (REST, EXPR)

    Used by hygiene discovery for scope inference.
    """

    return spec is not None and getattr(spec, 'is_scope_region_spec', False)


def extract_binder_symbols(spec, value):
    """
    Given a binder spec and its matched value, yield symbol names introduced.

    Delegates to spec.extract_binder_symbols(value) when spec is a
    BindingMatcher.
    """

    if spec is None or value is None:
        return
    if isinstance(spec, BindingMatcher):
        yield from spec.extract_binder_symbols(value)


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
