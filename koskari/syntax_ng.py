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
koskari.syntax_ng

Next-generation syntax dispatch scaffolding: ``Syntax`` (a ``Compiled``
dispatcher), transform arms, native ``koskari.lib.Datum`` for macro unbox,
and a
syntax-rules-compatible pattern lowerer to ``matcher_ng.Pattern`` plus
MATCH-dialect rule spines via :func:`koskari.matcher_ng.parse_explicit_rule_pattern`.

The compiled dispatcher type here is ``syntax_ng.Syntax``. The legacy
source wrapper remains ``koskari.syntax.Syntax``.

Bare-pattern ellipsis ``(id ...)`` lowers to ``REST(str(id))`` in the
RULE pattern lowerer: ``captures[id]`` is the tail-shaped value (often
``Datum``), not a Python list of repeat frames. Template expansion treats
a lone pattern-variable followed by ``...`` like quasiquote ``,@`` when
that binding is not a ``list``; see
:func:`koskari.hygiene_ng.expand_template_with_hygiene`. Compound
``(subpat ...)`` still uses ``repeat_capture`` until repeat-frame
transpose exists. An empty ellipsis region matches a subject with zero
spine elements (``()``), not ``(nil,)``.

Runtime constructors ``build_syntax`` and ``build_macro_syntax`` /
``build_special_syntax`` assemble a ``Syntax`` dispatcher from RULE
patterns. ``build_syntax_match``, ``build_macro_match``, and
``build_special_match`` use MATCH-dialect patterns (explicit matcher
spine). Templates and procedural handlers see the full use-site form
(nil-terminated pair spine) unchanged. For RULE builders, pattern
literals are only what the caller passes: the first pattern position is
normally a symbol capture (the actual head, including aliases); include
the dispatch symbol in ``literals`` when a fixed head is required. Ki
``syntax-match`` segments use only the macro name as a literal set at
the compile layer; here MATCH patterns encode structure explicitly.
These are not registered in bootstrap until the NG cutover.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Composer via Cursor
"""


from typing import Any, Protocol, runtime_checkable

from koskari.compile.compiler import Compiled
from koskari.exceptions import SpecialError
from koskari.hygiene_ng import (Hygiene, apply_syntax_rules_hygiene,
                                collect_binding_symbols,
                                expand_template_with_hygiene)
from koskari.lib import (Datum, Matcher, car, cdr, cons, demand_call, gensym,
                         keyword, nil, pair, symbol)
from koskari.matcher_ng import (BIND, CAPTURE, FORM, LITERAL, MATCH, REST,
                                HygienicMatchCompiler, check_pattern_arg,
                                parse_explicit_rule_pattern, repeat_capture)
from koskari.types import (build_proper, is_nil, is_pair, is_proper, is_symbol,
                           unpack)


__all__ = (
    "Datum",
    "MacroTransform",
    "SpecialTransform",
    "Syntax",
    "SyntaxTransform",
    "Transform",
    "build_macro_match",
    "build_macro_rules",
    "build_macro_syntax",
    "build_special_match",
    "build_special_match_from_rows",
    "build_special_rules",
    "build_special_syntax",
    "build_syntax_from_datums",
    "build_syntax_match",
    "build_syntax_rules",
    "deserialize_form",
    "hygienic_lower_tuple",
    "parse_syntax_rules_pattern",
    "serialize_form",
    "special",
)


_symbol_match = symbol("match")

_tag_syntax_rules = symbol("syntax/rules")
_tag_list = symbol("syntax/list")
_tag_cons = symbol("syntax/cons")


def serialize_form(form):
    """
    Encode a Koskari form as tagged tuples for const-pool persistence

    Proper lists use ``syntax/list``; improper pairs use ``syntax/cons``;
    tuples and frozensets recurse structurally; atoms pass through
    """

    if form is nil:
        return nil

    if is_pair(form):
        if is_proper(form):
            return (_tag_list,) + tuple(serialize_form(x) for x in unpack(form))
        return (_tag_cons, serialize_form(car(form)), serialize_form(cdr(form)))

    if isinstance(form, tuple):
        return tuple(serialize_form(x) for x in form)

    if isinstance(form, frozenset):
        return frozenset(serialize_form(x) for x in form)

    return form


def deserialize_form(payload):
    """
    Decode :func:`serialize_form` output back to Koskari pairs and datums
    """

    if payload is nil:
        return nil

    if isinstance(payload, tuple) and payload:
        tag = payload[0]

        if tag is _tag_list:
            return build_proper(*(deserialize_form(x) for x in payload[1:]))

        if tag is _tag_cons:
            if len(payload) != 3:
                raise ValueError("invalid cons payload")
            return cons(
                deserialize_form(payload[1]),
                deserialize_form(payload[2]),
            )

        return tuple(deserialize_form(x) for x in payload)

    if isinstance(payload, frozenset):
        return frozenset(deserialize_form(x) for x in payload)

    return payload


def hygienic_lower_tuple(pattern):
    """
    Lower a matcher_ng pattern with :class:`HygienicMatchCompiler` for
    persistence (same tables ``SyntaxTransform`` uses when built from a
    pattern tree)
    """

    check_pattern_arg(pattern, "hygienic_lower_tuple pattern")
    hc = HygienicMatchCompiler()
    return hc.lower(pattern)


def _init_pattern_fields(instance, pattern, handler):
    """
    Attach pattern, frozen matcher tuple, hygiene plan, and thawed matcher
    """

    check_pattern_arg(pattern, "Transform pattern")
    if handler is not None and not callable(handler):
        raise TypeError(
            "Transform handler must be callable or None, got %r"
            % (type(handler).__name__,))
    instance.pattern = pattern
    instance.handler = handler
    hc = HygienicMatchCompiler()
    instance.matcher_tuple = hc.lower(pattern)
    instance.hygiene = hc.build_hygiene()
    instance._matcher = Matcher.from_tuple(instance.matcher_tuple)


def _engine_externs():
    """
    Extern values for native ``Matcher.match`` on compile-time forms
    """

    return dict(
        symbol=symbol,
        keyword=keyword,
        pair=pair,
        nil=nil,
        cons=cons,
    )


def _run_matcher(matcher, matcher_tuple, subject):
    """
    Run ``matcher`` against ``subject`` with externs required by ``matcher_tuple``
    """

    if (not isinstance(matcher_tuple, tuple)
            or len(matcher_tuple) < 3
            or matcher_tuple[0] != "MATCHER"):
        return matcher.match(subject)
    enames = matcher_tuple[2]
    if not enames:
        return matcher.match(subject)
    full = _engine_externs()
    ext = {k: full[k] for k in enames if k in full}
    if not ext:
        return matcher.match(subject)
    return matcher.match(subject, **ext)


def _unwrap_syntax_binding_value(v):
    """
    Strip ``Datum`` wrappers from native matcher capture values so template
    substitution builds plain pair spines (repeat frames are dicts whose
    values may also be ``Datum``-wrapped)
    """

    if isinstance(v, Datum):
        return _unwrap_syntax_binding_value(v.datum)
    if isinstance(v, dict):
        if len(v) == 1:
            k, vv = next(iter(v.items()))
            if isinstance(k, str) and k.endswith("__sr"):
                return _unwrap_syntax_binding_value(vv)
        return {kk: _unwrap_syntax_binding_value(vv) for kk, vv in v.items()}
    if isinstance(v, list):
        return [_unwrap_syntax_binding_value(x) for x in v]
    return v


def _captures_to_bindings(captures):
    """
    Map string capture keys to koskari symbols for ``apply_syntax_rules_hygiene``
    """

    out = {}
    for k, v in captures.items():
        out[symbol(k)] = _unwrap_syntax_binding_value(v)
    return out


def _unbox_datum_in_macro_output(expanded):
    """
    Strip native ``Datum`` wrappers from procedural macro output (mirrors
    legacy ``unbox_macro`` for ``koskari.syntax.Syntax``)
    """

    seen = set()

    def walk(x):
        if isinstance(x, Datum):
            return walk(x.datum)

        if is_nil(x):
            return x

        if is_pair(x):
            nx = x
            while nx is not nil:
                xi = id(nx)
                if xi in seen:
                    nx = nx.cdr
                    continue
                seen.add(xi)

                head, tail = nx
                if isinstance(head, Datum):
                    nx.car = walk(head.datum)
                    nx.position = head.position
                elif is_nil(head):
                    pass
                elif is_pair(head):
                    walk(head)
                nx = tail
            return x
        return x

    return walk(expanded)


@runtime_checkable
class Transform(Protocol):
    """
    One clause of a ``Syntax`` dispatcher: frozen ``matcher_ng`` pattern,
    hygiene plan, and matcher-backed ``try_expand``
    """

    pattern: Any
    matcher_tuple: tuple
    hygiene: Any
    handler: Any
    _matcher: Matcher


    def try_expand(self, compiler, source_obj, tc):
        """
        Try this arm on ``source_obj``; return compile result or ``None``

        :param compiler: active compiler instance
        :param source_obj: full special form as a proper list (pair spine)
        :param tc: tail-call flag
        :return: fragment or form for ``compile_special``, or ``None`` when the
            pattern does not match
        """

        ...


class SyntaxTransform(object):
    """
    Template arm: match full use-site form, then ``apply_syntax_rules_hygiene``
    """

    def __init__(self, pattern, template=nil):
        """
        :param pattern: matcher_ng pattern tree
        :param template: syntax-rules template form (often contains pattern vars)
        """

        _init_pattern_fields(self, pattern, None)
        self.template = template


    @classmethod
    def from_persistent(cls, matcher_tuple, template):
        """
        Rebuild from a frozen native matcher tuple and deserialized template
        """

        self = cls.__new__(cls)
        self.pattern = None
        self.template = template
        self.handler = None
        self.matcher_tuple = matcher_tuple
        self.hygiene = Hygiene({}, {})
        self._matcher = Matcher.from_tuple(matcher_tuple)
        return self


    def try_expand(self, compiler, source_obj, tc):
        """
        Match ``source_obj`` as the full proper-list form; on success expand
        ``template`` with captures

        :param compiler: active compiler
        :param source_obj: full special form (proper list spine)
        :param tc: tail-call flag (unused for template expansion)
        :return: expanded form or ``None`` when the pattern does not match
        """

        result = _run_matcher(
            self._matcher, self.matcher_tuple, source_obj)
        if not result.matched:
            return None
        bindings = _captures_to_bindings(result.captures)
        return apply_syntax_rules_hygiene(
            self.template, bindings, compiler.find_compiled)


class MacroTransform(object):
    """
    Procedural arm: invoke handler then ``hygiene_ng`` on expanded output
    """

    def __init__(self, pattern, handler):
        """
        :param pattern: matcher_ng pattern tree
        :param handler:
            ``(compiler, tc, **captures)`` procedure returning expanded form;
            ``captures`` are string-keyed match slots from ``match_result.captures``
        """

        if handler is None or not callable(handler):
            raise TypeError("MacroTransform handler must be callable,"
                            f" got {handler!r}")
        _init_pattern_fields(self, pattern, handler)


    @classmethod
    def from_persistent(cls, matcher_tuple, handler):
        """
        Rebuild from a frozen native matcher tuple and procedure.

        Ki procedures are wrapped so ``try_expand`` still passes
        ``(compiler, tc, **captures)`` while the underlying lambda receives
        only ``**captures`` via :func:`koskari.lib.demand_call`, matching
        compile-emitted ``compile/lambda`` formals.
        """

        if handler is None or not callable(handler):
            raise TypeError("MacroTransform handler must be callable,"
                            f" got {handler!r}")
        self = cls.__new__(cls)
        self.pattern = None
        raw = handler

        def _ki_captures_kw(cap):
            kw = {}
            for k, v in cap.items():
                v2 = _unwrap_syntax_binding_value(v)
                if isinstance(k, str) and k.endswith("__sr"):
                    base = k[:-4]
                    if base not in cap:
                        kw[base] = v2
                else:
                    kw[k] = v2
            return kw

        def _ki_handler(compiler, tc, **captures):
            return demand_call(raw, **_ki_captures_kw(captures))

        self.handler = _ki_handler
        self.matcher_tuple = matcher_tuple
        self.hygiene = Hygiene({}, {})
        self._matcher = Matcher.from_tuple(matcher_tuple)
        return self


    def try_expand(self, compiler, source_obj, tc):
        """
        Match ``source_obj`` as the full proper-list form, run handler, then
        procedural macro hygiene on the result

        :param compiler: active compiler
        :param source_obj: full special form (proper list spine)
        :param tc: tail-call flag (passed to the handler as ``tc``)
        :return: hygiened expanded form or ``None`` when the pattern does not
            match
        """

        result = _run_matcher(
            self._matcher, self.matcher_tuple, source_obj)
        if not result.matched:
            return None
        expanded = self.handler(compiler, tc, **result.captures)
        syms = collect_binding_symbols(expanded, compiler.find_compiled)
        hygiened = expand_template_with_hygiene(expanded, {}, syms, {})
        return _unbox_datum_in_macro_output(hygiened)


class SpecialTransform(object):
    """
    Special arm: handler return is the compile result (no automatic hygiene)
    """

    def __init__(self, pattern, handler):
        """
        :param pattern: matcher_ng pattern tree
        :param handler:
            ``(compiler, source_obj, match_result, tc)`` procedure; use
            :func:`special` if you need other calling conventions
        """

        if handler is None or not callable(handler):
            raise TypeError("SpecialTransform handler must be callable,"
                            f" got {handler!r}")
        _init_pattern_fields(self, pattern, handler)


    @classmethod
    def from_persistent(cls, matcher_tuple, handler):
        """
        Rebuild from a frozen native matcher tuple and procedure.

        Ki procedures are wrapped so ``try_expand`` still passes
        ``(compiler, source_obj, match_result, tc)`` while the underlying
        lambda receives ``(compiler, tc, **captures)`` via
        :func:`koskari.lib.demand_call`, matching compile-emitted formals.
        """

        if handler is None or not callable(handler):
            raise TypeError("SpecialTransform handler must be callable,"
                            f" got {handler!r}")
        self = cls.__new__(cls)
        self.pattern = None
        raw = handler

        def _ki_captures_kw_special(cap):
            kw = {}
            for k, v in cap.items():
                if isinstance(k, str) and k.endswith("__sr"):
                    base = k[:-4]
                    if base not in cap:
                        kw[base] = v
                else:
                    kw[k] = v
            return kw

        def _ki_handler(compiler, source_obj, match_result, tc):
            out = demand_call(
                raw, compiler, tc,
                **_ki_captures_kw_special(match_result.captures))
            return _unbox_datum_in_macro_output(out)

        self.handler = _ki_handler
        self.matcher_tuple = matcher_tuple
        self.hygiene = Hygiene({}, {})
        self._matcher = Matcher.from_tuple(matcher_tuple)
        return self


    def try_expand(self, compiler, source_obj, tc):
        """
        Match ``source_obj`` as the full proper-list form; on success return
        handler result unchanged

        :param compiler: active compiler
        :param source_obj: full special form (proper list spine)
        :param tc: tail-call flag
        :return: compile product or ``None`` when the pattern does not match
        """

        result = _run_matcher(
            self._matcher, self.matcher_tuple, source_obj)
        if not result.matched:
            return None
        return self.handler(compiler, source_obj, result, tc)


class Syntax(Compiled):
    """
    Ordered dispatcher over ``Transform`` clauses (compiled form). Try order
    follows ``transforms`` tuple order; use ``__add__`` to prepend clauses

    :param name: primary compile-time symbol for this form
    :param aliases: additional symbols resolving to this dispatcher
    :param transforms: ordered transforms tried on each expansion
    """

    def __init__(self, name, aliases=(), transforms=(), *,
                 serialized_literals=None):
        """
        :param name: symbol or str naming this syntax object
        :param aliases: iterable of extra names
        :param transforms: iterable of ``Transform`` instances
        :param serialized_literals: optional frozenset mirrored into
            ``to_syntax_rules_tagged`` for Ki const-pool layout
        """

        self.name = name
        self.aliases = tuple(aliases)
        self.transforms = tuple(transforms)
        self.serialized_literals = serialized_literals
        for t in self.transforms:
            if not isinstance(t, Transform):
                raise TypeError("Syntax transforms must be Transform instances,"
                                f" got {t!r}")


    @property
    def hygiene(self):
        """
        Merged binder and scope-region plan from all transform arms for
        :func:`koskari.hygiene_ng.collect_binding_symbols`
        """

        binders = {}
        scopes = {}
        for t in self.transforms:
            h = t.hygiene
            if h.is_empty():
                continue
            binders.update(dict(h.binders))
            scopes.update(dict(h.scope_regions))
        return Hygiene(binders, scopes)


    def match_for_hygiene(self, rest):
        """
        Match ``(self.name . rest)`` against the first arm whose frozen
        matcher succeeds; return capture mapping for hygiene discovery
        """

        head = self.name
        if not is_symbol(head):
            head = symbol(str(head))
        full = cons(head, rest)
        for t in self.transforms:
            try:
                r = _run_matcher(t._matcher, t.matcher_tuple, full)
            except SpecialError:
                continue
            if r.matched:
                return r.captures
        return None


    def compile_impl(self, compiler, source_obj, tc):
        """
        Try each transform in order; first non-``None`` result wins

        :param compiler: active compiler instance
        :param source_obj: full special form being compiled
        :param tc: tail-call flag
        :raises SpecialError: when no arm matches
        :return: compile product from the winning transform
        """

        for t in self.transforms:
            out = t.try_expand(compiler, source_obj, tc)
            if out is not None:
                return out
        raise SpecialError("no matching syntax rule", source_obj)


    def to_syntax_rules_tagged(self):
        """
        Tagged tuple for ``(build-syntax-rules NAME TAGGED)`` const-pool
        layout (patterns as native frozen matcher tuples)
        """

        lit = self.serialized_literals
        if lit is None:
            lit = frozenset()
        rules_out = []
        for t in self.transforms:
            if not isinstance(t, SyntaxTransform):
                raise TypeError(
                    "to_syntax_rules_tagged requires SyntaxTransform arms,"
                    " got %r" % (type(t).__name__,))
            rules_out.append(
                (t.matcher_tuple, serialize_form(t.template)))
        return (
            _tag_syntax_rules,
            serialize_form(self.name),
            tuple(serialize_form(x) for x in sorted(lit, key=str)),
            tuple(rules_out),
        )


    def __add__(self, other):
        """
        Return a new ``Syntax`` with ``other``'s transforms prepended before
        ``self``'s (``other`` is tried first at expansion time)

        :param other: ``Transform`` or ``Syntax``
        :rtype: Syntax
        """

        if isinstance(other, Transform):
            new_transforms = (other,) + self.transforms
        elif isinstance(other, Syntax):
            new_transforms = other.transforms + self.transforms
        else:
            return NotImplemented
        return Syntax(
            self.name, self.aliases, new_transforms,
            serialized_literals=self.serialized_literals)


def _normalize_special_patterns(patterns):
    """
    Accept one ``Pattern`` or an iterable of patterns for ordered disjunction
    """

    if isinstance(patterns, (list, tuple)):
        out = tuple(patterns)
    else:
        out = (patterns,)
    for i, p in enumerate(out):
        check_pattern_arg(p, "special patterns[%s]" % i)
    return out


def special(form_symbol, *, patterns, unpack=None):
    """
    Build a ``Syntax`` dispatcher whose arms are ``SpecialTransform`` instances
    wrapping the decorated function. ``unpack`` selects one of three fixed
    invoke shapes at decoration time (no per-call unpack dispatch)

    :param form_symbol: primary compile-time head symbol for this form
    :param patterns: one ``matcher_ng.Pattern`` or an iterable (try-order)
    :param unpack: only for this decorator: ``None`` (receive
        ``compiler, source_obj, tc``), ``match`` (receive
        ``compiler, captures_dict, tc``), or ``kwargs`` (captures merged after
        ``tc``). For ``match`` and ``kwargs``, ``captures_dict`` includes only
        slots the matcher bound; values are matcher outputs (``Datum``
        wrappers may be present). Not available on ``build_*`` rule rows.
    """

    if unpack not in (None, "match", "kwargs"):
        raise ValueError(
            f"unpack must be None, 'match', or 'kwargs', got {unpack!r}")

    def deco(fn):
        """
        :param fn: Python handler implementing this special form
        :rtype: Syntax
        """

        if not callable(fn):
            raise TypeError("special decorator requires a callable,"
                            f" got {fn!r}")

        plist = _normalize_special_patterns(patterns)
        if unpack is None:
            def wrapped(compiler, source_obj, match_result, tc):
                return fn(compiler, source_obj, tc)

        elif unpack == "match":
            def wrapped(compiler, source_obj, match_result, tc):
                return fn(compiler, match_result.captures, tc)

        elif unpack == "kwargs":
            def wrapped(compiler, source_obj, match_result, tc):
                return fn(compiler, tc=tc, **match_result.captures)

        else:
            raise ValueError(f"internal: bad unpack {unpack!r}")

        transforms = tuple(SpecialTransform(p, wrapped) for p in plist)
        return Syntax(form_symbol, (), transforms)

    return deco


def parse_syntax_rules_pattern(pattern_expr, literals, source_ctx):
    """
    Lower a syntax-rules RULE pattern (cons tree) to a ``matcher_ng.Pattern``.

    Supports the same surface as ``parse_rule_pattern`` /
    ``syntax_pattern_to_matcher`` in ``compile/specials/syntax.py`` (RULE
    dialect only). Does not implement MATCH-dialect ``matcher/foo`` forms.

    :param pattern_expr: koskari pattern datum
    :param literals: set or frozenset of literal symbols
    :param source_ctx: value recorded in error messages
    :rtype: Pattern
    """

    if not isinstance(literals, (set, frozenset)):
        literals = frozenset(literals)
    return _parse_rule_pattern(pattern_expr, literals, source_ctx)


def _parse_rule_pattern(pattern_expr, literals, source_ctx):
    if is_pair(pattern_expr):
        head, rest = pattern_expr
        if head == _symbol_match:
            if (not is_proper(rest)
                    or rest.cdr is not nil
                    or rest is nil):
                raise ValueError(
                    f"match pattern must have exactly one form: {pattern_expr!r}"
                    f" (context {source_ctx!r})"
                )
            return _syntax_pattern_to_pattern(rest.car, literals, source_ctx)

    return _syntax_pattern_to_pattern(pattern_expr, literals, source_ctx)


def _syntax_pattern_to_pattern(pattern_expr, literals, source_ctx):
    if pattern_expr is Ellipsis:
        raise TypeError(
            f"invalid ellipsis placement in pattern: {pattern_expr!r}"
            f" (context {source_ctx!r})"
        )

    if is_symbol(pattern_expr):
        if pattern_expr in literals:
            return MATCH(LITERAL(pattern_expr))
        return MATCH(REST(str(pattern_expr)))

    if not is_pair(pattern_expr):
        return MATCH(LITERAL(pattern_expr))

    return MATCH(*_syntax_pattern_specs(
        list(unpack(pattern_expr)), literals, source_ctx))


def _syntax_elem_pattern(pat, literals, source_ctx):
    if pat is Ellipsis:
        raise ValueError(
            f"invalid ellipsis placement in pattern: {pat!r}"
            f" (context {source_ctx!r})"
        )

    if is_symbol(pat):
        if pat in literals:
            return LITERAL(pat)
        return BIND(str(pat))

    if not is_pair(pat):
        return LITERAL(pat)

    return FORM(MATCH(*_syntax_pattern_specs(
        list(unpack(pat)), literals, source_ctx)))


def _syntax_pattern_specs(p_items, literals, source_ctx):
    specs = []
    i = 0
    while i < len(p_items):
        if i + 1 < len(p_items) and p_items[i + 1] is Ellipsis:
            pat = p_items[i]
            if is_symbol(pat) and pat not in literals:
                specs.append(REST(str(pat)))
            else:
                slot = str(gensym("_repeat"))
                inner = _syntax_elem_pattern(pat, literals, source_ctx)
                specs.append(repeat_capture(slot, inner))
            i += 2
            continue

        specs.append(_syntax_elem_pattern(p_items[i], literals, source_ctx))
        i += 1

    return specs


def _pattern_literals(literals):
    """
    Frozenset of pattern literals supplied by the caller (no auto-merge)
    """

    return frozenset(literals) if literals is not None else frozenset()


def _normalize_syntax_rule_row(row):
    """
    Split a syntax-rules-style row into pattern and template datums
    """

    if not isinstance(row, (list, tuple)):
        raise TypeError(
            f"syntax-rules row must be a sequence, got {row!r}"
        )
    seq = tuple(row)
    if len(seq) != 2:
        raise ValueError(
            f"syntax-rules row must be (pattern, template), got length {len(seq)}"
        )
    return seq[0], seq[1]


def _normalize_proc_rule_row(row):
    """
    Split a macro-rules / special-rules row into pattern and procedure
    """

    if not isinstance(row, (list, tuple)):
        raise TypeError(
            f"macro/special rule row must be a sequence, got {row!r}"
        )
    seq = tuple(row)
    if len(seq) != 2:
        raise ValueError(
            f"macro/special rule row must be (pattern, procedure), "
            f"got length {len(seq)}"
        )
    pat_d, proc = seq
    if proc is None or not callable(proc):
        raise TypeError(
            f"macro/special rule procedure must be callable, got {proc!r}"
        )
    return pat_d, proc


def build_syntax_from_datums(name, literals, rules, *, source_ctx=None):
    """
    Build a ``Syntax`` dispatcher of ``SyntaxTransform`` arms from RULE
    pattern datums and templates.

    Patterns describe the full use-site form (nil-terminated list spine)
    passed to ``compile_impl``. The first list element of a rule is a
    symbol capture unless it appears in ``literals`` (use that to fix the
    head to one symbol, e.g. for matching only a primary name and not an
    alias).

    :param name: primary name symbol or string for this macro
    :param literals: iterable of literal symbols or keywords for patterns
    :param rules: iterable of ``(pattern_datum, template_datum)`` rows
    :param source_ctx: context value for pattern parse errors
    :return: ``Syntax`` instance
    :raises ValueError: when ``rules`` is empty or a row has wrong shape
    """

    rows = tuple(rules)
    if not rows:
        raise ValueError("build_syntax_from_datums requires at least one rule")

    lit = _pattern_literals(literals)
    transforms = []
    for row in rows:
        pat_d, tmpl = _normalize_syntax_rule_row(row)
        ptree = parse_syntax_rules_pattern(pat_d, lit, source_ctx)
        transforms.append(SyntaxTransform(ptree, tmpl))

    return Syntax(
        name, (), tuple(transforms), serialized_literals=lit)


def build_syntax_match(name, rules, *, source_ctx=None):
    """
    Build a ``Syntax`` dispatcher of ``SyntaxTransform`` arms from
    MATCH-dialect patterns and templates.

    Same row shape as :func:`build_syntax_from_datums`. Each pattern datum is lowered
    with :func:`koskari.matcher_ng.parse_explicit_rule_pattern`. Ki
    ``syntax-match`` supplies a literals segment of only the macro name;
    explicit patterns encode list structure directly, so no separate
    literals iterable is required here.

    :param name: primary name symbol or string for this macro
    :param rules: iterable of ``(pattern_datum, template_datum)`` rows
    :param source_ctx: context value for pattern parse errors
    :return: ``Syntax`` instance
    :raises ValueError: when ``rules`` is empty or a row has wrong shape
    """

    rows = tuple(rules)
    if not rows:
        raise ValueError("build_syntax_match requires at least one rule")

    transforms = []
    for row in rows:
        pat_d, tmpl = _normalize_syntax_rule_row(row)
        ptree = parse_explicit_rule_pattern(pat_d, source_ctx)
        transforms.append(SyntaxTransform(ptree, tmpl))

    return Syntax(name, (), tuple(transforms))


def build_macro_syntax(name, literals, rule_rows, *, source_ctx=None):
    """
    Build a ``Syntax`` dispatcher of ``MacroTransform`` arms.

    Patterns describe the full use-site form (nil-terminated list spine).
    The first list element of a rule is a symbol capture unless it appears
    in ``literals`` (see :func:`build_syntax_from_datums`).

    Each procedure is invoked as ``procedure(compiler, tc, **captures)``.

    :param name: primary name symbol or string for this macro
    :param literals: iterable of pattern literals for patterns
    :param rule_rows: iterable of ``(pattern_datum, procedure)`` rows
    :param source_ctx: context value for pattern parse errors
    :return: ``Syntax`` instance
    """

    rows = tuple(rule_rows)
    if not rows:
        raise ValueError("build_macro_syntax requires at least one rule")

    lit = _pattern_literals(literals)
    transforms = []
    for row in rows:
        pat_d, proc = _normalize_proc_rule_row(row)
        ptree = parse_syntax_rules_pattern(pat_d, lit, source_ctx)
        transforms.append(MacroTransform(ptree, proc))

    return Syntax(
        name, (), tuple(transforms), serialized_literals=lit)


def build_macro_match(name, rule_rows, *, source_ctx=None):
    """
    Build a ``Syntax`` dispatcher of ``MacroTransform`` arms using
    MATCH-dialect patterns.

    Same row shape as :func:`build_macro_syntax`. Parallels Ki
    ``macro-match`` (no separate literals list; serialized literals tuple
    is empty at the compile layer).

    Each procedure is invoked as ``procedure(compiler, tc, **captures)``.

    :param name: primary name symbol or string for this macro
    :param rule_rows: iterable of ``(pattern_datum, procedure)`` rows
    :param source_ctx: context value for pattern parse errors
    :return: ``Syntax`` instance
    """

    rows = tuple(rule_rows)
    if not rows:
        raise ValueError("build_macro_match requires at least one rule")

    transforms = []
    for row in rows:
        pat_d, proc = _normalize_proc_rule_row(row)
        ptree = parse_explicit_rule_pattern(pat_d, source_ctx)
        transforms.append(MacroTransform(ptree, proc))

    return Syntax(name, (), tuple(transforms))


def build_special_syntax(name, literals, rule_rows, *, source_ctx=None):
    """
    Build a ``Syntax`` dispatcher of ``SpecialTransform`` arms.

    Patterns describe the full use-site form (nil-terminated list spine).
    The first list element of a rule is a symbol capture unless it appears
    in ``literals`` (see :func:`build_syntax_from_datums`).

    Each procedure is invoked as
    ``procedure(compiler, source_obj, match_result, tc)``.

    :param name: primary name symbol or string for this special form macro
    :param literals: iterable of pattern literals for patterns
    :param rule_rows: iterable of ``(pattern_datum, procedure)`` rows
    :param source_ctx: context value for pattern parse errors
    :return: ``Syntax`` instance
    """

    rows = tuple(rule_rows)
    if not rows:
        raise ValueError("build_special_syntax requires at least one rule")

    lit = _pattern_literals(literals)
    transforms = []
    for row in rows:
        pat_d, proc = _normalize_proc_rule_row(row)
        ptree = parse_syntax_rules_pattern(pat_d, lit, source_ctx)
        transforms.append(SpecialTransform(ptree, proc))

    return Syntax(
        name, (), tuple(transforms), serialized_literals=lit)


def build_special_match_from_rows(name, rule_rows, *, source_ctx=None):
    """
    Build a ``Syntax`` dispatcher of ``SpecialTransform`` arms using
    MATCH-dialect patterns.

    Parallels Ki ``special-match``: same row shape as
    :func:`build_special_syntax`, but each pattern datum is lowered with
    :func:`koskari.matcher_ng.parse_explicit_rule_pattern` instead of
    RULE-style :func:`parse_syntax_rules_pattern`.

    Each procedure is invoked as
    ``procedure(compiler, source_obj, match_result, tc)``.
    """

    rows = tuple(rule_rows)
    if not rows:
        raise ValueError("build_special_match_from_rows requires at least one rule")

    transforms = []
    for row in rows:
        pat_d, proc = _normalize_proc_rule_row(row)
        ptree = parse_explicit_rule_pattern(pat_d, source_ctx)
        transforms.append(SpecialTransform(ptree, proc))

    return Syntax(name, (), tuple(transforms))


def build_syntax_rules(name, tagged_tuple):
    """
    Ki runtime: ``(build-syntax-rules NAME TAGGED)`` deserializer
    """

    if not isinstance(tagged_tuple, tuple) or len(tagged_tuple) != 4:
        raise ValueError("invalid syntax-rules tuple")
    if tagged_tuple[0] is not _tag_syntax_rules:
        raise ValueError("invalid syntax-rules tuple tag")
    name_tagged = deserialize_form(tagged_tuple[1])
    literals = frozenset(deserialize_form(x) for x in tagged_tuple[2])
    transforms = []
    for rt in tagged_tuple[3]:
        if not isinstance(rt, tuple) or len(rt) != 2:
            raise ValueError("invalid syntax rule tuple")
        pat_tup, ser_tmpl = rt
        tmpl = deserialize_form(ser_tmpl)
        transforms.append(SyntaxTransform.from_persistent(pat_tup, tmpl))
    use_name = name if name is not None else name_tagged
    return Syntax(
        use_name, (), tuple(transforms), serialized_literals=literals)


def build_macro_rules(name, literals_tuple, *pattern_and_lambdas):
    """
    Ki runtime: ``(build-macro-rules NAME LITERALS PAT LAMBDA ...)``
    """

    if len(pattern_and_lambdas) % 2 != 0:
        raise ValueError("build-macro-rules requires pattern/lambda pairs")
    literals = frozenset(deserialize_form(x) for x in literals_tuple)
    transforms = []
    for i in range(0, len(pattern_and_lambdas), 2):
        pat_tup = pattern_and_lambdas[i]
        fn = pattern_and_lambdas[i + 1]
        transforms.append(MacroTransform.from_persistent(pat_tup, fn))
    return Syntax(
        name, (), tuple(transforms), serialized_literals=literals)


def build_special_rules(name, literals_tuple, *pattern_and_lambdas):
    """
    Ki runtime: ``(build-special-rules NAME LITERALS PAT LAMBDA ...)``
    """

    if len(pattern_and_lambdas) % 2 != 0:
        raise ValueError("build-special-rules requires pattern/lambda pairs")
    literals = frozenset(deserialize_form(x) for x in literals_tuple)
    transforms = []
    for i in range(0, len(pattern_and_lambdas), 2):
        pat_tup = pattern_and_lambdas[i]
        fn = pattern_and_lambdas[i + 1]
        transforms.append(SpecialTransform.from_persistent(pat_tup, fn))
    return Syntax(
        name, (), tuple(transforms), serialized_literals=literals)


def build_special_match(name, *pattern_and_lambdas):
    """
    Ki runtime: ``(build-special-match NAME PAT LAMBDA ...)``
    """

    return build_special_rules(name, (), *pattern_and_lambdas)


# The end.
