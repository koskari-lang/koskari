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
koskari.syntax

Syntax dispatch scaffolding

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from abc import ABC, abstractmethod
from functools import wraps
from typing import Any

from koskari.bootstrap import builtin
from koskari.compile.compiler import Compiled
from koskari.exceptions import SyntaxMatchError
from koskari.hygiene import expand_template_with_hygiene, finalize_expansion
from koskari.lib import (
    Datum, Matcher, cons, demand_call, gensym, match_select, nil, symbol)
from koskari.matcher import (
    BIND, CALLEDBY, FORM, LITERAL, MATCH, REST, SAME, TEST_ANY,
    MatchCompiler, Pattern, check_pattern_arg,
    parse_explicit_rule_pattern, repeat_capture)
from koskari.reader.layouts import apply_layout
from koskari.types import (
    fill_position, get_position, is_nil, is_pair, is_proper, is_symbol,
    strip_mark, unpack)


__all__ = (
    "BareTransform",
    "Datum",
    "MacroTransform",
    "SpecialTransform",
    "Syntax",
    "SyntaxTransform",
    "Transform",
    "build_bare_syntax",
    "build_macro",
    "build_special",
    "build_syntax",
    "parse_syntax_rules_pattern",
    "special",
)


_symbol_match = symbol("match")


class Syntax(Compiled):
    """
    Ordered dispatcher over ``Transform`` clauses (compiled form). Try order
    follows ``transforms`` tuple order; use ``__add__`` to prepend clauses

    A syntax may also carry one ``BareTransform`` in its ``bare`` slot,
    outside the ordered ``transforms``. That arm expands the syntax name
    when it appears as a lone atom rather than at the head of a call form

    :param name: primary compile-time symbol for this form
    :param transforms: ordered transforms tried on each expansion
    :param bare: lone-atom arm, or None
    """

    def __init__(self, name, transforms=(), bare=None):
        """
        :param name: symbol or str naming this syntax object
        :param transforms: iterable of ``Transform`` instances
        :param bare: ``BareTransform`` for lone-atom use, or None
        """

        self.name = name
        self.transforms = tuple(transforms)

        # instance-level, so a syntax without documentation reports none
        # rather than inheriting this class's own docstring
        self.__doc__ = None

        for t in self.transforms:
            if not isinstance(t, Transform):
                raise TypeError("Syntax transforms must be Transform instances,"
                                f" got {t!r}")

        if bare is not None and not isinstance(bare, BareTransform):
            raise TypeError("Syntax bare must be a BareTransform instance,"
                            f" got {bare!r}")

        self.bare = bare

        # fast matching via match_select
        self._matchers = tuple(t.matcher for t in self.transforms)


    @property
    def matchers(self):
        return self._matchers


    @property
    def patterns(self):
        return tuple(t.pattern for t in self.transforms)


    def match_transformer(self, form):
        """
        Match ``form`` against the first arm whose frozen matcher succeeds.

        :param form: special form pair spine
        :return: ``(transform, result)`` where ``transform`` is the matching
            transform and ``result`` is the match result
        """

        idx, result = match_select(self._matchers, form)
        if result is None:
            return None, None
        return self.transforms[idx], result


    def compile_impl(self, compiler, source_obj):
        """
        Try each transform in order; first non-``None`` result wins

        A non-pair source is a lone-atom use, which bypasses the matchers
        entirely and goes straight to the ``bare`` arm

        A bare-only syntax (no call arms at all) used in form position is
        an alias: the head expands through the bare arm and the tail rides
        along for recompilation. A syntax that has call arms never falls
        back to its bare arm -- a form no call arm matches is a malformed
        use, and the bare arm stands in for the lone atom, not for a call

        :param compiler: active compiler instance
        :param source_obj: full special form being compiled
        :raises SyntaxMatchError: when no arm matches
        :return: compile product from the winning transform
        """

        if not is_pair(source_obj):
            # compile_symbol only routes here through find_bare_compiled,
            # handing the atom in a Datum carrying the caller-threaded
            # position; a missing bare arm means the expansion was
            # invoked directly
            pos = None
            sym = source_obj
            if isinstance(sym, Datum):
                pos = sym.position
                sym = sym.datum
            if self.bare is None:
                raise SyntaxMatchError(
                    "syntax has no bare form",
                    sym,
                    syntax=self,
                    patterns=self.patterns,
                )
            return self.bare.expand_bare(compiler, sym, pos)

        if self.bare is not None and not self.transforms:
            # bare-only: alias semantics, decided up front -- there are
            # no call arms, so there is nothing to consult the matchers
            # about
            head, tail = source_obj
            expansion = self.bare.expand_bare(
                compiler, head, get_position(source_obj))
            reemitted = cons(expansion, tail)
            fill_position(reemitted, get_position(source_obj))
            return reemitted

        idx, result = match_select(self._matchers, source_obj)
        if result is None:
            raise SyntaxMatchError(
                "no matching syntax rule",
                source_obj,
                syntax=self,
                patterns=tuple(t.pattern for t in self.transforms),
            )

        tf = self.transforms[idx]
        return tf.handle_expand(compiler, source_obj, result)


    def __add__(self, other):
        """
        Return a new ``Syntax`` with ``other``'s transforms prepended before
        ``self``'s (``other`` is tried first at expansion time)

        A syntax carries at most one bare arm, so the bare slot is an
        either-or rather than a merge: ``other``'s wins when it has one,
        otherwise ``self``'s carries forward

        Documentation accumulates instead: ``self``'s text carries forward
        and ``other``'s is appended after it, so extending a documented
        syntax keeps the original text and lets the extension describe what
        it adds

        :param other: ``Transform`` or ``Syntax``
        :rtype: Syntax
        """

        bare = self.bare
        other_doc = None

        if isinstance(other, BareTransform):
            new_transforms = self.transforms
            bare = other
        elif isinstance(other, Transform):
            new_transforms = (other,) + self.transforms
        elif isinstance(other, Syntax):
            new_transforms = other.transforms + self.transforms
            other_doc = other.__doc__
            if other.bare is not None:
                bare = other.bare
        else:
            return NotImplemented

        syn = Syntax(self.name, new_transforms, bare)
        syn.__doc__ = _join_docs(self.__doc__, other_doc)
        return syn


    def append_doc(self, doc):
        """
        Append an extension's documentation to what this syntax already
        carries. Used by ``extend-syntax`` so an extension describes what
        it adds rather than replacing the original text

        :param doc: documentation text contributed by the extension
        :return: self
        """

        self.__doc__ = _join_docs(self.__doc__, doc)
        return self


