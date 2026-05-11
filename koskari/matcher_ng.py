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
matcher_ng

Python surface for the next-generation native matcher. Provides the thaw
helper that reconstructs a native Matcher from its frozen tagged-tuple
representation, plus a small builder API for authoring matcher patterns
in Python that lower to the same frozen form. Lowering uses either the
baseline MatchCompiler (slots, externs, consts, nodes only) or
HygienicMatchCompiler, which additionally records koskari hygiene_ng
metadata for source-form matchers. Rule-level MATCH spines use
:func:`parse_explicit_rule_pattern`. Named repeat captures (CAPTURE wrapping
REPEAT) produce per-iteration frame dicts; see repeat_capture and
binding_inner_pattern

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude Opus 4.7 via Cursor
"""


from koskari.hygiene_ng import Hygiene
from koskari.lib import (
    Datum,
    Matcher,
    MatchResult,
    keyword,
    nil,
    pair,
    symbol,
)
from koskari.types import is_keyword, is_pair, is_proper, is_symbol, unpack
from typing import Protocol, runtime_checkable


__all__ = [
    "Datum",
    "Matcher",
    "MatchResult",
    "Hygiene",

    "thaw",
    "make_matcher",

    "Pattern",
    "Composite",
    "MatchCompiler",
    "HygienicMatchCompiler",

    "TEST_ANY",
    "LITERAL",
    "TYPE",
    "PREDICATE",
    "CAPTURE",
    "FORM",
    "MATCH",
    "GROUP",
    "ONEOF",
    "REPEAT",
    "OPTIONAL",
    "REST",
    "TAIL",
    "NOT",
    "CLASS",
    "FIELD",

    "BIND",
    "VALUE",
    "KEYWORD",
    "SYMBOL",
    "PAIR",
    "LITERAL_KW",
    "LITERAL_SYM",
    "LIST",

    "repeat_capture",
    "slot_prefix",
    "binding_inner_pattern",
    "bindings_grouped_list_cell_pattern",
    "BINDING_CELL",

    "DEFINE",
    "DECL",
    "BINDING",
    "BINDINGS",
    "FORMALS",
    "FORMALS_SOURCE_KEY",
    "USING",
    "USING_UNPACK",
    "BODY",

    "HygienicPattern",

    "MATCHER_REGISTRY",
    "register_matcher",
    "lookup_matcher",
    "parse_matcher",
    "parse_explicit_rule_pattern",
    "pattern_slot_names",
    "META_PATTERN",
    "META_MATCHER",
    "KNOWN_HEADS",
    "CAPTURE_OPTION_KEYS",

]


def thaw(tagged_tuple):
    """
    Reconstruct a native Matcher from its frozen tagged-tuple
    representation
    """

    return Matcher.from_tuple(tagged_tuple)


# === Builder infrastructure ===


class MatchCompiler:
    """
    Baseline freeze-time compiler for one lowering pass. Holds the four
    insertion-ordered tables that become the slots, externs, consts, and
    nodes of the frozen 6-tuple, plus capture-scope bookkeeping
    """


    def __init__(self):
        """
        Construct an empty baseline match compiler
        """

        self.slot_names = []
        self.extern_indices = {}
        self.consts = []
        self.nodes = []
        self.capture_scope_stack = [{}]
        self.in_class_fields = False


    def push_capture_scope(self):
        """
        Begin an isolated duplicate-slot scope (used per ONEOF branch)
        """

        self.capture_scope_stack.append({})


    def pop_capture_scope(self):
        """
        End the innermost duplicate-slot scope
        """

        if len(self.capture_scope_stack) <= 1:
            raise RuntimeError(
                "pop_capture_scope underflow (unbalanced ONEOF scope)")
        self.capture_scope_stack.pop()


    def intern_slot(self, name):
        """
        Append a capture slot name and return its index. Duplicate names
        are allowed (distinct indices); use claim_slot_name to forbid
        duplicates within a capture scope
        """

        idx = len(self.slot_names)
        self.slot_names.append(name)
        return idx


    def intern_extern(self, name):
        """
        Return the extern index for the named extern, allocating one when
        first seen
        """

        if name in self.extern_indices:
            return self.extern_indices[name]

        idx = len(self.extern_indices)
        self.extern_indices[name] = idx
        return idx


    def intern_const(self, value):
        """
        Return the const index for the given value, allocating one when
        no equal-and-same-type entry already exists. Type-aware to
        preserve distinctions like 0 vs False
        """

        for i, existing in enumerate(self.consts):
            if type(existing) is type(value) and existing == value:
                return i

        idx = len(self.consts)
        self.consts.append(value)
        return idx


    def append_node(self, node_tuple):
        """
        Append a node tuple to the nodes table and return its index
        """

        idx = len(self.nodes)
        self.nodes.append(node_tuple)
        return idx


    def claim_slot_name(self, name, signature):
        """
        Reject incompatible duplicate slot bindings within the current capture
        scope. A second claim with the same ``signature`` as the first is
        allowed so syntax-rules patterns can repeat a pattern variable (the
        native engine requires the same value twice). ONEOF branches use a fresh
        scope per branch so alternate arms may reuse slot names

        :param name: capture slot name
        :param signature: hashable tuple describing this claim
        """

        top = self.capture_scope_stack[-1]
        if name in top:
            prev = top[name]
            if prev == signature:
                return
            if (isinstance(prev, tuple) and isinstance(signature, tuple)
                    and len(prev) >= 1 and len(signature) >= 1
                    and prev[0] == "capture" and signature[0] == "capture"):
                raise ValueError(
                    "duplicate slot name %r in matcher pattern "
                    "(differing matcher type)" % (name,))
            raise ValueError(
                "duplicate slot name %r in matcher pattern" % (name,))
        top[name] = signature


    def on_pattern(self, pat):
        """
        Hook invoked when a pattern node begins lowering; baseline compiler
        ignores it
        """

        pass


    def lower(self, root):
        """
        Lower root into this compiler and return the frozen matcher tuple

        :param root: pattern to lower
        :rtype: tuple
        """

        entry = root.lower(self)
        frozen = (
            "MATCHER",
            tuple(Pattern._slot_names(self)),
            tuple(Pattern._extern_names(self)),
            tuple(self.consts),
            tuple(self.nodes),
            entry,
        )
        return frozen


class HygienicMatchCompiler(MatchCompiler):
    """
    Match compiler that accumulates koskari hygiene_ng binder and
    scope-region owners alongside the baseline tables. Use for matchers
    over compile-time source forms; runtime-only matchers should use the
    baseline MatchCompiler via MatchCompiler().lower(pattern)
    """


    def __init__(self):
        """
        Construct a match compiler with empty hygiene registration tables
        """

        super().__init__()
        self.hygiene_binders = {}
        self.hygiene_scope_regions = {}


    def add_binder(self, slot, owner):
        """
        Record the Pattern or Composite that owns hygiene interpretation
        for this binder slot. Raises ValueError on duplicate slot entries

        :param slot: slot name introduced by the composite
        :param owner: HygienicPattern instance whose iter_introduced_symbols
            and iter_recurse_targets Hygiene uses at runtime
        """

        if slot in self.hygiene_binders:
            raise ValueError(
                "duplicate binder for slot %r in matcher pattern"
                % (slot,))
        self.hygiene_binders[slot] = owner


    def add_scope_region(self, slot, owner):
        """
        Record the Pattern instance that owns scope-region traversal for
        this slot. Re-registration of the same owner is idempotent

        :param slot: slot name carrying scope-region semantics
        :param owner: HygienicPattern instance whose iter_scope_targets
            Hygiene uses
        """

        existing = self.hygiene_scope_regions.get(slot)
        if existing is not None:
            if existing is not owner:
                raise ValueError(
                    "scope region %r already registered" % (slot,))
            return
        self.hygiene_scope_regions[slot] = owner


    def build_hygiene(self):
        """
        Assemble the hygiene plan from accumulated binders and scope
        regions. Returns an empty Hygiene when no composite has
        registered anything
        """

        if not self.hygiene_binders and not self.hygiene_scope_regions:
            return Hygiene({}, {})

        return Hygiene(
            dict(self.hygiene_binders),
            dict(self.hygiene_scope_regions),
        )


    def on_pattern(self, pat):
        """
        Register HygienicPattern owners during the same lower walk as node
        emission
        """

        if isinstance(pat, HygienicPattern):
            pat.register_hygiene(self)


    def lower(self, root):
        """
        Lower root into this compiler and return the frozen matcher tuple.
        Hygiene owners register via on_pattern during lowering
        """

        entry = root.lower(self)
        frozen = (
            "MATCHER",
            tuple(Pattern._slot_names(self)),
            tuple(Pattern._extern_names(self)),
            tuple(self.consts),
            tuple(self.nodes),
            entry,
        )
        return frozen


@runtime_checkable
class Pattern(Protocol):
    """
    Structural base for matcher_ng pattern builders. Implementations
    define lower(ctx) to append their node tuple to a MatchCompiler and
    return the resulting node index. MatchCompiler.lower and
    HygienicMatchCompiler.lower drive lowering from the root pattern;
    composites and BODY call ctx.on_pattern(self) before expanding so
    hygienic compilers register in the same walk; freeze, build, and
    match use the baseline MatchCompiler path
    """


    def lower(self, ctx):
        """
        Append nodes for this pattern into the compiler context and
        return the index of the resulting top node
        """

        ...


    def freeze(self):
        """
        Lower this pattern to a frozen 7-tuple suitable for
        Matcher.from_tuple
        """

        return MatchCompiler().lower(self)


    def build(self):
        """
        Lower this pattern and thaw it into a native Matcher. Match-time
        behaviour is pure native engine

        :rtype: Matcher
        """

        return Matcher.from_tuple(MatchCompiler().lower(self))


    def match(self, subject, **externs):
        """
        Convenience helper that thaws this pattern into a native Matcher
        and runs it against subject. Returns the native MatchResult;
        captures are final
        """

        return self.build().match(subject, **externs)


    def visit(self, fn):
        """
        Invoke fn on this pattern; subclasses recurse into children
        """

        fn(self)


    @staticmethod
    def _slot_names(ctx):
        """
        Return slot names in declaration order
        """

        return list(ctx.slot_names)


    @staticmethod
    def _extern_names(ctx):
        """
        Return extern names in declaration order
        """

        return list(ctx.extern_indices.keys())


def check_pattern_arg(arg, what):
    """
    Validate that arg is a Pattern instance and raise a clear TypeError
    when it is not
    """

    if not isinstance(arg, Pattern):
        raise TypeError(
            "%s must be a matcher_ng pattern, got %r"
            % (what, type(arg).__name__))


def check_str_arg(arg, what, *, allow_empty=False):
    """
    Validate that arg is a non-empty (by default) str. Raises TypeError
    for the wrong type and ValueError for the empty-string case
    """

    if not isinstance(arg, str):
        raise TypeError(
            "%s must be a str, got %r" % (what, type(arg).__name__))
    if not allow_empty and not arg:
        raise ValueError("%s must be a non-empty str" % (what,))


def pattern_slot_names(pattern):
    """
    Slot names for ``pattern`` in declaration order (hygienic lower)
    """

    check_pattern_arg(pattern, "pattern_slot_names pattern")
    hc = HygienicMatchCompiler()
    hc.lower(pattern)
    return list(Pattern._slot_names(hc))


# === Primitive builders ===


class TEST_ANY(Pattern):
    """
    Match any single element
    """


    def __init__(self):
        """
        TEST_ANY takes no parameters
        """

        pass


    def lower(self, ctx):
        """
        Emit the TEST_ANY node tuple
        """

        return ctx.append_node(("TEST", "ANY", -1))


class LITERAL(Pattern):
    """
    Match a single element equal to the given literal value
    """


    def __init__(self, value):
        """
        Capture the literal value to be referenced through the consts
        table at freeze time

        :param value: any concrete value, must not be a pattern
        :raises TypeError: when value is itself a Pattern
        """

        if isinstance(value, Pattern):
            raise TypeError(
                "LITERAL value must be a concrete value, "
                "not a pattern: %r" % (type(value).__name__,))
        self._value = value


    def lower(self, ctx):
        """
        Intern the literal in consts and emit a TEST/LITERAL node
        """

        idx = ctx.intern_const(self._value)
        return ctx.append_node(("TEST", "LITERAL", idx))


class TYPE(Pattern):
    """
    Match a single element which is an instance of the named extern type
    """


    def __init__(self, extern_name):
        """
        :param extern_name: the extern binding name supplied at match
            time, e.g. matcher.match(subj, symbol=symbol)
        :raises TypeError: when extern_name is not a str
        :raises ValueError: when extern_name is empty
        """

        check_str_arg(extern_name, "TYPE extern name")
        self._extern = extern_name


    def lower(self, ctx):
        """
        Intern the extern and emit a TEST/TYPE node
        """

        idx = ctx.intern_extern(self._extern)
        return ctx.append_node(("TEST", "TYPE", idx))


class PREDICATE(Pattern):
    """
    Match a single element for which the named extern callable returns
    a truthy value
    """


    def __init__(self, extern_name):
        """
        :param extern_name: the extern binding name supplied at match
            time
        :raises TypeError: when extern_name is not a str
        :raises ValueError: when extern_name is empty
        """

        check_str_arg(extern_name, "PREDICATE extern name")
        self._extern = extern_name


    def lower(self, ctx):
        """
        Intern the extern and emit a TEST/PREDICATE node
        """

        idx = ctx.intern_extern(self._extern)
        return ctx.append_node(("TEST", "PREDICATE", idx))


class CAPTURE(Pattern):
    """
    Bind one matched element (or a sub-pattern result) into a named slot
    """


    def __init__(
            self,
            slot,
            sub=None,
            *,
            hygiene_role=0,
            scope_slot=None):
        """
        :param slot: target slot name
        :param sub: optional sub-pattern; when None the capture takes
            any single element
        :param hygiene_role: integer in [0, 0xFFFF]; reserved for the
            phase 9 hygiene migration
        :param scope_slot: optional slot name carrying scope metadata;
            also reserved for phase 9
        :raises TypeError: when slot is not a str, sub is not a pattern
            or None, scope_slot is not str or None, or hygiene_role is
            not an int
        :raises ValueError: when slot is empty or hygiene_role is out
            of range
        """

        check_str_arg(slot, "CAPTURE slot")
        if sub is not None:
            check_pattern_arg(sub, "CAPTURE sub")
        if scope_slot is not None:
            check_str_arg(scope_slot, "CAPTURE scope_slot")
        if isinstance(hygiene_role, bool) or not isinstance(hygiene_role, int):
            raise TypeError(
                "CAPTURE hygiene_role must be an int, got %r"
                % type(hygiene_role).__name__)
        if hygiene_role < 0 or hygiene_role > 0xFFFF:
            raise ValueError(
                "CAPTURE hygiene_role out of uint16 range: %d"
                % (hygiene_role,))

        self._slot = slot
        self._sub = sub
        self._hygiene_role = hygiene_role
        self._scope_slot = scope_slot


    def _claim_signature(self):
        """
        Freeze-time tuple so repeated identical captures share a signature
        """

        if self._sub is None:
            return ("capture", None)
        if isinstance(self._sub, TYPE):
            return ("capture", ("type", self._sub._extern))
        return ("capture", ("complex", id(self._sub)))


    def lower(self, ctx):
        """
        Reserve the slot, lower the optional sub-pattern, and emit the
        CAPTURE node
        """

        ctx.claim_slot_name(self._slot, self._claim_signature())
        slot_idx = ctx.intern_slot(self._slot)
        if self._sub is None:
            sub_idx = -1
        else:
            sub_idx = self._sub.lower(ctx)
        if self._scope_slot is None:
            scope_idx = -1
        else:
            scope_idx = ctx.intern_slot(self._scope_slot)
        return ctx.append_node(
            ("CAPTURE", slot_idx, sub_idx,
             self._hygiene_role, scope_idx))


    def visit(self, fn):
        fn(self)
        if self._sub is not None:
            self._sub.visit(fn)


class FORM(Pattern):
    """
    Descend into a single source element treated as a pair-tree and
    apply the inner pattern against its contents
    """


    def __init__(self, sub):
        """
        :param sub: pattern applied to the inner sequence
        :raises TypeError: when sub is not a pattern
        """

        check_pattern_arg(sub, "FORM sub")
        self._sub = sub


    def lower(self, ctx):
        """
        Emit the inner pattern then a FORM node referencing it
        """

        sub_idx = self._sub.lower(ctx)
        return ctx.append_node(("FORM", sub_idx))


    def visit(self, fn):
        fn(self)
        self._sub.visit(fn)


class MATCH(Pattern):
    """
    Sequential spine: match children in order in the current capture dict
    """


    def __init__(self, *children):
        """
        :param children: zero or more patterns
        :raises TypeError: when any child is not a pattern
        """

        for c in children:
            check_pattern_arg(c, "MATCH child")
        self._children = children


    def lower(self, ctx):
        """
        Emit each child then a MATCH node listing them in order
        """

        idxs = [c.lower(ctx) for c in self._children]
        return ctx.append_node(("MATCH",) + tuple(idxs))


    def visit(self, fn):
        fn(self)
        for c in self._children:
            c.visit(fn)


class GROUP(Pattern):
    """
    Named nested capture namespace: inner captures go into a sub-dict
    stored under the given slot name in the parent dict
    """


    def __init__(self, slot, *children):
        """
        :param slot: dict key / capture slot name for the nested result
        :param children: zero or more patterns matched in order inside
            the nested dict
        """

        check_str_arg(slot, "GROUP slot")
        for c in children:
            check_pattern_arg(c, "GROUP child")
        self._slot = slot
        self._children = children


    def lower(self, ctx):
        """
        Reserve the group slot name, run children in a fresh capture
        scope, emit a GROUP frozen node (slot index plus child indices)
        """

        ctx.claim_slot_name(self._slot, ("group", id(self)))
        slot_idx = ctx.intern_slot(self._slot)
        ctx.push_capture_scope()
        try:
            idxs = [c.lower(ctx) for c in self._children]
        finally:
            ctx.pop_capture_scope()
        return ctx.append_node(("GROUP", slot_idx) + tuple(idxs))


    def visit(self, fn):
        fn(self)
        for c in self._children:
            c.visit(fn)


class ONEOF(Pattern):
    """
    Disjunction: try each branch in order until one matches
    """


    def __init__(self, *branches):
        """
        :param branches: one or more patterns
        :raises TypeError: when any branch is not a pattern
        :raises ValueError: when called with no branches
        """

        if not branches:
            raise ValueError("ONEOF requires at least one branch")
        for b in branches:
            check_pattern_arg(b, "ONEOF branch")
        self._branches = branches


    def lower(self, ctx):
        """
        Emit each branch then a ONEOF node listing them in order. Each
        branch gets its own capture scope so identical slot names across
        mutually exclusive arms remain valid
        """

        idxs = []
        for b in self._branches:
            ctx.push_capture_scope()
            try:
                idxs.append(b.lower(ctx))
            finally:
                ctx.pop_capture_scope()
        return ctx.append_node(("ONEOF",) + tuple(idxs))


    def visit(self, fn):
        fn(self)
        for b in self._branches:
            b.visit(fn)


class REPEAT(Pattern):
    """
    Greedily match the inner pattern one-or-more times
    """


    def __init__(self, sub):
        """
        :param sub: pattern repeated against successive elements
        :raises TypeError: when sub is not a pattern
        """

        check_pattern_arg(sub, "REPEAT sub")
        self._sub = sub


    def lower(self, ctx):
        """
        Emit the inner pattern then a REPEAT node referencing it
        """

        sub_idx = self._sub.lower(ctx)
        return ctx.append_node(("REPEAT", sub_idx))


    def visit(self, fn):
        fn(self)
        self._sub.visit(fn)


class OPTIONAL(Pattern):
    """
    Match the inner pattern at most once. When the inner pattern does
    not match, any capture slots it would have bound are omitted from
    the result captures dict
    """


    def __init__(self, sub):
        """
        :param sub: pattern that may or may not match
        :raises TypeError: when sub is not a pattern
        """

        check_pattern_arg(sub, "OPTIONAL sub")
        self._sub = sub


    def lower(self, ctx):
        """
        Emit the inner pattern then an OPTIONAL node referencing it
        """

        sub_idx = self._sub.lower(ctx)
        return ctx.append_node(("OPTIONAL", sub_idx))


    def visit(self, fn):
        fn(self)
        self._sub.visit(fn)


class REST(Pattern):
    """
    Capture the remainder of the source list into the named slot
    """


    def __init__(self, slot):
        """
        :param slot: target slot name
        :raises TypeError: when slot is not a str
        :raises ValueError: when slot is empty
        """

        check_str_arg(slot, "REST slot")
        self._slot = slot


    def lower(self, ctx):
        """
        Reserve the slot and emit a REST node
        """

        ctx.claim_slot_name(self._slot, ("rest", id(self)))
        slot_idx = ctx.intern_slot(self._slot)
        return ctx.append_node(("REST", slot_idx))


class TAIL(Pattern):
    """
    Optional remainder capture; equivalent to OPTIONAL(REST(slot))

    When the optional arm matches, REST binds at least one remaining
    spine cell. When it does not, the slot is omitted from captures
    """


    def __init__(self, slot):
        """
        :param slot: str capture slot name for the non-empty remainder

        :raises TypeError: when slot is not a str
        :raises ValueError: when slot is empty
        """

        check_str_arg(slot, "TAIL slot")
        self._slot = slot


    def lower(self, ctx):
        """
        Emit OPTIONAL wrapping REST for this slot
        """

        return OPTIONAL(REST(self._slot)).lower(ctx)


    def visit(self, fn):
        fn(self)
        r = REST(self._slot)
        OPTIONAL(r).visit(fn)


class NOT(Pattern):
    """
    Negative lookahead: succeed when the inner pattern does not match
    the current single element. Width 1 with no captures inside
    """


    def __init__(self, sub):
        """
        :param sub: pattern whose match would cause NOT to fail
        :raises TypeError: when sub is not a pattern
        """

        check_pattern_arg(sub, "NOT sub")
        self._sub = sub


    def lower(self, ctx):
        """
        Emit the inner pattern then a NOT node referencing it
        """

        sub_idx = self._sub.lower(ctx)
        return ctx.append_node(("NOT", sub_idx))


    def visit(self, fn):
        fn(self)
        self._sub.visit(fn)


class CLASS(Pattern):
    """
    Match a single element which is an instance of the named extern
    type. Field children project named or positional attributes through
    sub-patterns
    """


    def __init__(self, type_extern, *fields):
        """
        :param type_extern: name of the extern binding for the class
            type
        :param fields: zero or more FIELD instances
        :raises TypeError: when type_extern is not a str or a field is
            not a FIELD
        :raises ValueError: when type_extern is empty
        """

        check_str_arg(type_extern, "CLASS type_extern")
        for f in fields:
            if not isinstance(f, FIELD):
                raise TypeError(
                    "CLASS field arguments must be FIELD instances, "
                    "got %r" % (type(f).__name__,))
        self._extern = type_extern
        self._fields = fields


    def lower(self, ctx):
        """
        Emit each FIELD child then a CLASS node referencing them.
        Toggles ctx.in_class_fields so that nested CLASS/FIELD trees
        validate correctly. The CLASS extern is interned before
        recursing into fields so that the externs tuple reflects
        source declaration order
        """

        ext_idx = ctx.intern_extern(self._extern)
        prev = ctx.in_class_fields
        ctx.in_class_fields = True
        try:
            field_idxs = [f.lower(ctx) for f in self._fields]
        finally:
            ctx.in_class_fields = prev
        return ctx.append_node(
            ("CLASS", ext_idx) + tuple(field_idxs))


    def visit(self, fn):
        fn(self)
        for f in self._fields:
            f.visit(fn)


class FIELD(Pattern):
    """
    Project a single attribute of a CLASS instance through an inner
    pattern. Positional when name is None, keyword otherwise
    """


    def __init__(self, sub, *, name=None):
        """
        :param sub: pattern applied to the projected attribute value
        :param name: optional attribute name; when None the field is
            positional and resolved via __match_args__
        :raises TypeError: when sub is not a pattern or name is not a
            str
        :raises ValueError: when name is provided but empty
        """

        check_pattern_arg(sub, "FIELD sub")
        if name is not None:
            check_str_arg(name, "FIELD name")
        self._sub = sub
        self._name = name


    def lower(self, ctx):
        """
        Emit a FIELD_POS or FIELD_KW node. Rejects emission outside a
        CLASS scope. Temporarily clears in_class_fields so that the
        sub-pattern can host its own CLASS/FIELD nesting
        """

        if not ctx.in_class_fields:
            raise ValueError(
                "FIELD must be a child of CLASS")

        prev = ctx.in_class_fields
        ctx.in_class_fields = False
        try:
            sub_idx = self._sub.lower(ctx)
        finally:
            ctx.in_class_fields = prev

        if self._name is None:
            return ctx.append_node(("FIELD_POS", sub_idx))
        return ctx.append_node(("FIELD_KW", sub_idx, self._name))


    def visit(self, fn):
        fn(self)
        self._sub.visit(fn)


def repeat_capture(slot, inner):
    """
    Capture the results of repeating inner across successive subject
    elements.     The native engine stores matched.captures[slot] as a bare Python
    list of dict objects (not wrapped in Datum): each dict maps slot
    names touched during one iteration to their captured values
    (sparse; absent slots are omitted). Inner patterns should use
    CAPTURE nodes for fields to collect per iteration

    :param slot: outer slot receiving the list of frame dicts
    :param inner: pattern run once per repetition (often FORM around
        a MATCH of captures)
    :rtype: CAPTURE
    """

    check_str_arg(slot, "repeat_capture slot")
    check_pattern_arg(inner, "repeat_capture inner")
    return CAPTURE(slot, REPEAT(inner))


def slot_prefix(pattern, prefix, sep="__"):
    """
    Return a copy of pattern with every nested CAPTURE, REST, and TAIL
    slot renamed to prefix + sep + original, and CAPTURE scope_slot likewise
    prefixed when present. Use before composing the same binding-shaped
    subtree multiple times so freeze-time duplicate-slot detection does
    not fire

    :param pattern: matcher_ng pattern tree
    :param prefix: non-empty prefix string
    :param sep: separator between prefix and original slot name
    :raises TypeError: when pattern is not a matcher_ng pattern
    :raises ValueError: when prefix is empty
    """

    check_pattern_arg(pattern, "slot_prefix pattern")
    check_str_arg(prefix, "slot_prefix prefix")
    return slot_prefix_copy(pattern, prefix, sep)


def slot_prefix_copy(pattern, prefix, sep):
    """
    Recursive implementation for slot_prefix
    """

    if isinstance(pattern, CAPTURE):
        new_scope = None
        if pattern._scope_slot is not None:
            new_scope = prefix + sep + pattern._scope_slot
        sub = (slot_prefix_copy(pattern._sub, prefix, sep)
               if pattern._sub is not None else None)
        return CAPTURE(
            prefix + sep + pattern._slot,
            sub,
            hygiene_role=pattern._hygiene_role,
            scope_slot=new_scope,
        )

    if isinstance(pattern, FORM):
        return FORM(slot_prefix_copy(pattern._sub, prefix, sep))

    if isinstance(pattern, MATCH):
        return MATCH(*[
            slot_prefix_copy(c, prefix, sep) for c in pattern._children])

    if isinstance(pattern, GROUP):
        return GROUP(
            prefix + sep + pattern._slot,
            *[slot_prefix_copy(c, prefix, sep) for c in pattern._children])

    if isinstance(pattern, ONEOF):
        return ONEOF(*[
            slot_prefix_copy(b, prefix, sep) for b in pattern._branches])

    if isinstance(pattern, REPEAT):
        return REPEAT(slot_prefix_copy(pattern._sub, prefix, sep))

    if isinstance(pattern, OPTIONAL):
        return OPTIONAL(slot_prefix_copy(pattern._sub, prefix, sep))

    if isinstance(pattern, NOT):
        return NOT(slot_prefix_copy(pattern._sub, prefix, sep))

    if isinstance(pattern, REST):
        return REST(prefix + sep + pattern._slot)

    if isinstance(pattern, TAIL):
        return TAIL(prefix + sep + pattern._slot)

    if isinstance(pattern, CLASS):
        new_fields = tuple(
            FIELD(slot_prefix_copy(f._sub, prefix, sep), name=f._name)
            for f in pattern._fields)
        return CLASS(pattern._extern, *new_fields)

    if isinstance(pattern, FIELD):
        return FIELD(slot_prefix_copy(pattern._sub, prefix, sep),
                     name=pattern._name)

    if isinstance(pattern, (TEST_ANY, LITERAL, TYPE, PREDICATE)):
        return pattern

    raise TypeError(
        "slot_prefix: unsupported pattern type %r"
        % (type(pattern).__name__,))


FORMALS_SOURCE_KEY = "source"


def binding_inner_pattern():
    """
    Build the inner shape of a single koskari binding cell as matcher_ng
    primitives: FORM around ONEOF of (SYM EXPR) or (EXPR as: SYM).
    The outer FORM descends into one spine element that is already a
    binding pair (SYM EXPR). For a grouped cell such as ``((t 1))`` from
    ``[[t 1]]``, use :func:`bindings_grouped_list_cell_pattern` inside
    :class:`BINDINGS` instead; a single FORM here would yield an inner
    spine of one pair element, and ONEOF would run MATCH(SYMBOL, ...) on
    that pair and fail

    Does not wrap an outer CAPTURE; when embedding multiple binding
    cells in one matcher, pass the result through slot_prefix with a
    unique prefix per cell first

    :rtype: FORM
    """

    return FORM(ONEOF(
        MATCH(SYMBOL("name"), VALUE("expr")),
        MATCH(VALUE("expr"), LITERAL_KW("as"), SYMBOL("name")),
    ))


def binding_group_capture_pattern():
    """
    Whole-cell CAPTURE plus binding_inner_pattern; same inner layout as
    :class:`BINDING` GROUP dict and each :class:`BINDINGS` repeat frame
    """

    return CAPTURE(FORMALS_SOURCE_KEY, binding_inner_pattern())


def bindings_grouped_list_cell_pattern():
    """
    Match one binding-list element that wraps a single pair in an extra
    list cell, e.g. ``((t 1))`` from ``[[t 1]]``. FORM enters the outer
    list; MATCH consumes the sole inner element; inner FORM enters the
    pair tree for ONEOF(MATCH, MATCH)
    """

    inner = binding_group_capture_pattern()
    return FORM(MATCH(inner))


def BINDING_CELL(slot):
    """
    One binding-shaped subject element: same as binding_inner_pattern
    but with captures rewritten to slot + sep + name and slot + sep +
    expr so multiple cells can coexist in one matcher

    :param slot: logical name prefix for mangled capture slots
    :rtype: FORM
    """

    check_str_arg(slot, "BINDING_CELL slot")
    return slot_prefix(binding_inner_pattern(), slot)


# === Ergonomic shorthands ===


def BIND(slot):
    """
    Capture any single element into the named slot
    """

    return CAPTURE(slot)


def VALUE(slot):
    """
    Capture any single element into the named slot. Synonym for BIND;
    distinguished for forthcoming hygiene-aware lowering in phase 9
    """

    return CAPTURE(slot)


def SYMBOL(slot):
    """
    Capture a symbol element into the named slot. Requires the
    'symbol' extern at match time
    """

    return CAPTURE(slot, TYPE("symbol"))


def KEYWORD(slot):
    """
    Capture a keyword element into the named slot. Requires the
    'keyword' extern at match time
    """

    return CAPTURE(slot, TYPE("keyword"))


def PAIR(slot):
    """
    Capture a pair element into the named slot. Requires the 'pair'
    extern at match time
    """

    return CAPTURE(slot, TYPE("pair"))


def LITERAL_KW(s):
    """
    Match a keyword equal to keyword(s)
    """

    return LITERAL(keyword(s))


def LITERAL_SYM(s):
    """
    Match a symbol equal to symbol(s)
    """

    return LITERAL(symbol(s))


def LIST(*specs, key=None):
    """
    Match a single source element which is a list whose contents
    satisfy the given specs in order. When key is given the matched
    sub-list is also captured into that slot
    """

    inner = FORM(MATCH(*specs))
    if key is None:
        return inner
    return CAPTURE(key, inner)


def make_matcher(pattern):
    """
    Lower a pattern AST into a native Matcher. The matcher carries
    slots, externs, and consts declared by the pattern tree

    :param pattern: a matcher_ng pattern object
    :raises TypeError: when pattern is not a matcher_ng pattern
    :rtype: Matcher
    """

    if not isinstance(pattern, Pattern):
        raise TypeError(
            "make_matcher requires a matcher_ng pattern, got %r"
            % (type(pattern).__name__,))
    return pattern.build()


# === Composite matchers ===


def normalize_scopes(scope, scopes, what):
    """
    Coerce the scope/scopes kwargs into a single tuple of slot names.
    Accepts None for both, a single str for scope, and a tuple/list of
    str for scopes. Mixing both raises ValueError

    :param scope: optional single scope-region slot name
    :param scopes: optional tuple/list of scope-region slot names
    :param what: caller name used in error messages
    :raises TypeError: when scope/scopes have wrong types
    :raises ValueError: when both are supplied or scope is empty
    """

    if scope is not None and scopes is not None:
        raise ValueError(
            "%s accepts scope or scopes, not both" % (what,))
    if scope is not None:
        check_str_arg(scope, "%s scope" % (what,))
        return (scope,)
    if scopes is None:
        return ()
    if not isinstance(scopes, (tuple, list)):
        raise TypeError(
            "%s scopes must be a tuple or list of str, got %r"
            % (what, type(scopes).__name__))
    out = []
    for s in scopes:
        check_str_arg(s, "%s scopes entry" % (what,))
        out.append(s)
    return tuple(out)


class Composite(Pattern):
    """
    Base class for matcher_ng composite matchers. Subclasses return
    a tree of primitive Pattern nodes from _expand; lower on the
    composite lowers that tree. Composites contribute no match-time Python
    behaviour: every semantic concern is encoded structurally
    """


    def lower(self, ctx):
        """
        Emit the expanded primitive pattern
        """

        ctx.on_pattern(self)
        primitive = self._expand()
        return primitive.lower(ctx)


    def _expand(self):
        """
        Subclass responsibility: return the primitive Pattern tree
        that performs the structural match for this composite
        """

        ...


    def visit(self, fn):
        fn(self)
        self._expand().visit(fn)


@runtime_checkable
class HygienicPattern(Protocol):
    """
    Structural contract for patterns that participate in hygiene wire,
    freeze-time registration, and post-match hygiene walks over captures
    """

    def hygiene_wire(self):
        """
        Build a portable hygiene row for this instance
        """

        ...


    @classmethod
    def hygiene_from_wire(cls, row):
        """
        Rebuild an instance from ``hygiene_wire`` output
        """

        ...


    def register_hygiene(self, ctx):
        """
        Record binder or scope-region ownership on a HygienicMatchCompiler
        """

        ...


    def iter_introduced_symbols(self, matched):
        """
        Yield symbols introduced as binders for captures produced by this
        pattern, given a native-style capture mapping from one match
        """

        ...


    def iter_recurse_targets(self, matched):
        """
        Yield nested forms to recurse for binder discovery (initializers,
        type expressions, etc.)
        """

        ...


    def iter_scope_targets(self, matched):
        """
        Yield template roots for lexical scope-region descent (bodies)
        """

        ...


class HygieneWireSlotScopes(object):
    """
    Shared hygiene wire for binder composites keyed by slot and scopes
    tuple
    """


    def hygiene_wire(self):
        """
        Wire row is ``(class name, slot, scopes)``
        """

        return (type(self).__name__, self._slot, self._scopes)


    @classmethod
    def hygiene_from_wire(cls, row):
        """
        Invert ``hygiene_wire`` for the 3-tuple slot-and-scopes shape
        """

        _, slot, scopes = row
        return cls(slot, scopes=scopes)


    def register_hygiene(self, ctx):
        ctx.add_binder(self._slot, self)


    def iter_introduced_symbols(self, matched):
        yield from ()


    def iter_recurse_targets(self, matched):
        yield from ()


    def iter_scope_targets(self, matched):
        yield from ()


class DEFINE(Composite, HygieneWireSlotScopes):
    """
    Capture one element constrained to be a symbol. Used as the
    introduced-name slot for define-shaped specials
    """


    def __init__(self, slot, *, scope=None, scopes=None):
        """
        :param slot: target slot name
        :param scope: optional scope-region slot name
        :param scopes: optional tuple of scope-region slot names
        """

        check_str_arg(slot, "DEFINE slot")
        self._slot = slot
        self._scopes = normalize_scopes(scope, scopes, "DEFINE")


    def _expand(self):
        return CAPTURE(self._slot, TYPE("symbol"))


    def iter_introduced_symbols(self, matched):
        val = matched.get(self._slot)
        if val is None:
            return
        val = val.datum
        if is_symbol(val):
            yield val


class DECL(Composite, HygieneWireSlotScopes):
    """
    Capture one element shaped as a declaration: a bare symbol, a
    one-element list naming the symbol, the (type as: name) form,
    or a (name . tail) form where tail captures the keyword
    attribute pairs verbatim. After a successful match captures[slot]
    is a dict (native GROUP). Keys include ``source`` (Datum of the whole
    declaration cell), ``name`` where applicable, ``type`` from the as-form,
    ``decl_tail`` from the dotted tail arm; other keys are omitted
    """


    def __init__(self, slot, *, scope=None, scopes=None):
        """
        :param slot: target slot name
        :param scope: optional scope-region slot name
        :param scopes: optional tuple of scope-region slot names
        """

        check_str_arg(slot, "DECL slot")
        self._slot = slot
        self._scopes = normalize_scopes(scope, scopes, "DECL")


    def _expand(self):
        return GROUP(self._slot, CAPTURE(FORMALS_SOURCE_KEY, ONEOF(
            TYPE("symbol"),
            FORM(MATCH(SYMBOL("name"))),
            FORM(MATCH(
                VALUE("type"),
                LITERAL_KW("as"),
                SYMBOL("name"))),
            FORM(MATCH(SYMBOL("name"), REST("decl_tail"))),
        )))


    def iter_introduced_symbols(self, matched):
        grp = matched.get(self._slot)
        if grp is None or not isinstance(grp, dict):
            return
        name = grp.get("name")
        if name is not None:
            name = name.datum
            if is_symbol(name):
                yield name
                return
        raw = grp.get(FORMALS_SOURCE_KEY)
        if raw is not None:
            raw = raw.datum
            if is_symbol(raw):
                yield raw


    def iter_recurse_targets(self, matched):
        grp = matched.get(self._slot)
        if grp is None or not isinstance(grp, dict):
            return
        t = grp.get("type")
        if t is not None:
            yield t
        dt = grp.get("decl_tail")
        if dt is not None:
            if is_pair(dt.datum):
                yield dt


class BINDING(Composite, HygieneWireSlotScopes):
    """
    Capture a single binding-shaped cell, (NAME EXPR) or
    (EXPR as: NAME). After a successful match captures[slot] is a dict
    (native GROUP) with ``source`` (Datum of the pair), ``name``, and
    ``expr`` as produced by binding_inner_pattern. Use BINDING_CELL when
    several binding-shaped captures must coexist in one matcher
    """


    def __init__(self, slot, *, scope=None, scopes=None):
        """
        :param slot: target slot name
        :param scope: optional scope-region slot name
        :param scopes: optional tuple of scope-region slot names
        """

        check_str_arg(slot, "BINDING slot")
        self._slot = slot
        self._scopes = normalize_scopes(scope, scopes, "BINDING")


    def _expand(self):
        return GROUP(self._slot, binding_group_capture_pattern())


    def iter_introduced_symbols(self, matched):
        grp = matched.get(self._slot)
        if grp is None or not isinstance(grp, dict):
            return
        name = grp.get("name")
        if name is not None:
            name = name.datum
            if is_symbol(name):
                yield name


    def iter_recurse_targets(self, matched):
        grp = matched.get(self._slot)
        if grp is None or not isinstance(grp, dict):
            return
        ex = grp.get("expr")
        if ex is not None:
            yield ex


class BINDINGS(Composite):
    """
    Capture a binding-list cell: a single element which is either nil
    or a proper list of binding-shaped cells. The nonempty arm uses
    repeat_capture so captures[slot] is a bare Python list of per-cell
    frame dicts (same keys as :class:`BINDING`: ``source``, ``name``,
    ``expr``), not Datum-wrapped; the empty arm matches nil and binds
    captures[slot] = nil (a Datum in the usual CAPTURE sense). The two
    arms share an outer slot name through ONEOF's per-branch capture
    scoping
    """


    def __init__(self, slot, *, mode='parallel', scope=None, scopes=None):
        """
        :param slot: target slot name
        :param mode: stored on the composite instance only, currently
            uninterpreted; legacy callers pass 'parallel' or
            'sequential'
        :param scope: optional scope-region slot name
        :param scopes: optional tuple of scope-region slot names
        """

        check_str_arg(slot, "BINDINGS slot")
        if not isinstance(mode, str):
            raise TypeError(
                "BINDINGS mode must be a str, got %r"
                % (type(mode).__name__,))
        self._slot = slot
        self._mode = mode
        self._scopes = normalize_scopes(scope, scopes, "BINDINGS")


    def _expand(self):
        """
        Match one subject cell (``nil`` or a proper binding list). The repeat
        must run inside that cell via ``FORM`` so we do not treat following
        special-form siblings (e.g. let body) as extra binding rows.
        """

        inner = ONEOF(
            bindings_grouped_list_cell_pattern(),
            binding_group_capture_pattern(),
        )
        return ONEOF(
            CAPTURE(self._slot, LITERAL(nil)),
            FORM(CAPTURE(self._slot, REPEAT(inner))),
        )


    def iter_introduced_symbols(self, matched):
        cells = matched.get(self._slot)
        if cells is None:
            return
        if isinstance(cells, Datum):
            cells = cells.datum
            if cells is nil:
                return
        for fr in cells:
            if isinstance(fr, dict):
                n = fr.get("name")
                if n is not None:
                    if isinstance(n, Datum):
                        n = n.datum
                    if is_symbol(n):
                        yield n


    def iter_recurse_targets(self, matched):
        cells = matched.get(self._slot)
        if cells is None:
            return
        if isinstance(cells, Datum):
            cells = cells.datum
            if cells is nil:
                return
        for fr in cells:
            if isinstance(fr, dict):
                ex = fr.get("expr")
                if ex is not None:
                    yield ex


    def iter_scope_targets(self, matched):
        yield from ()


    def register_hygiene(self, ctx):
        ctx.add_binder(self._slot, self)


    def hygiene_wire(self):
        return ("BINDINGS", self._slot, self._mode, self._scopes)


    @classmethod
    def hygiene_from_wire(cls, row):
        """
        Build from ``hygiene_wire`` output

        :param row: ``("BINDINGS", slot, mode, scopes)``
        :rtype: BINDINGS
        """

        _, slot, mode, scopes = row
        return cls(slot, mode=mode, scopes=scopes)


def formal_param_pattern():
    """
    Build a one-element matcher that recognises a single formal
    parameter cell. Accepted shapes mirror parse_formal_parameter:
    bare symbol, (name), (name default), (type as: name), and
    (name kw1 v1 kw2 v2 ...) with one-or-more keyword/value pairs
    in the tail. Captures inside this pattern appear in the enclosing
    REPEAT frame as 'name', 'default', 'type', and 'attrs' (a list
    of {'kw', 'attr_val'} frame dicts from CAPTURE/VALUE pairs) depending
    on which arm fires
    """

    return ONEOF(
        SYMBOL("name"),
        FORM(MATCH(SYMBOL("name"))),
        FORM(MATCH(SYMBOL("name"), VALUE("default"))),
        FORM(MATCH(VALUE("type"), LITERAL_KW("as"), SYMBOL("name"))),
        FORM(MATCH(
            SYMBOL("name"),
            repeat_capture("attrs", MATCH(
                CAPTURE("kw", TYPE("keyword")),
                VALUE("attr_val"))))),
    )


def kwonly_param_pattern():
    """
    Build a one-element matcher that recognises a single kw-only
    parameter cell. After the *: marker each kw-only entry is either
    a bare symbol (required keyword argument) or a keyword followed
    by a value (defaulted keyword argument). The leading **: marker
    is excluded with a NOT lookahead so the kw-only REPEAT terminates
    cleanly before the optional **: tail
    """

    return MATCH(
        NOT(LITERAL_KW("**")),
        ONEOF(
            FORM(MATCH(SYMBOL("name"))),
            MATCH(SYMBOL("name")),
            MATCH(CAPTURE("kw", TYPE("keyword")), VALUE("default")),
        ),
    )


def default_pair_pattern():
    """
    Build the matcher for one positional-default keyword/value pair
    appearing after the positional run and before *: or **:. The
    leading lookaheads exclude the *: and **: markers so the REPEAT
    of default pairs stops at the first marker keyword
    """

    return MATCH(
        NOT(LITERAL_KW("*")),
        NOT(LITERAL_KW("**")),
        CAPTURE("kw", TYPE("keyword")),
        VALUE("default"),
    )


def formals_inner_pattern():
    """
    Build the proper-list arm of the FORMALS matcher. The list-shape
    decomposes into five optional phases consumed in order:

    * a positional run capturing each formal as a REPEAT frame
    * a default keyword/value run capturing each pair as a REPEAT
      frame (frames hold 'kw' and 'default')
    * an optional *: segment binding either a star symbol or nil
    * a kw-only run capturing each entry as a REPEAT frame (frames
      hold 'name' for bare symbols or 'kw' and 'default' for
      keyword/value pairs)
    * an optional **: segment binding the starstar symbol

    No phase is required; an empty proper list matches with optional
    phases contributing no capture keys
    """

    star_segment = MATCH(
        LITERAL_KW("*"),
        ONEOF(SYMBOL("star"), CAPTURE("star", LITERAL(nil))),
    )

    starstar_segment = MATCH(
        LITERAL_KW("**"),
        SYMBOL("starstar"),
    )

    return FORM(MATCH(
        OPTIONAL(repeat_capture("positionals", formal_param_pattern())),
        OPTIONAL(repeat_capture("defaults", default_pair_pattern())),
        OPTIONAL(star_segment),
        OPTIONAL(repeat_capture("kwonly", kwonly_param_pattern())),
        OPTIONAL(starstar_segment),
    ))


class FORMALS(Composite, HygieneWireSlotScopes):
    """
    Capture one element shaped as a proper-list formals cell and
    decompose its structure into a nested capture dict. Improper-list
    formals are rejected because koskari does not allow them as a
    function-formals shape. After a successful match captures[slot] is
    a dict (native GROUP) whose keys include:

    * 'source': Datum of the whole formals list spine (pair or nil)
    * 'positionals': bare Python list of frame dicts (one per positional formal)
    * 'defaults': bare Python list of frame dicts (one per default keyword/value)
    * 'star': the symbol after *: (or nil for the *: nil marker)
    * 'kwonly': bare Python list of frame dicts (one per kw-only entry)
    * 'starstar': the symbol after **:

    Inner keys for phases that did not run are omitted from the group
    dict. Use one FORMALS per matcher; multi-instance composition
    requires slot_prefix because inner slots share the surrounding
    scope
    """


    def __init__(self, slot, *, scope=None, scopes=None):
        """
        :param slot: target slot name
        :param scope: optional scope-region slot name
        :param scopes: optional tuple of scope-region slot names
        """

        check_str_arg(slot, "FORMALS slot")
        self._slot = slot
        self._scopes = normalize_scopes(scope, scopes, "FORMALS")


    def _expand(self):
        return GROUP(self._slot, CAPTURE(
            FORMALS_SOURCE_KEY, formals_inner_pattern()))


    def iter_introduced_symbols(self, matched):
        grp = matched.get(self._slot)
        if grp is None or not isinstance(grp, dict):
            return
        pos = grp.get("positionals", ())
        for fr in pos:
            if isinstance(fr, dict):
                n = fr.get("name")
                if n is not None:
                    n = n.datum
                    if is_symbol(n):
                        yield n
        star = grp.get("star")
        if star is not None:
            star = star.datum
            if is_symbol(star):
                yield star
        ss = grp.get("starstar")
        if ss is not None:
            ss = ss.datum
            if is_symbol(ss):
                yield ss
        kwonly = grp.get("kwonly", ())
        for fr in kwonly:
            if not isinstance(fr, dict):
                continue
            n = fr.get("name")
            if n is not None:
                n = n.datum
                if is_symbol(n):
                    yield n


    def iter_recurse_targets(self, matched):
        grp = matched.get(self._slot)
        if grp is None or not isinstance(grp, dict):
            return
        defs = grp.get("defaults", ())
        for fr in defs:
            if isinstance(fr, dict):
                dv = fr.get("default")
                if dv is not None:
                    yield dv
        kwonly = grp.get("kwonly", ())
        for fr in kwonly:
            if isinstance(fr, dict):
                dv = fr.get("default")
                if dv is not None:
                    yield dv
        pos = grp.get("positionals", ())
        for fr in pos:
            if isinstance(fr, dict):
                attrs = fr.get("attrs", ())
                for afr in attrs:
                    if isinstance(afr, dict):
                        av = afr.get("attr_val")
                        if av is not None:
                            yield av


class USING(Composite, HygieneWireSlotScopes):
    """
    Capture one element shaped as a using-form. Four inner shapes
    are recognised through an ordered ONEOF; first match wins:

    1. ((PATTERN...) EXPR) -- unpack form, captures lhs and expr
    2. (NAME EXPR)         -- named binding, captures name and expr
    3. (EXPR as: NAME)     -- as form, captures expr and name
    4. (EXPR)              -- bare expression, captures expr only

    After a successful match captures[slot] is a dict (native GROUP)
    with ``source`` (Datum of the whole cell) plus whichever of ``lhs``,
    ``name``, ``expr`` the winning arm binds
    """


    def __init__(self, slot, *, scope=None, scopes=None):
        """
        :param slot: target slot name
        :param scope: optional scope-region slot name
        :param scopes: optional tuple of scope-region slot names
        """

        check_str_arg(slot, "USING slot")
        self._slot = slot
        self._scopes = normalize_scopes(scope, scopes, "USING")


    def _expand(self):
        return GROUP(self._slot, CAPTURE(FORMALS_SOURCE_KEY, FORM(ONEOF(
            MATCH(PAIR("lhs"), VALUE("expr")),
            MATCH(SYMBOL("name"), VALUE("expr")),
            MATCH(VALUE("expr"), LITERAL_KW("as"), SYMBOL("name")),
            MATCH(VALUE("expr")),
        ))))


    def iter_introduced_symbols(self, matched):
        grp = matched.get(self._slot)
        if grp is None or not isinstance(grp, dict):
            return
        name = grp.get("name")
        if name is not None:
            name = name.datum
            if is_symbol(name):
                yield name


    def iter_recurse_targets(self, matched):
        grp = matched.get(self._slot)
        if grp is None or not isinstance(grp, dict):
            return
        ex = grp.get("expr")
        if ex is not None:
            yield ex


class USING_UNPACK(Composite, HygieneWireSlotScopes):
    """
    Capture ``((PATTERN...) EXPR)`` for unpack bindings (e.g. for-each).
    After a successful match captures[slot] is a dict (native GROUP)
    with ``source``, ``lhs``, and ``expr``
    """


    def __init__(self, slot, *, scope=None, scopes=None):
        """
        :param slot: target slot name
        :param scope: optional scope-region slot name
        :param scopes: optional tuple of scope-region slot names
        """

        check_str_arg(slot, "USING_UNPACK slot")
        self._slot = slot
        self._scopes = normalize_scopes(scope, scopes, "USING_UNPACK")


    def _expand(self):
        return GROUP(self._slot, CAPTURE(
            FORMALS_SOURCE_KEY,
            FORM(MATCH(PAIR("lhs"), VALUE("expr")))))


    def iter_introduced_symbols(self, matched):
        grp = matched.get(self._slot)
        if grp is None or not isinstance(grp, dict):
            return
        lhs = grp.get("lhs")
        if lhs is None:
            return
        lhs_v = lhs.datum if isinstance(lhs, Datum) else lhs
        yield from _iter_unpack_lhs_symbols(lhs_v)


    def iter_recurse_targets(self, matched):
        grp = matched.get(self._slot)
        if grp is None or not isinstance(grp, dict):
            return
        ex = grp.get("expr")
        if ex is not None:
            yield ex


def _iter_unpack_lhs_symbols(lhs):
    """
    Yield symbols introduced by an unpack LHS (symbol or proper list)
    """

    if is_symbol(lhs):
        yield lhs
        return
    if not is_pair(lhs):
        return
    if not is_proper(lhs):
        return
    cells = list(unpack(lhs))
    _kw_star = keyword("*")
    star_idx = [i for i, elt in enumerate(cells) if elt == _kw_star]
    if len(star_idx) > 1:
        return
    if not star_idx:
        for elt in cells:
            if is_pair(elt):
                yield from _iter_unpack_lhs_symbols(elt)
            elif is_symbol(elt):
                yield elt
        return
    si = star_idx[0]
    prefix = cells[:si]
    after = cells[si + 1:]
    if not after:
        return
    star_name = after[0]
    suffix = after[1:]
    if not is_symbol(star_name):
        return
    for elt in prefix:
        if is_pair(elt):
            yield from _iter_unpack_lhs_symbols(elt)
        elif is_symbol(elt):
            yield elt
    yield star_name
    for elt in suffix:
        if is_pair(elt):
            yield from _iter_unpack_lhs_symbols(elt)
        elif is_symbol(elt):
            yield elt


class BODY(Pattern):
    """
    Scope-region-aware REST capture. Behaves identically to REST at
    the engine level but additionally registers the slot as a body
    scope region in the compiled Hygiene plan. The capture is one
    lexical extent for hygiene: iter_scope_targets yields that value
    once, not tuple elements
    """


    def __init__(self, slot):
        """
        :param slot: target slot name
        :raises TypeError: when slot is not a str
        :raises ValueError: when slot is empty
        """

        check_str_arg(slot, "BODY slot")
        self._slot = slot


    def lower(self, ctx):
        """
        Emit the underlying REST node
        """

        ctx.on_pattern(self)
        return REST(self._slot).lower(ctx)


    def iter_introduced_symbols(self, matched):
        yield from ()


    def iter_recurse_targets(self, matched):
        yield from ()


    def iter_scope_targets(self, matched):
        """
        Yield the body capture as the single scope-region walk root
        """

        val = matched.get(self._slot)
        if val is None:
            return
        yield val


    def hygiene_wire(self):
        """
        Wire row is ``("BODY", slot)``
        """

        return ("BODY", self._slot)


    @classmethod
    def hygiene_from_wire(cls, row):
        """
        Invert ``hygiene_wire`` for the 2-tuple BODY shape
        """

        _, slot = row
        return cls(slot)


    def register_hygiene(self, ctx):
        ctx.add_scope_region(self._slot, self)


# === Self-describing matcher syntax: registry, meta-matcher, parser ===


CAPTURE_OPTION_KEYS = frozenset({"scope", "mode", "role"})


MATCHER_REGISTRY = {}


def register_matcher(name, factory):
    """
    Register a factory for the surface head name. Raises ValueError on
    collision unless the existing entry is the same callable
    (idempotent re-registration is permitted)

    :param name: head atom string (the symbol name appearing in
        surface position, e.g. 'FORM' or 'BINDING')
    :param factory: callable invoked as factory(head_sym, raw_args)
        returning a matcher_ng pattern object; raw_args is the
        koskari pair-list tail of the surface form
    :raises TypeError: when name is not a str or factory is not
        callable
    :raises ValueError: when name is empty or already registered to
        a different callable
    """

    if not isinstance(name, str):
        raise TypeError(
            "register_matcher name must be a str, got %r"
            % (type(name).__name__,))
    if not name:
        raise ValueError("register_matcher name must be non-empty")
    if not callable(factory):
        raise TypeError(
            "register_matcher factory must be callable, got %r"
            % (type(factory).__name__,))
    existing = MATCHER_REGISTRY.get(name)
    if existing is not None and existing is not factory:
        raise ValueError(
            "matcher %r already registered to a different factory"
            % (name,))
    MATCHER_REGISTRY[name] = factory


def lookup_matcher(name):
    """
    Return the registered factory for name, or None when no factory
    has been registered under that name

    :param name: head atom string
    :rtype: callable or None
    """

    return MATCHER_REGISTRY.get(name)


KW_DCOLON = keyword("::")
KW_TYPE = keyword("type")
KW_VAL = keyword("val")
KW_SCOPE = keyword("scope")
KW_MODE = keyword("mode")
KW_ROLE = keyword("role")


CAPTURE_OPTION_KW_TO_NAME = {
    KW_SCOPE: "scope",
    KW_MODE: "mode",
    KW_ROLE: "role",
}


META_PATTERN = ONEOF(
    FORM(MATCH(
        LITERAL_SYM("quote"),
        CAPTURE("quoted_symbol", TYPE("symbol")))),
    FORM(MATCH(
        LITERAL_KW("::"),
        REST("dcolon_args"))),
    FORM(MATCH(
        LITERAL_KW("type"),
        REST("type_args"))),
    FORM(MATCH(
        LITERAL_KW("val"),
        CAPTURE("val_expr"))),
    FORM(MATCH(
        LITERAL_SYM("CLASS"),
        CAPTURE("class_extern", TYPE("symbol")),
        REST("class_fields"))),
    FORM(MATCH(
        LITERAL_SYM("CLASS"),
        CAPTURE("class_extern", TYPE("symbol")))),
    FORM(MATCH(
        CAPTURE("head", TYPE("symbol")),
        REST("head_args"))),
    FORM(MATCH(CAPTURE("head", TYPE("symbol")))),
    CAPTURE("bare_symbol", TYPE("symbol")),
    CAPTURE("atom_literal"),
)


META_MATCHER = make_matcher(MATCH(META_PATTERN))


def parse_matcher(elem):
    """
    Lower one matcher-syntax element to a matcher_ng pattern object.
    Runs META_MATCHER against (elem,), then dispatches on which
    capture slot is bound. Raises ValueError when elem does not
    satisfy any known surface form

    :param elem: one koskari source element (atom or pair-tree)
    :rtype: Pattern
    """

    if elem is nil:
        raise ValueError(
            "nil is not a valid matcher pattern element")
    result = META_MATCHER.match(
        (elem,), symbol=symbol, keyword=keyword)
    if not result.matched:
        raise ValueError(
            "not a valid matcher pattern: %r" % (elem,))
    c = result.captures

    if "bare_symbol" in c:
        return CAPTURE(str(c["bare_symbol"].datum))

    if "atom_literal" in c:
        atom = c["atom_literal"].datum
        if isinstance(atom, pair):
            raise ValueError(
                "improper list is not a valid matcher pattern: %r"
                % (atom,))
        return LITERAL(atom)

    if "quoted_symbol" in c:
        return LITERAL_SYM(str(c["quoted_symbol"].datum))

    if "dcolon_args" in c:
        return lower_anon_constraint(
            "::", c["dcolon_args"].datum, elem)

    if "type_args" in c:
        return lower_anon_constraint(
            "type:", c["type_args"].datum, elem)

    if "val_expr" in c:
        return LITERAL(c["val_expr"].datum)

    if "class_extern" in c:
        fields = (
            c["class_fields"].datum if "class_fields" in c else nil)
        return lower_class(c["class_extern"].datum, fields)

    if "head" in c:
        head = c["head"].datum
        args = c["head_args"].datum if "head_args" in c else nil
        name = str(head)
        factory = MATCHER_REGISTRY.get(name)
        if factory is not None:
            return factory(head, args)
        return lower_named_capture(head, args)

    raise ValueError(
        "internal: meta-matcher matched %r with no recognised slot bound"
        % (elem,))


def parse_explicit_rule_pattern(form, source_ctx=None):
    """
    Lower MATCH-dialect pattern source (a cons tree) to a matcher_ng Pattern.

    Distinct from :func:`parse_matcher`, which lowers a single META element.
    This walks a rule-level spine: either a registered declaration
    ``(HEAD slot . opts)`` when ``HEAD`` is in :data:`MATCHER_REGISTRY` and
    ``slot`` is a symbol, or a sequential ``MATCH`` of per-cell patterns.

    :param form: koskari pattern datum
    :param source_ctx: optional value appended to error messages
    :return: Pattern instance (often ``MATCH`` wrapping a sequence)
    :raises ValueError: on ellipsis, improper lists, or factory failures
    """

    ctx = "" if source_ctx is None else " (context %r)" % (source_ctx,)

    if form is Ellipsis:
        raise ValueError("invalid ellipsis in MATCH pattern%s" % ctx)

    if not is_pair(form):
        return LITERAL(form)

    if not is_proper(form):
        raise ValueError("MATCH pattern must be a proper list%s" % ctx)

    head, tail = form

    if tail is not nil and is_pair(tail):
        decl_bind = tail.car
        factory = lookup_matcher(str(head))
        if factory is not None and is_symbol(decl_bind):
            return factory(head, tail)

    specs = []
    cur = form
    while cur is not nil:
        first, cur = cur
        specs.append(parse_explicit_rule_pattern(first, source_ctx))
    return MATCH(*specs)


def lower_anon_constraint(tag, args, source):
    """
    Lower (:: pat) or (type: pat) or (val: pat) anonymous-constraint
    forms. For :: and type:, a single symbol payload becomes
    TYPE(name); a single pair payload recurses via parse_matcher.
    For val:, the single payload is wrapped in LITERAL verbatim
    """

    items = list(unpack(args))
    if len(items) != 1:
        raise ValueError(
            "(%s ...) expects exactly one payload, got %d in %r"
            % (tag, len(items), source))
    payload = items[0]
    if tag == "val:":
        return LITERAL(payload)
    if is_symbol(payload):
        return TYPE(str(payload))
    return parse_matcher(payload)


def lower_class(extern_sym, fields):
    """
    Lower (CLASS Sym field-pat...) into CLASS(name, *field-objs).
    Each field element must lower (via parse_matcher) to a FIELD
    instance; non-FIELD lowerings are rejected
    """

    type_name = str(extern_sym)
    field_objs = []
    for f in unpack(fields):
        lowered = parse_matcher(f)
        if not isinstance(lowered, FIELD):
            raise ValueError(
                "(CLASS %s ...) field elements must lower to FIELD,"
                " got %r" % (type_name, type(lowered).__name__))
        field_objs.append(lowered)
    return CLASS(type_name, *field_objs)


def lower_named_capture(name_sym, args):
    """
    Lower (NAME args...) where NAME is not a registered head. Walks
    args, splitting capture options (scope:/mode:/role: keywords)
    from constraints. Inline ::, type:, and val: markers are
    recognised inside the args list as constraint shorthand. The
    returned CAPTURE accepts at most one constraint child; multiple
    constraints raise ValueError
    """

    name = str(name_sym)
    constraints = []
    options = {}
    items = list(unpack(args))
    i = 0
    n = len(items)
    while i < n:
        elem = items[i]
        if is_keyword(elem):
            opt_name = CAPTURE_OPTION_KW_TO_NAME.get(elem)
            if opt_name is not None:
                if i + 1 >= n:
                    raise ValueError(
                        "(%s ...) capture option %s: missing value"
                        % (name, opt_name))
                options[opt_name] = items[i + 1]
                i += 2
                continue
            if elem is KW_DCOLON or elem is KW_TYPE:
                if i + 1 >= n:
                    raise ValueError(
                        "(%s ...) constraint marker missing payload"
                        % (name,))
                payload = items[i + 1]
                if is_symbol(payload):
                    constraints.append(TYPE(str(payload)))
                else:
                    constraints.append(parse_matcher(payload))
                i += 2
                continue
            if elem is KW_VAL:
                if i + 1 >= n:
                    raise ValueError(
                        "(%s ...) val: marker missing payload"
                        % (name,))
                constraints.append(LITERAL(items[i + 1]))
                i += 2
                continue
            raise ValueError(
                "(%s ...) unrecognised keyword %r in capture body"
                % (name, elem))
        constraints.append(parse_matcher(elem))
        i += 1

    if len(constraints) > 1:
        raise ValueError(
            "(%s ...) named capture accepts at most one constraint;"
            " got %d" % (name, len(constraints)))
    sub = constraints[0] if constraints else None

    kwargs = {}
    if "scope" in options:
        scope_val = options["scope"]
        if is_symbol(scope_val):
            kwargs["scope_slot"] = str(scope_val)
        elif isinstance(scope_val, str):
            kwargs["scope_slot"] = scope_val
        else:
            raise ValueError(
                "(%s ...) scope: option must be a symbol or string,"
                " got %r" % (name, type(scope_val).__name__))
    if "role" in options:
        role_val = options["role"]
        if isinstance(role_val, bool) or not isinstance(role_val, int):
            raise ValueError(
                "(%s ...) role: option must be an int, got %r"
                % (name, type(role_val).__name__))
        kwargs["hygiene_role"] = role_val
    if "mode" in options:
        raise ValueError(
            "(%s ...) mode: option only applies to BINDINGS,"
            " not to a named capture" % (name,))

    return CAPTURE(name, sub, **kwargs)


# === Built-in factory helpers ===


def expect_arity(name, items, expected):
    """
    Assert that items has exactly expected length; raise ValueError
    on mismatch
    """

    if len(items) != expected:
        raise ValueError(
            "(%s ...) expects %d argument(s), got %d"
            % (name, expected, len(items)))


def coerce_slot(name, value):
    """
    Coerce a surface slot-name element to a Python str. Accepts a
    koskari symbol or a Python str
    """

    if is_symbol(value):
        return str(value)
    if isinstance(value, str):
        return value
    raise ValueError(
        "(%s ...) slot name must be a symbol or string, got %r"
        % (name, type(value).__name__))


def coerce_str_arg(name, value):
    """
    Coerce a surface single-string arg (used for TYPE, PREDICATE,
    LITERAL_KW, LITERAL_SYM) to a Python str. Accepts symbol, str,
    or a koskari keyword (whose name is used)
    """

    if is_symbol(value) or is_keyword(value):
        return str(value)
    if isinstance(value, str):
        return value
    raise ValueError(
        "(%s ...) argument must be a symbol, keyword, or string,"
        " got %r" % (name, type(value).__name__))


def slot_only_factory(name, ctor):
    """
    Build a factory for a slot-only composite: (NAME slot)
    """

    def factory(head_sym, raw_args):
        items = list(unpack(raw_args))
        expect_arity(name, items, 1)
        return ctor(coerce_slot(name, items[0]))

    factory.__name__ = "factory_%s" % (name,)
    factory.matcher_ng_wire_class = ctor
    return factory


def slot_with_options_factory(name, ctor, allowed_options):
    """
    Build a factory for a slot-first composite that takes additional
    keyword/value option pairs after the slot, e.g.
    (BINDINGS slot mode: parallel)
    """

    def factory(head_sym, raw_args):
        items = list(unpack(raw_args))
        if not items:
            raise ValueError(
                "(%s ...) requires a slot name" % (name,))
        slot = coerce_slot(name, items[0])
        opts = {}
        i = 1
        n = len(items)
        while i < n:
            elem = items[i]
            if not is_keyword(elem):
                raise ValueError(
                    "(%s ...) trailing args must be keyword/value"
                    " pairs, got %r" % (name, elem))
            kw_name = str(elem)
            if kw_name not in allowed_options:
                raise ValueError(
                    "(%s ...) unknown option %r"
                    % (name, elem))
            if i + 1 >= n:
                raise ValueError(
                    "(%s ...) option %r missing value"
                    % (name, elem))
            value = items[i + 1]
            if is_symbol(value):
                value = str(value)
            opts[kw_name] = value
            i += 2
        return ctor(slot, **opts)

    factory.__name__ = "factory_%s" % (name,)
    factory.matcher_ng_wire_class = ctor
    return factory


def unary_recursive_factory(name, ctor):
    """
    Build a factory for a unary-recursive head, e.g. (NOT pat),
    (REPEAT pat), (OPTIONAL pat)
    """

    def factory(head_sym, raw_args):
        items = list(unpack(raw_args))
        expect_arity(name, items, 1)
        return ctor(parse_matcher(items[0]))

    factory.__name__ = "factory_%s" % (name,)
    return factory


def form_factory(head_sym, raw_args):
    """
    Lower (FORM pat) and (FORM pat1 pat2 ...). The single-arg form
    becomes FORM(parse_matcher(pat)); the multi-arg form implicitly
    wraps the children in MATCH per the rollup convention
    """

    items = list(unpack(raw_args))
    if not items:
        raise ValueError("(FORM ...) requires at least one sub-pattern")
    if len(items) == 1:
        return FORM(parse_matcher(items[0]))
    return FORM(MATCH(*[parse_matcher(i) for i in items]))


def match_factory(head_sym, raw_args):
    """
    Lower (MATCH pat...). Empty MATCH is permitted (matches zero
    elements)
    """

    return MATCH(*[parse_matcher(i) for i in unpack(raw_args)])


def group_named_factory(head_sym, raw_args):
    """
    Lower (GROUP slot pat...). slot names the nested capture dict key
    """

    items = list(unpack(raw_args))
    if not items:
        raise ValueError("(GROUP ...) requires a slot name")
    slot = coerce_slot("GROUP", items[0])
    return GROUP(slot, *[parse_matcher(i) for i in items[1:]])


def oneof_factory(head_sym, raw_args):
    """
    Lower (ONEOF pat...). Requires at least one branch (mirrors the
    ONEOF builder)
    """

    items = [parse_matcher(i) for i in unpack(raw_args)]
    if not items:
        raise ValueError("(ONEOF ...) requires at least one branch")
    return ONEOF(*items)


def list_factory(head_sym, raw_args):
    """
    Lower (LIST pat...). LIST itself wraps in FORM(MATCH(...)) so
    extra wrapping here is unnecessary
    """

    return LIST(*[parse_matcher(i) for i in unpack(raw_args)])


def capture_factory(head_sym, raw_args):
    """
    Lower explicit (CAPTURE slot args...). Delegates to
    lower_named_capture so the option/constraint splitting matches
    the implicit named-capture form
    """

    items = list(unpack(raw_args))
    if not items:
        raise ValueError("(CAPTURE ...) requires a slot name")
    slot_sym = items[0]
    if not is_symbol(slot_sym):
        raise ValueError(
            "(CAPTURE ...) slot must be a symbol, got %r"
            % (type(slot_sym).__name__,))
    _, tail = raw_args
    return lower_named_capture(slot_sym, tail)


def literal_factory(head_sym, raw_args):
    """
    Lower (LITERAL value). The value is taken raw without recursion
    through parse_matcher (so symbols and lists become themselves,
    not capture or match patterns)
    """

    items = list(unpack(raw_args))
    expect_arity("LITERAL", items, 1)
    return LITERAL(items[0])


def test_any_factory(head_sym, raw_args):
    """
    Lower (TEST_ANY)
    """

    items = list(unpack(raw_args))
    expect_arity("TEST_ANY", items, 0)
    return TEST_ANY()


def str_arg_factory(name, ctor):
    """
    Build a factory for a head taking exactly one stringy argument
    (TYPE, PREDICATE, LITERAL_KW, LITERAL_SYM)
    """

    def factory(head_sym, raw_args):
        items = list(unpack(raw_args))
        expect_arity(name, items, 1)
        return ctor(coerce_str_arg(name, items[0]))

    factory.__name__ = "factory_%s" % (name,)
    return factory


def field_factory(head_sym, raw_args):
    """
    Lower (FIELD pat) or (FIELD NAME pat). Single-arg form becomes
    a positional field; two-arg form binds the keyword field name
    to NAME (which must be a symbol)
    """

    items = list(unpack(raw_args))
    if len(items) == 1:
        return FIELD(parse_matcher(items[0]))
    if len(items) == 2:
        first = items[0]
        if not is_symbol(first):
            raise ValueError(
                "(FIELD NAME pat) NAME must be a symbol, got %r"
                % (type(first).__name__,))
        return FIELD(parse_matcher(items[1]), name=str(first))
    raise ValueError(
        "(FIELD ...) expects 1 or 2 arguments, got %d" % (len(items),))


def register_builtin_matchers():
    """
    Seed MATCHER_REGISTRY with factories for every built-in surface
    head. Idempotent at module load
    """

    register_matcher("FORM", form_factory)
    register_matcher("MATCH", match_factory)
    register_matcher("GROUP", group_named_factory)
    register_matcher("ONEOF", oneof_factory)
    register_matcher("LIST", list_factory)
    register_matcher("NOT", unary_recursive_factory("NOT", NOT))
    register_matcher("REPEAT", unary_recursive_factory("REPEAT", REPEAT))
    register_matcher(
        "OPTIONAL", unary_recursive_factory("OPTIONAL", OPTIONAL))

    register_matcher("CAPTURE", capture_factory)
    register_matcher("REST", slot_only_factory("REST", REST))
    register_matcher("TAIL", slot_only_factory("TAIL", TAIL))
    register_matcher("BIND", slot_only_factory("BIND", BIND))
    register_matcher("VALUE", slot_only_factory("VALUE", VALUE))
    register_matcher("KEYWORD", slot_only_factory("KEYWORD", KEYWORD))
    register_matcher("SYMBOL", slot_only_factory("SYMBOL", SYMBOL))
    register_matcher("PAIR", slot_only_factory("PAIR", PAIR))
    register_matcher("BODY", slot_only_factory("BODY", BODY))

    register_matcher("DEFINE", slot_only_factory("DEFINE", DEFINE))
    register_matcher("DECL", slot_only_factory("DECL", DECL))
    register_matcher("BINDING", slot_only_factory("BINDING", BINDING))
    register_matcher(
        "BINDINGS",
        slot_with_options_factory(
            "BINDINGS", BINDINGS, frozenset({"mode"})))
    register_matcher("FORMALS", slot_only_factory("FORMALS", FORMALS))
    register_matcher("USING", slot_only_factory("USING", USING))

    register_matcher("LITERAL", literal_factory)
    register_matcher(
        "LITERAL_KW", str_arg_factory("LITERAL_KW", LITERAL_KW))
    register_matcher(
        "LITERAL_SYM", str_arg_factory("LITERAL_SYM", LITERAL_SYM))
    register_matcher("TYPE", str_arg_factory("TYPE", TYPE))
    register_matcher("PREDICATE", str_arg_factory("PREDICATE", PREDICATE))
    register_matcher("TEST_ANY", test_any_factory)
    register_matcher("FIELD", field_factory)


register_builtin_matchers()


KNOWN_HEADS = frozenset(MATCHER_REGISTRY)


# The end.
