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

from dataclasses import dataclass
from typing import Any, Dict, Optional, Protocol, Tuple, runtime_checkable

from .lib import car, cdr, cons, keyword, nil, symbol
from .types import get_position, is_pair, is_proper, is_symbol, unpack


__all__ = (
    'BindingMatcher',
    'BindingsResult',
    'BindingResult',
    'DeclResult',
    'UsingResult',
    'FormalsResult',
    'MatchResult',
    'Matcher',
    'matcher',
    'RepeatResult',

    'ANY_ORDER',
    'BIND',
    'DECL',
    'DEFINE',
    'EXPR',
    'LITERAL',
    'LITERAL_KW',
    'LITERAL_SYM',
    'LIST',
    'MATCH',
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
    'match1',

    'Syntax',
    'match_dict_to_syntax_kwargs',
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


@runtime_checkable
class Matcher(Protocol):
    """
    Protocol for matcher types. Element matchers implement matches_element and
    use default consume (take head, test identity, return tail). Structural
    matchers override consume to consume more (REST, REPEAT, LIST, MATCH).
    """

    def matches_element(
            self,
            value: Any,
            literals: frozenset,
            result: Dict[str, Any],
            specs: Optional[Dict[str, Any]] = None,
            source: Optional[Any] = None) -> bool:
        """
        Does this single value match? Mutates result on success. source is
        the pair (value . rest) when called from consume, for position.
        Optional; structural matchers omit.
        """

        ...


    def consume(
            self,
            source: Any,
            literals: frozenset,
            result: Dict[str, Any],
            specs: Optional[Dict[str, Any]] = None) -> Tuple[Optional[Any], bool]:
        """
        Consume from source list. Return (remainder, True) on success,
        (None, False) on failure. Default: take head; if matches_element(head)
        return (tail, True).
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
_tag_any_order = symbol('matcher/any-order')
_tag_list = symbol('matcher/list')
_tag_expr = symbol('matcher/expr')
_tag_define = symbol('matcher/define')
_tag_decl = symbol('matcher/decl')
_tag_value = symbol('matcher/value')
_tag_repeat = symbol('matcher/repeat')
_tag_key = symbol('matcher/key')


class MatchResult:
    """
    Base result for simple captures. Every capture includes source (raw value)
    and source_pos (position when source is a pair).
    """

    def __init__(self, source, source_pos=None):
        self.source = source
        self.source_pos = source_pos


    def box(self, position_fallback=None):
        """
        Build a Syntax value from this capture (datum and best-effort position).

        :param position_fallback: used when source_pos is unset and the datum is
            not a pair with get_position
        :return: Syntax instance
        """

        d = self.source
        pos = self.source_pos
        if pos is None and is_pair(d):
            pos = get_position(d)
        if pos is None:
            pos = position_fallback

        return Syntax(d, pos)


class FormalsResult(MatchResult):
    """
    Result for FORMALS: source, source_pos, plus positional, defaults, kwonly,
    star, starstar, annotations. Unpacking yields five values for backward compat.
    """

    def __init__(
            self,
            source,
            source_pos=None,
            positional=None,
            defaults=None,
            kwonly=None,
            star=None,
            starstar=None,
            annotations=None):
        super().__init__(source, source_pos)
        self.positional = positional if positional is not None else []
        self.defaults = defaults if defaults is not None else []
        self.kwonly = kwonly if kwonly is not None else []
        self.star = star
        self.starstar = starstar
        self.annotations = annotations if annotations is not None else {}


    def __iter__(self):
        return iter((self.positional, self.defaults, self.kwonly,
                     self.star, self.starstar))


class BindingsResult(MatchResult):
    """
    Result for BINDINGS: source, source_pos, plus names, values. Supports
    unpacking for backward compat.
    """

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

    def __init__(self, source, source_pos=None, name=None, value=None):
        super().__init__(source, source_pos)
        self.name = name
        self.value = value


class DeclResult(MatchResult):
    """
    Result for DECL: binding-shaped first argument with no value; optional
    type expression and raw source_form for debugging
    """

    def __init__(
            self,
            source,
            source_pos=None,
            name=None,
            type=None,
            source_form=None):
        super().__init__(source, source_pos)
        self.name = name
        self.type = type
        self.source_form = source_form if source_form is not None else source


class UsingResult(BindingResult):
    """
    Result for USING or USING_UNPACK: source, source_pos, name (symbol or
    None), lhs (unpack pattern cons tree or None), and value (the expression).
    USING sets lhs to None; USING_UNPACK sets name to None and lhs to the
    pattern for ((pattern...) expr).
    """

    def __init__(self, source, source_pos=None, name=None, value=None,
                 lhs=None):
        super().__init__(source, source_pos, name=name, value=value)
        self.lhs = lhs


class RepeatResult(MatchResult):
    """
    Result for REPEAT: source (consumed list as pair), source_pos, matches.
    matches: list of (result_dict, specs_dict) per repetition.
    """

    def __init__(self, source, source_pos=None, matches=None):
        super().__init__(source, source_pos)
        self.matches = matches if matches is not None else []


    def box(self, position_fallback=None):
        """
        Box each repetition into a symbol-keyed dict of Syntax values.

        :param position_fallback: passed through to nested captures
        :return: list of per-iteration dicts, or None if a sub-capture fails
        """

        rows = []
        for sub_res, _specs in self.matches:
            flat = match_dict_to_syntax_kwargs(
                sub_res, position_fallback=position_fallback)
            if flat is None:
                return None

            rows.append(flat)

        return rows


def _is_tagged_tuple(tup):
    """
    True if tup is a non-empty tuple/list with symbol as first element.
    """

    if not isinstance(tup, (tuple, list)) or not tup:
        return False

    return is_symbol(tup[0])


def _check_duplicate_slot(key, value, matcher_obj, result, specs):
    """
    Before writing to result[key], validate if key already exists from a prior
    capture. Return True if OK to write; return False to fail the match; raise
    if matcher type differs.
    """

    if key not in result:
        return True
    if specs is not None and key in specs:
        if type(matcher_obj) is not type(specs[key]):
            raise ValueError(
                "duplicate slot %r with differing matcher type: %s vs %s"
                % (key,
                   type(matcher_obj).__name__,
                   type(specs[key]).__name__))
    existing = result[key]
    if hasattr(value, 'matches'):
        if not (hasattr(existing, 'source') and hasattr(existing, 'matches')):
            return False
        if existing.source != value.source or existing.matches != value.matches:
            return False
    else:
        existing_val = existing.source if hasattr(existing, 'source') else existing
        if existing_val != value:
            return False
    return True


def _element_consume(matcher, source, literals, result, specs=None):
    """
    Default consume for element matchers: take head, if matches_element
    return tail.
    """

    if source is nil:
        return (None, False)
    head, tail = source
    if matcher.matches_element(head, literals, result, specs, source):
        return (tail, True)
    return (None, False)


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


    def matches_element(self, value, literals, result, specs=None, source=None):
        return value == self.value


    def consume(self, source, literals, result, specs=None):
        # print("LiteralMatcher source", source)
        return _element_consume(self, source, literals, result, specs)


    def to_tuple(self):
        return (_tag_literal, self.value)


@matcher(_tag_bind)
class _BindMatcher(Matcher):
    """
    Capture one element
    """

    def __init__(self, slot):
        self.slot = slot
        self._key = str(slot)


    @classmethod
    def from_payload(cls, payload, deserialize):
        if len(payload) != 1:
            raise ValueError('bind expects 1 payload, got %d' % len(payload))
        return cls(payload[0])


    def matches_element(self, value, literals, result, specs=None, source=None):
        if not _check_duplicate_slot(self._key, value, self, result, specs):
            return False
        pos = get_position(source) if source is not None and is_pair(source) else None
        result[self._key] = MatchResult(value, pos)
        if specs is not None:
            specs[self._key] = self
        return True


    def consume(self, source, literals, result, specs=None):
        return _element_consume(self, source, literals, result, specs)


    def to_tuple(self):
        return (_tag_bind, symbol(self._key) if isinstance(self.slot, str) else self.slot)


@matcher(_tag_bind_sym)
class _SymbolMatcher(BindingMatcher):
    """
    Capture one element only when it is a symbol
    """

    def __init__(self, slot):
        self.slot = slot
        self._key = str(slot)


    @classmethod
    def from_payload(cls, payload, deserialize):
        if len(payload) != 1:
            raise ValueError('bind-sym expects 1 payload, got %d' % len(payload))
        return cls(payload[0])


    def matches_element(self, value, literals, result, specs=None, source=None):
        if not is_symbol(value):
            return False
        if not _check_duplicate_slot(self._key, value, self, result, specs):
            return False
        pos = get_position(source) if source is not None and is_pair(source) else None
        result[self._key] = MatchResult(value, pos)
        if specs is not None:
            specs[self._key] = self
        return True


    def consume(self, source, literals, result, specs=None):
        # print("SymbolMatcher source", source)
        return _element_consume(self, source, literals, result, specs)


    def extract_binder_symbols(self, value):
        raw = value.source # if hasattr(value, 'source') else value
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
        self._key = str(slot)


    @classmethod
    def from_payload(cls, payload, deserialize):
        if len(payload) != 1:
            raise ValueError('define expects 1 payload, got %d' % len(payload))
        return cls(payload[0])


    def matches_element(self, value, literals, result, specs=None, source=None):
        if not is_symbol(value):
            return False
        if not _check_duplicate_slot(self._key, value, self, result, specs):
            return False
        pos = get_position(source) if source is not None and is_pair(source) else None
        result[self._key] = MatchResult(value, pos)
        if specs is not None:
            specs[self._key] = self
        return True


    def consume(self, source, literals, result, specs=None):
        return _element_consume(self, source, literals, result, specs)


    def extract_binder_symbols(self, value):
        raw = value.source if hasattr(value, 'source') else value
        if is_symbol(raw):
            yield raw


    def to_tuple(self):
        return (_tag_define, symbol(self._key) if isinstance(self.slot, str) else self.slot)


@matcher(_tag_decl)
class _DeclMatcher(BindingMatcher):
    """
    Capture one element as a declaration (binding-shaped, no value)
    """

    def __init__(self, slot):
        self.slot = slot
        self._key = str(slot)


    @classmethod
    def from_payload(cls, payload, deserialize):
        if len(payload) != 1:
            raise ValueError('decl expects 1 payload, got %d' % len(payload))
        return cls(payload[0])


    def consume(self, source, literals, result, specs=None):
        if source is nil:
            return (None, False)
        head, tail = source
        from koskari.compile.helpers import parse_decl
        from koskari.exceptions import SpecialError
        try:
            name_sym, type_expr = parse_decl(head, source=head)
        except SpecialError:
            return (None, False)
        pos = get_position(source) if is_pair(source) else None
        decl_res = DeclResult(
            head, pos, name=name_sym, type=type_expr, source_form=head)
        if not _check_duplicate_slot(self._key, decl_res, self, result, specs):
            return (None, False)
        result[self._key] = decl_res
        if specs is not None:
            specs[self._key] = self
        return (tail, True)


    def extract_binder_symbols(self, value):
        if isinstance(value, DeclResult):
            yield value.name


    def to_tuple(self):
        return (_tag_decl, symbol(self._key) if isinstance(self.slot, str) else self.slot)


@matcher(_tag_value)
class _ValueMatcher(Matcher):
    """
    Capture one element; value fed in, not a scope region for hygiene
    """

    def __init__(self, slot):
        self.slot = slot
        self._key = str(slot)


    @classmethod
    def from_payload(cls, payload, deserialize):
        if len(payload) != 1:
            raise ValueError('value expects 1 payload, got %d' % len(payload))
        return cls(payload[0])


    def matches_element(self, value, literals, result, specs=None, source=None):
        if not _check_duplicate_slot(self._key, value, self, result, specs):
            return False
        pos = get_position(source) if source is not None and is_pair(source) else None
        result[self._key] = MatchResult(value, pos)
        return True


    def consume(self, source, literals, result, specs=None):
        # print("ValueMatcher source", source)
        return _element_consume(self, source, literals, result, specs)


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
        self._key = str(slot)


    @classmethod
    def from_payload(cls, payload, deserialize):
        if len(payload) != 1:
            raise ValueError('rest expects 1 payload, got %d' % len(payload))
        return cls(payload[0])


    def matches_element(self, value, literals, result, specs=None, source=None):
        return False


    def consume(self, source, literals, result, specs=None):
        if source is nil:
            return (None, False)
        pos = get_position(source) if is_pair(source) else None
        if not _check_duplicate_slot(self._key, source, self, result, specs):
            return (None, False)
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
        self._key = str(slot)


    @classmethod
    def from_payload(cls, payload, deserialize):
        if len(payload) != 1:
            raise ValueError('pair expects 1 payload, got %d' % len(payload))
        return cls(payload[0])


    def matches_element(self, value, literals, result, specs=None, source=None):
        if not is_pair(value):
            return False
        if not _check_duplicate_slot(self._key, value, self, result, specs):
            return False
        pos = get_position(source) if source is not None and is_pair(source) else None
        result[self._key] = MatchResult(value, pos)
        if specs is not None:
            specs[self._key] = self
        return True


    def consume(self, source, literals, result, specs=None):
        return _element_consume(self, source, literals, result, specs)


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
        self._key = str(slot)


    @classmethod
    def from_payload(cls, payload, deserialize):
        if len(payload) != 1:
            raise ValueError('expr expects 1 payload, got %d' % len(payload))
        return cls(payload[0])


    def matches_element(self, value, literals, result, specs=None, source=None):
        if not _check_duplicate_slot(self._key, value, self, result, specs):
            return False
        pos = get_position(source) if source is not None and is_pair(source) else None
        result[self._key] = MatchResult(value, pos)
        if specs is not None:
            specs[self._key] = self
        return True


    def consume(self, source, literals, result, specs=None):
        return _element_consume(self, source, literals, result, specs)


    def to_tuple(self):
        return (_tag_expr, symbol(self._key) if isinstance(self.slot, str) else self.slot)


@matcher(_tag_repeat)
class _RepeatMatcher(BindingMatcher):
    """
    Match sub-matcher min_count or more times, bind slot to RepeatResult with
    list of (result_dict, specs_dict) per repetition. When min_count is zero,
    an empty source list succeeds with zero matches.
    """

    def __init__(self, slot, elem, min_count=1):
        self.slot = slot
        self._key = str(slot)
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


    def matches_element(self, value, literals, result, specs=None, source=None):
        return False


    def consume(self, source, literals, result, specs=None):
        # print("RepeatMatcher source", source)
        if source is nil:
            if self.min_count != 0:
                return (None, False)

            repeat_result = RepeatResult(nil, None, [])
            if not _check_duplicate_slot(
                    self._key, repeat_result, self, result, specs):
                return (None, False)

            result[self._key] = repeat_result
            if specs is not None:
                specs[self._key] = self

            return (nil, True)

        matches = []
        current = source
        iter_num = 0

        while current is not nil:
            sub_result = {}
            sub_specs = {}
            iter_num += 1
            # print("RepeatMatcher iter %d current" % iter_num, current)
            rem, ok = self.elem.consume(current, literals, sub_result, sub_specs)
            # print("RepeatMatcher iter %d elem.consume rem=" % iter_num, rem, "ok=", ok)
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
        repeat_result = RepeatResult(consumed_source, pos, matches)
        if not _check_duplicate_slot(
                self._key, repeat_result, self, result, specs):
            return (None, False)
        result[self._key] = repeat_result
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
        elem_t = self.elem.to_tuple()
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
    Top-level only. Receives source list and applies specs element-by-element.
    Requires full consumption.
    """

    def __init__(self, *specs, key=None):
        self.specs = specs
        self.key = key


    @classmethod
    def from_payload(cls, payload, deserialize):
        key = None
        if payload \
           and isinstance(payload[-1], (tuple, list)) \
           and len(payload[-1]) == 2 and payload[-1][0] == _tag_key:

            key = payload[-1][1]
            payload = payload[:-1]
        specs = [deserialize(p) for p in payload]
        return cls(*specs, key=key)


    def matches_element(self, value, literals, result, specs=None, source=None):
        return False


    def consume(self, source, literals, result, specs=None):
        # print("MatchMatcher source", source)
        current = source
        for spec in self.specs:
            current, ok = spec.consume(current, literals, result, specs)
            # print(f"  {spec.__class__.__name__} ok:", ok)
            if not ok:
                return (None, False)
        if current is not nil:
            return (None, False)
        if self.key is not None:
            result[self.key] = MatchResult(source, get_position(source))
        return (current, True)


    def to_tuple(self):
        parts = [s.to_tuple() for s in self.specs]
        if self.key is not None:
            parts.append((_tag_key, self.key))
        return (_tag_match,) + tuple(parts)


@matcher(_tag_list)
class _ListMatcher(Matcher):
    """
    Takes HEAD of source, applies MATCH with same specs to it. The element
    at this position must be a list that fully matches the specs.
    """

    def __init__(self, *specs, key=None):
        self.key = key
        self.matcher = MATCH(*specs, key=key)


    @classmethod
    def from_payload(cls, payload, deserialize):
        key = None
        if payload and isinstance(payload[-1], (tuple, list)) \
           and len(payload[-1]) == 2 and payload[-1][0] == _tag_key:

            key = payload[-1][1]
            payload = payload[:-1]
        specs = [deserialize(p) for p in payload]
        return cls(*specs, key=key)


    def matches_element(self, value, literals, result, specs=None, source=None):
        return False


    def consume(self, source, literals, result, specs=None):
        # print("ListMatcher source", source)
        if source is nil:
            return (None, False)
        head, rest = source
        if not is_pair(head):
            return (None, False)
        rem, ok = self.matcher.consume(head, literals, result, specs)
        if not ok or rem is not nil:
            return (None, False)
        if self.key is not None:
            result[self.key] = MatchResult(head, get_position(source))
        return (rest, True)


    def to_tuple(self):
        parts = [s.to_tuple() for s in self.matcher.specs]
        if self.key is not None:
            parts.append((_tag_key, self.key))
        return (_tag_list,) + tuple(parts)


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


    def matches_element(self, value, literals, result, specs=None, source=None):
        return False


    def consume(self, source, literals, result, specs=None):
        # TODO: add allow_improper option
        if source is not nil and is_pair(source) and not is_proper(source):
            raise ValueError('ANY_ORDER requires proper list')
        elements = list(unpack(source)) if source is not nil else []
        pool = list(elements)
        for spec in self.specs:
            matched = False
            for i, elem in enumerate(pool):
                elem_pair = cons(elem, nil)
                rem, ok = spec.consume(elem_pair, literals, result, specs)
                if ok and rem is nil:
                    del pool[i]
                    matched = True
                    break
            if not matched:
                return (None, False)
        return (nil, True)


    def to_tuple(self):
        parts = tuple(s.to_tuple() for s in self.specs)
        return (_tag_any_order,) + parts


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


def DECL(slot):
    """
    Capture one element as a declaration (symbol or typed binding list, no
    value)
    """

    return _DeclMatcher(slot)


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
    with list of (result_dict, specs_dict) per repetition. With min_count 0,
    an empty remaining list matches zero repetitions.
    """

    return _RepeatMatcher(slot, elem, min_count)


def EXPR(slot):
    """
    Capture one element (single form); scope region for hygiene
    """

    return _ExprMatcher(slot)


def LIST(*specs, key=None):
    """
    Takes head of source, applies MATCH with same specs to it. The element
    at this position must be a list that fully matches the specs.
    """

    return _ListMatcher(*specs, key=key)


def MATCH(*specs, key=None):
    """
    Top-level only. Receives source list and applies specs element-by-element.
    Requires full consumption. Use key= to capture the matched list.
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

    :param source: must be nil or a pair (no implicit conversion)
    :param patterns: single matcher, tagged tuple, or sequence of patterns
    :param literals: set or frozenset of symbols to match literally; default
        empty frozenset
    :param with_specs: if True, return (result_dict, specs_dict) on match
    :return: dict mapping slot names to values; or (dict, dict) if with_specs
        True; or None on no match
    """

    # print("match source", source)

    lit = frozenset(literals) if literals is not None else frozenset()
    src = source
    if not is_pair(src):
        raise TypeError(
            'match source must be nil or a pair, got %s' % type(src).__name__)

    if not isinstance(patterns, (list, tuple)):
        patterns = [patterns]

    for pat in patterns:
        result = {}
        specs = {}
        # print("match pat", pat)
        rem, ok = pat.consume(src, lit, result, specs)
        if ok:
            if with_specs:
                return (result, specs)
            return result

    return None


def match1(
        value,
        patterns,
        literals=None,
        with_specs=False):
    """
    Does this single element match? Wraps value in cons(val, nil) and defers
    to match.

    :param value: single value (atom or pair)
    :param patterns: single matcher or sequence of patterns
    :param literals: set or frozenset of symbols to match literally
    :param with_specs: if True, return (result_dict, specs_dict) on match
    :return: dict of bindings; or (dict, dict) if with_specs; or None
    """

    return match(cons(value, nil), patterns, literals=literals,
                 with_specs=with_specs)


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


@dataclass(frozen=True)
class Syntax:
    """
    Wraps a Koskari datum with optional source position and optional tag.

    :param datum: underlying form (pair, symbol, nil, literals)
    :param position: source position tuple or None when unknown
    :param tag: optional interpretation hint; reserved for future use
    """

    datum: Any
    position: Any = None
    tag: Any = None


def match_dict_to_syntax_kwargs(raw, position_fallback=None):
    """
    Map match() result dict to kwargs suitable for macro-rules: str keys to
    Syntax or list of boxed values (ellipsis). Symbol keys in output for
    Koskari call convention via demand_apply.
    """

    if not raw:
        return {}

    out = {}
    for k, v in raw.items():
        boxed = v.box(position_fallback)
        if boxed is None:
            return None

        out[symbol(k)] = boxed

    return out


# The end.