class Transform(ABC):
    """
    One clause of a ``Syntax`` dispatcher: frozen ``matcher`` pattern
    and matcher-backed ``try_expand``
    """

    pattern: Any
    matcher: Matcher


    def __init__(self, pattern):
        """
        :param pattern: matcher pattern tree
        :param handler: callable invoked by procedural arms; ``None`` for
            template-only ``SyntaxTransform`` arms
        """

        if not isinstance(pattern, Pattern):
            raise TypeError(
                "Transform pattern must be a matcher pattern,"
                f" got {type(pattern)}")

        self.pattern = pattern
        self.matcher = MatchCompiler.compile(pattern)


    def try_expand(self, compiler, source_obj):
        """
        Try this arm on ``source_obj``; return compile result or ``None``

        :param compiler: active compiler instance
        :param source_obj: full special form as a proper list (pair spine)
        :return: fragment or form for ``compile_special``, or ``None`` when the
            pattern does not match
        """

        result = self.matcher.match(source_obj)
        if not result.matched:
            return None
        return self.handle_expand(compiler, source_obj, result)


    @abstractmethod
    def handle_expand(self, compiler, source, result):
        """
        When the pattern matches, handle the expansion.
        """

        ...


class SyntaxTransform(Transform):
    """
    Template arm: template expansion with a fresh mark, then finalize
    """

    def __init__(self, pattern, template, pattern_src=nil):
        """
        :param pattern: matcher pattern tree
        :param transformer: ``(compiler, **captures)`` procedure, or
            ``nil`` / ``None`` for template-only expansion
        :param template: syntax-rules template form (often contains pattern vars)
        :param pattern_src: original RULE or MATCH pattern datum for errors
        """

        super().__init__(pattern)

        self.template = template
        self.pattern_src = pattern_src


    def handle_expand(self, compiler, source, result):
        """
        run ``transformer`` or expand ``template`` with captures

        :param compiler: active compiler
        :param source: full special form (proper list spine)
        :param result: match result from ``self._matcher``
        :return: expanded form after finalize
        """

        bindings = _captures_to_bindings(result.captures)
        bindings = _promote_repeat_frame_bindings(bindings)

        subst = expand_template_with_hygiene(
            self.template, bindings, gensym.next_mark())

        return _finalize_macro_output(compiler, subst)


class _BareResult:
    """
    Stand-in for a native ``MatchResult`` on the bare path, where the matcher
    is bypassed and the called-by symbol is the only capture
    """

    __slots__ = ("captures", "matched")


    def __init__(self, captures):
        """
        :param captures: slot name to capture value mapping
        """

        self.captures = captures
        self.matched = True


class BareTransform(SyntaxTransform):
    """
    Bare arm: template expansion for the syntax name used as a lone atom
    rather than at the head of a call form. The called-by symbol is the only
    capture, bound to ``slot``; the matcher is never consulted for dispatch,
    it stands in for the ``(NAME)`` call form the template usually rewrites to
    """

    def __init__(self, slot, template):
        """
        :param slot: symbol or str naming the called-by capture
        :param template: template form expanded with that single capture
        """

        self.slot = str(slot)
        super().__init__(MATCH(CALLEDBY(self.slot)), template)


    def expand_bare(self, compiler, sym, position=None):
        """
        Expand ``template`` with ``sym`` bound to the called-by slot

        The capture carries the caller-threaded enclosing position: the
        atom itself is interned and positionless, so the enclosing form's
        position is the best available location for diagnostics

        :param compiler: active compiler instance
        :param sym: lone atom that triggered this expansion
        :param position: enclosing source position threaded by the caller
        :return: expanded form after finalize
        """

        result = _BareResult({self.slot: Datum(sym, position)})
        expansion = self.handle_expand(compiler, sym, result)
        if position and is_pair(expansion):
            fill_position(expansion, position)
        return expansion


class MacroTransform(Transform):
    """
    Procedural arm: invoke handler, then mark and finalize its output
    """

    def __init__(self, pattern, handler):
        """
        :param pattern: matcher pattern tree
        :param handler:
            ``(compiler, **captures)`` procedure returning expanded form;
            ``captures`` are string-keyed match slots from ``match_result.captures``
        """

        if not callable(handler):
            raise TypeError("MacroTransform handler must be callable,"
                            f" got {handler!r}")

        super().__init__(pattern)
        self.handler = handler


    def handle_expand(self, compiler, source, result):
        """
        Run handler, then mark and finalize the result

        :param compiler: active compiler
        :param source: full special form (proper list spine)
        :param result: match result from ``self._matcher``
        :return: expanded form after finalize
        """

        expanded = self.handler(
            compiler, **_captures_for_procedure(result.captures))
        return _finalize_macro_output(
            compiler, expanded, gensym.next_mark())


class SpecialTransform(Transform):
    """
    Special arm: handler return is the compile result (no marking)
    """

    def __init__(self, pattern, handler):
        """
        :param pattern: matcher pattern tree
        :param handler:
            ``(compiler, source_obj, match_result)`` procedure; use
            :func:`special` if you need other calling conventions
        """

        if not callable(handler):
            raise TypeError("SpecialTransform handler must be callable,"
                            f" got {handler!r}")

        super().__init__(pattern)
        self.handler = handler


    def handle_expand(self, compiler, source, result):
        """
        Return handler result unchanged

        :param compiler: active compiler
        :param source: full special form (proper list spine)
        :param result: match result from ``self._matcher``
        :return: compile product from the handler
        """

        return self.handler(compiler, source, result)


def _join_docs(base, extra):
    """
    Combine a syntax's documentation with an extension's, base text first
    and separated by a blank line. Either side may be absent, and an
    extension repeating the text it already carries adds nothing

    :param base: existing documentation, or None
    :param extra: documentation contributed by the extension, or None
    :return: combined documentation, or None when neither side has any
    """

    if not base:
        return extra
    if not extra or extra == base:
        return base
    return f"{base}\n\n{extra}"


def _captures_to_bindings(captures):
    """
    Map string capture keys to koskari symbols for template expansion.

    Values are native matcher captures (often ``Datum``-wrapped); they are
    stored unchanged so :func:`koskari.hygiene.expand_template_with_hygiene`
    peels at use sites. Macro handlers receive the same ``Datum``-bearing
    ``MatchResult.captures`` mapping unchanged from the matcher.
    """

    return {symbol(k): v for k, v in captures.items()}


def _repeat_frames(value):
    """
    The per-iteration frame dicts a ``repeat_capture`` value holds, or None
    when the value is not that shape

    A Datum at the binding root is never the frame vector; that structure is
    always a bare, non-empty list of dicts. A Datum here is one wrapped cell
    (often a pair spine for REST or a leaf for BIND), and a flat list of
    anything else came from elsewhere
    """

    if isinstance(value, Datum):
        return None
    if not (value and isinstance(value, list)):
        return None
    if not all(isinstance(fr, dict) for fr in value):
        return None
    return value


# the anonymous slot a pair sub-pattern under an ellipsis captures into;
# its frames are promoted onto the pair's own variables, and the slot
# itself is never a template name or a procedure formal
_REPEAT_SLOT_PREFIX = "_repeat"


def _is_repeat_slot(key):
    return str(key).startswith(_REPEAT_SLOT_PREFIX + "#")


def _captures_for_procedure(captures):
    """
    The keyword arguments a procedural arm's lambda takes from a match:
    repeat frames promoted onto the pattern's variables, as the template
    arm sees them, with the anonymous repeat slots dropped since they
    are not formals
    """

    bindings = _promote_repeat_frame_bindings(
        _captures_to_bindings(captures), named=False)
    return {str(k): v for k, v in bindings.items()
            if not _is_repeat_slot(k)}


def _promote_repeat_frame_bindings(bindings, named=True):
    """
    For each ``repeat_capture`` value (bare list of per-iteration dicts),
    copy slot columns onto template keys.

    Each frame is promoted first, so a pair sub-pattern under a further
    ellipsis, which the matcher captured as a nested frame vector inside
    the frame, becomes named columns of that frame before the frames are
    transposed. A variable under N ellipses then lands N lists deep, the
    shape template ellipsis indexing peels one level at a time.

    Keys already present in ``bindings`` are left alone so outer captures and
    the first promoted column win when names collide.

    :param named: promote a REPEAT the pattern named as well as the
        anonymous slots the rule dialect makes; a procedural arm passes
        False, since a named REPEAT is a formal whose value is its frames
    """

    out = dict(bindings)
    for k, v in bindings.items():
        frames = _repeat_frames(v)
        if frames is None:
            continue
        if not named and not _is_repeat_slot(k):
            continue

        frames = [_promote_repeat_frame_bindings(fr, named) for fr in frames]

        # each frame maps slot names to one iteration's captures (often
        # Datum-wrapped); transpose into parallel lists for template
        # ellipsis indexing
        slot_keys = set()
        for fr in frames:
            slot_keys.update(fr.keys())
        for sk in slot_keys:
            sym = symbol(sk) if isinstance(sk, str) else sk
            if sym in out:
                continue
            col = []
            for fr in frames:
                if sk not in fr:
                    col = None
                    break
                col.append(fr[sk])
            if col is None:
                continue
            out[sym] = col
    return out


def _finalize_macro_output(compiler, form, mark=None):
    """
    Apply layout to ``form``, mark a procedural arm's output when
    ``mark`` is given, and peel the ``Datum`` boxes. None is a form like
    any other: an expansion to None compiles to the constant None
    """

    form = apply_layout(form, vars(compiler.module))
    return finalize_expansion(form, mark)


def _normalize_special_patterns(patterns):
    """
    Accept one ``Pattern`` or an iterable of patterns for ordered disjunction
    """

    if isinstance(patterns, (list, tuple)):
        out = tuple(patterns)
    else:
        out = (patterns,)
    for i, p in enumerate(out):
        check_pattern_arg(p, f"special patterns[{i}]")
    return out


def special(form_symbol, *, patterns, unpack=None):
    """
    Build a ``Syntax`` dispatcher whose arms are ``SpecialTransform`` instances
    wrapping the decorated function. ``unpack`` selects one of three fixed
    invoke shapes at decoration time (no per-call unpack dispatch)

    :param form_symbol: primary compile-time head symbol for this form
    :param patterns: one ``matcher.Pattern`` or an iterable (try-order)
    :param unpack: only for this decorator: ``None`` (receive
        ``compiler, source_obj``), ``match`` (receive
        ``compiler, captures_dict``), or ``kwargs`` (captures merged after
        ``compiler``). For ``match`` and ``kwargs``, ``captures_dict``
        includes only slots the matcher bound; values are matcher outputs
        (``Datum`` wrappers may be present). Not available on ``build_*``
        rule rows.
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
            def wrapped(compiler, source_obj, match_result):
                return fn(compiler, source_obj)

        elif unpack == "match":
            def wrapped(compiler, source_obj, match_result):
                return fn(compiler, match_result.captures)

        elif unpack == "kwargs":
            def wrapped(compiler, source_obj, match_result):
                return fn(compiler, **match_result.captures)

        else:
            raise ValueError(f"internal: bad unpack {unpack!r}")

        transforms = [SpecialTransform(p, wrapped) for p in plist]
        syn = Syntax(form_symbol, transforms)
        doc = fn.__doc__
        if doc:
            syn.__doc__ = doc
        return syn

    return deco


def parse_syntax_rules_pattern(pattern_expr, literals, source_ctx):
    """
    Lower a syntax-rules RULE pattern (cons tree) to a ``matcher.Pattern``.

    RULE dialect only; does not implement MATCH-dialect ``matcher/foo`` forms.
    The ``build-syntax`` Koskari builtin and :func:`parse_rule_pattern` call this
    when lowering pattern source.

    :param pattern_expr: koskari pattern datum
    :param literals: set, frozenset, list, or tuple of literal symbols
    :param source_ctx: value recorded in error messages
    :rtype: Pattern
    """

    # a literal is named and matched by its bare spelling
    literals = frozenset(strip_mark(lit) for lit in literals)

    if is_pair(pattern_expr):
        head, rest = pattern_expr
        if strip_mark(head) == _symbol_match:
            if (not is_proper(rest)
                    or not is_nil(rest.cdr)
                    or is_nil(rest)):
                raise ValueError(
                    f"match pattern must have exactly one form: {pattern_expr!r}"
                    f" (context {source_ctx!r})"
                )
            pattern_expr = rest.car

    if pattern_expr is Ellipsis:
        raise TypeError(
            f"invalid ellipsis placement in pattern: {pattern_expr!r}"
            f" (context {source_ctx!r})"
        )

    if is_symbol(pattern_expr):
        bare = strip_mark(pattern_expr)
        if bare in literals:
            return MATCH(LITERAL(bare))
        return MATCH(REST(str(pattern_expr)))

    if not is_pair(pattern_expr):
        return MATCH(LITERAL(pattern_expr))

    return MATCH(*_syntax_pattern_specs(
        list(unpack(pattern_expr)), literals, source_ctx, set()))


@builtin("parse-match-pattern")
def parse_match_pattern(form, source_ctx=None):
    """
    Parse a match style pattern to a Pattern.
    """

    return parse_explicit_rule_pattern(form, source_ctx)


@builtin("parse-rule-pattern")
def parse_rule_pattern(pattern_expr, literals, source_ctx=None):
    """
    Parse a syntax-rules style pattern and literals list to a Pattern.
    """

    if not literals:
        lit = frozenset()
    elif isinstance(literals, (set, frozenset, list, tuple)):
        lit = frozenset(literals)
    elif is_pair(literals) and is_proper(literals):
        lit = frozenset(unpack(literals))
    else:
        lit = frozenset((literals,))

    return parse_syntax_rules_pattern(pattern_expr, lit, source_ctx)


def _syntax_elem_pattern(pat, literals, source_ctx, seen):
    if pat is Ellipsis:
        raise ValueError(
            f"invalid ellipsis placement in pattern: {pat!r}"
            f" (context {source_ctx!r})"
        )

    if is_symbol(pat):
        # a literal matches by its bare spelling; a pattern variable a
        # template introduced keeps its mark in its slot name, so the
        # marked template reference finds it and a bare use-site symbol
        # does not
        bare = strip_mark(pat)
        if bare in literals:
            return LITERAL(bare)
        name = str(pat)
        if name in seen:
            return SAME(name)
        seen.add(name)
        return BIND(name)

    if not is_pair(pat):
        # a keyword or other atom literal matches by its bare spelling
        return LITERAL(pat)

    return FORM(MATCH(*_syntax_pattern_specs(
        list(unpack(pat)), literals, source_ctx, seen)))


def _syntax_pattern_specs(p_items, literals, source_ctx, seen):
    specs = []
    i = 0
    while i < len(p_items):
        if i + 1 < len(p_items) and p_items[i + 1] is Ellipsis:
            pat = p_items[i]
            if is_symbol(pat) and strip_mark(pat) not in literals:
                if i + 2 < len(p_items):
                    specs.append(REST(str(pat)))
                else:
                    specs.append(repeat_capture(str(pat), TEST_ANY()))
            else:
                slot = str(gensym(_REPEAT_SLOT_PREFIX))
                inner = _syntax_elem_pattern(
                    pat, literals, source_ctx, set())
                specs.append(repeat_capture(slot, inner))
            i += 2
            continue

        specs.append(_syntax_elem_pattern(
            p_items[i], literals, source_ctx, seen))
        i += 1

    return specs


def _wrap_macro_handler(raw):
    """
    Adapt a Koskari procedure to MacroTransform ``handle_expand`` calling convention
    """

    @wraps(raw)
    def handler(compiler, **captures):
        return demand_call(raw, **captures)

    return handler


def _wrap_special_handler(raw):
    """
    Adapt a Koskari procedure to SpecialTransform ``handle_expand`` calling convention
    """

    @wraps(raw)
    def handler(compiler, source_obj, match_result):
        return demand_call(
            raw, compiler, **_captures_for_procedure(match_result.captures))

    return handler


@builtin('build-syntax')
def build_syntax(name, *pattern_template_pairs):
    """
    Koskari runtime: ``(build-syntax NAME PAT TMPL ...)`` where each ``PAT``
    is a lowered ``matcher.Pattern`` (for example from ``parse-rule-pattern`` or
    ``parse-match-pattern`` at use site) and each ``TMPL`` is a template datum
    """

    if len(pattern_template_pairs) % 2 != 0:
        raise ValueError("build-syntax requires pattern/template pairs")

    transforms = []
    for i in range(0, len(pattern_template_pairs), 2):
        pat = pattern_template_pairs[i]
        tmpl = pattern_template_pairs[i + 1]
        transforms.append(SyntaxTransform(pat, tmpl))

    return Syntax(name, transforms)


@builtin('build-bare-syntax')
def build_bare_syntax(name, slot, template):
    """
    Koskari runtime: ``(build-bare-syntax NAME SLOT TMPL)``, producing a
    ``Syntax`` carrying only a bare arm. ``SLOT`` names the called-by
    capture that ``TMPL`` refers to. Combine with ``+`` to attach the arm
    to an existing syntax
    """

    return Syntax(name, (), BareTransform(slot, template))


@builtin('build-macro')
def build_macro(name, *pattern_and_lambdas):
    """
    Koskari runtime: ``(build-macro NAME PAT LAMBDA ...)``
    """

    if len(pattern_and_lambdas) % 2 != 0:
        raise ValueError("build-macro requires pattern/lambda pairs")

    transforms = []
    for i in range(0, len(pattern_and_lambdas), 2):
        pat = pattern_and_lambdas[i]
        fn = pattern_and_lambdas[i + 1]
        transforms.append(MacroTransform(pat, _wrap_macro_handler(fn)))

    return Syntax(name, transforms)


@builtin('build-special')
def build_special(name, *pattern_and_lambdas):
    """
    Koskari runtime: ``(build-special NAME PAT LAMBDA ...)``
    """

    if len(pattern_and_lambdas) % 2 != 0:
        raise ValueError("build-special requires pattern/lambda pairs")

    transforms = []
    for i in range(0, len(pattern_and_lambdas), 2):
        pat = pattern_and_lambdas[i]
        fn = pattern_and_lambdas[i + 1]
        transforms.append(SpecialTransform(pat, _wrap_special_handler(fn)))

    return Syntax(name, transforms)


# The end.
