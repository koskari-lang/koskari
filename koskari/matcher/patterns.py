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
koskari.matcher.patterns

The pattern builders of the matcher: the Pattern protocol every pattern
implements, the primitive patterns and their shorthands, the result of
Pattern.match, and the one-shot entry points that build a pattern's
matcher function

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


from typing import Protocol, runtime_checkable

from koskari.lib import (
    CoreType, ModuleNamespace, cons, demand, keyword, kos_repr, nil, pair,
    symbol)
from koskari.types import is_keyword, is_nil, is_pair, is_symbol, strip_mark

from .analysis import analyse
from .build import build_matcher
from .tables import TableBuilder


__all__ = (
    'APP',
    'BIND',
    'CAPTURE',
    'CLASS',
    'FIELD',
    'FORM',
    'GROUP',
    'KEYWORD',
    'LIST',
    'LITERAL',
    'LITERAL_KW',
    'LITERAL_SYM',
    'MAP',
    'MAP_ENTRY',
    'MATCH',
    'NOT',
    'ONEOF',
    'OPTIONAL',
    'PAIR',
    'PREDICATE',
    'REPEAT',
    'REST',
    'SAME',
    'SYMBOL',
    'TEST_ANY',
    'TYPE',
    'Pattern',
    'check_pattern_arg',
    'make_matcher',
    'match',
    'match1',
    'match_pattern_raw',
    'pattern_slot_names',
    'repeat_capture',
)


@runtime_checkable
class Pattern(Protocol):
    """
    Structural base for matcher pattern builders. Implementations define
    lower(ctx); the compiler passes itself as ctx when lowering a root
    pattern. GROUP and ONEOF bracket nested lowering with
    begin_nested_claims and end_nested_claims. build lowers through
    TableBuilder and compiles the matcher function
    """


    def lower(self, ctx):
        """
        Append nodes for this pattern into the compiler context and
        return the index of the resulting top node
        """

        ...


    def build(self, externs=None):
        """
        Compile this pattern to its matcher function: one positional
        formal, the subject; the captures on a match, None on a miss.
        Its externs are closure names resolved in a namespace whose
        builtins are the externs mapping

        :param externs: the extern names mapped to their values, or None
        :raises TypeError: when a LITERAL value or a MAP entry key is
            not one the constant pool holds, or an extern the pattern
            declares is not given
        :raises ValueError: when the pattern is one the core refuses
        :rtype: Function
        """

        tables = TableBuilder().lower(self)
        analysis = analyse(tables)
        externs = dict(externs or {})
        for name in tables[1]:
            if name not in externs:
                raise TypeError(f"matcher missing extern binding: {name!r}")
        return build_matcher(
            tables, analysis, ModuleNamespace("matcher", externs))


def check_pattern_arg(arg, what):
    """
    Validate that arg is a Pattern instance and raise a clear TypeError
    when it is not
    """

    if not isinstance(arg, Pattern):
        raise TypeError(
            f"{what} must be a matcher pattern, got {type(arg).__name__!r}")


def check_str_arg(arg, what):
    """
    Validate that arg is a non-empty str. Raises TypeError for the
    wrong type and ValueError for the empty-string case
    """

    if not isinstance(arg, str):
        raise TypeError(
            f"{what} must be a str, got {type(arg).__name__!r}")
    if not arg:
        raise ValueError(f"{what} must be a non-empty str")


def pattern_slot_names(pattern):
    """
    Top-level macro lambda formals for ``pattern`` in declaration order

    Slots nested inside anonymous GROUP / REPEAT row frames are omitted;
    ONEOF branch captures and spine MATCH slots are included
    """

    check_pattern_arg(pattern, "pattern_slot_names pattern")
    mc = TableBuilder()
    mc.lower(pattern)
    return list(mc.formal_slot_names)


# === Primitive builders ===


class SAME(Pattern):
    """
    Match one element equal to an earlier capture of this frame: the
    root, or the GROUP or REPEAT iteration the SAME sits in, at depth
    0; the enclosing frame at depth 1
    """


    def __init__(self, slot, depth=0):
        """
        :param slot: capture slot name; a SAME that reads it while it
            is unbound misses
        :param depth: 0 for current capture dict, 1 for parent GROUP frame
        :raises TypeError: when slot is not a str or depth is not an int
        :raises ValueError: when slot is empty or depth is negative
        """

        check_str_arg(slot, "SAME slot")
        if isinstance(depth, bool) or not isinstance(depth, int):
            raise TypeError(
                f"SAME depth must be an int, got {type(depth).__name__!r}")
        if depth < 0:
            raise ValueError(
                f"SAME depth must be non-negative, got {depth}")
        self._slot = slot
        self._depth = depth


    def lower(self, ctx):
        """
        Emit a SAME node referencing an existing slot index
        """

        slot_idx = ctx.resolve_slot(self._slot, self._depth)
        return ctx.append_node(
            ("SAME", slot_idx, self._depth))


    def __repr__(self):
        if self._depth:
            return f"SAME({self._slot!r}, depth={self._depth})"
        return f"SAME({self._slot!r})"


    def __kos_repr__(self):
        if self._depth:
            return f"(SAME {self._slot} depth: {self._depth})"
        return f"(SAME {self._slot})"


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


    def __repr__(self):
        return "TEST_ANY()"


    def __kos_repr__(self):
        return "(TEST_ANY)"


class LITERAL(Pattern):
    """
    Match a single element equal to the given literal value
    """


    def __init__(self, value):
        """
        Capture the literal value to be referenced through the consts
        table when the pattern is lowered

        :param value: any concrete value, must not be a pattern; an
            atom is held by its bare spelling, since a literal names
            an atom and hygiene renames only bindings
        :raises TypeError: when value is itself a Pattern
        """

        if isinstance(value, Pattern):
            raise TypeError(
                "LITERAL value must be a concrete value, "
                f"not a pattern: {type(value).__name__!r}")
        self._value = strip_mark(value)


    def lower(self, ctx):
        """
        Intern the literal in consts and emit a TEST/LITERAL node
        """

        idx = ctx.intern_const(self._value)
        return ctx.append_node(("TEST", "LITERAL", idx))


    def __repr__(self):
        return f"LITERAL({repr(self._value)})"


    def __kos_repr__(self):
        if is_symbol(self._value):
            return str(self._value)
        if is_keyword(self._value):
            name = str(self._value).rstrip(':')
            return f'(LITERAL_KW "{name}")'
        return f"(LITERAL {kos_repr(self._value)})"


# the types a pattern names by their bare spelling and tests without a
# reference: a MATCH_CORE instruction carries the ordinal
CORE_TYPES = {name.lower(): member for name, member in CoreType.__members__.items()}


class TYPE(Pattern):
    """
    Match a single element which is an instance of the named type: a
    core type by its ordinal, any other name as an extern, a variable of
    the code the pattern runs in
    """


    def __init__(self, extern_name):
        """
        :param extern_name: the extern binding name supplied when the
            matcher is made, e.g. make_matcher(pat, {"Point": Point})
        :raises TypeError: when extern_name is not a str
        :raises ValueError: when extern_name is empty
        """

        check_str_arg(extern_name, "TYPE extern name")
        self._extern = extern_name


    def lower(self, ctx):
        """
        Emit a TEST/CORE node for a core type, else intern the extern
        and emit a TEST/TYPE node
        """

        core = CORE_TYPES.get(self._extern)
        if core is not None:
            return ctx.append_node(("TEST", "CORE", int(core)))
        idx = ctx.intern_extern(self._extern)
        return ctx.append_node(("TEST", "TYPE", idx))


    def __repr__(self):
        return f'TYPE({self._extern!r})'


    def __kos_repr__(self):
        return f"(:: {self._extern})"


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


    def __repr__(self):
        return f'PREDICATE({self._extern!r})'


    def __kos_repr__(self):
        return f"(PREDICATE {self._extern})"


class CAPTURE(Pattern):
    """
    Bind one matched element (or a sub-pattern result) into a named slot
    """


    def __init__(self, slot, sub=None):
        """
        :param slot: target slot name
        :param sub: optional sub-pattern; when None the capture takes
            any single element
        :raises TypeError: when slot is not a str or sub is not a
            pattern or None
        :raises ValueError: when slot is empty
        """

        check_str_arg(slot, "CAPTURE slot")
        if sub is not None:
            check_pattern_arg(sub, "CAPTURE sub")

        self._slot = slot
        self._sub = sub


    def _claim_signature(self):
        """
        The signature a slot claim records, equal for repeated identical
        captures
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
        return ctx.append_node(("CAPTURE", slot_idx, sub_idx))


    def __repr__(self):
        parts = [f"CAPTURE({self._slot!r}"]
        if self._sub is not None:
            parts.append(f"{repr(self._sub)}")
        if len(parts) > 1:
            return ', '.join(parts) + ')'
        return parts[0] + ')'


    def __kos_repr__(self):
        sub = self._sub
        slot = self._slot
        if isinstance(sub, REPEAT):
            inner = sub._sub
            if (isinstance(inner, GROUP)
                    and inner._slot is None
                    and len(inner._children) == 1):
                inner = inner._children[0]
            parts = ['REPEAT', slot, kos_repr(inner)]
            if sub._min_count != 0:
                parts.extend(['min:', str(sub._min_count)])
            return f"({' '.join(parts)})"
        if isinstance(sub, REST):
            parts = ['REST', slot]
            if sub._min_tail != 0:
                parts.extend(['min:', str(sub._min_tail)])
            return f"({' '.join(parts)})"
        parts = [slot]
        if sub is None:
            if len(parts) == 1:
                return slot
            return f"({' '.join(parts)})"
        if isinstance(sub, TYPE):
            parts.extend(['::', sub._extern])
        elif isinstance(sub, LITERAL):
            parts.extend(['::', f"(LITERAL {kos_repr(sub._value)})"])
        else:
            parts.extend(['::', kos_repr(sub)])
        return f"({' '.join(parts)})"


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


    def __repr__(self):
        return f"FORM({repr(self._sub)})"


    def __kos_repr__(self):
        return f"(FORM {kos_repr(self._sub)})"


class APP(Pattern):
    """
    Apply the named extern callable to a single source element and match
    the inner pattern against the call's result. The extern is invoked
    through the demand path at match time, so a plain koskari callable
    works directly; a genuine async suspension raises
    KoskariSyncViolation, and a raising extractor propagates as an
    error, not a no-match
    """


    def __init__(self, extern_name, sub=None):
        """
        :param extern_name: the extractor's extern binding name
        :param sub: optional pattern applied to the call result
        :raises TypeError: when extern_name is not a str or sub is not
            a pattern
        :raises ValueError: when extern_name is empty
        """

        check_str_arg(extern_name, "APP extern name")
        if sub is not None:
            check_pattern_arg(sub, "APP sub")
        self._extern = extern_name
        self._sub = sub


    def lower(self, ctx):
        """
        Intern the extern, emit the inner pattern, then an APP node
        """

        idx = ctx.intern_extern(self._extern)
        sub_idx = self._sub.lower(ctx) if self._sub is not None else -1
        return ctx.append_node(("APP", idx, sub_idx))


    def __repr__(self):
        if self._sub is None:
            return f"APP({self._extern!r})"
        return f"APP({self._extern!r}, {repr(self._sub)})"


    def __kos_repr__(self):
        if self._sub is None:
            return f"(APP {self._extern})"
        return f"(APP {self._extern} {kos_repr(self._sub)})"


class MAP_ENTRY:
    """
    One mapping-pattern entry: a key plus an optional sub-pattern for
    the looked-up value. The key is a literal constant, or -- when
    ``extern`` is true -- an extern name resolved from the namespace at
    match time (the same channel as TYPE / CLASS names). Not a Pattern:
    only meaningful as a child of MAP
    """


    def __init__(self, key, sub=None, extern=False):
        """
        :param key: literal key value, or extern name str when extern
        :param sub: optional pattern applied to the looked-up value
        :param extern: when true, key is an extern binding name
        :raises TypeError: when sub is not a pattern, or extern is set
            and key is not a str
        """

        if extern:
            check_str_arg(key, "MAP_ENTRY extern key")
        if sub is not None:
            check_pattern_arg(sub, "MAP_ENTRY sub")
        self._key = key
        self._sub = sub
        self._extern = extern


    def lower(self, ctx):
        """
        Intern the key, emit the sub-pattern, then a MAP_ENTRY node
        """

        if self._extern:
            tag = "MAP_ENTRY_EXTERN"
            ref = ctx.intern_extern(self._key)
        else:
            tag = "MAP_ENTRY_CONST"
            ref = ctx.intern_const(self._key)
        sub_idx = self._sub.lower(ctx) if self._sub is not None else -1
        return ctx.append_node((tag, ref, sub_idx))


    def __repr__(self):
        parts = [repr(self._key)]
        if self._sub is not None:
            parts.append(repr(self._sub))
        if self._extern:
            parts.append("extern=True")
        return f"MAP_ENTRY({', '.join(parts)})"


class MAP(Pattern):
    """
    Match a single element as a mapping (dict or collections.abc
    Mapping): every entry's key must be present with a value matching
    the entry's sub-pattern. Extra keys are ignored, per PEP 634
    mapping-pattern semantics
    """


    def __init__(self, *entries):
        """
        :param entries: MAP_ENTRY children
        :raises TypeError: when an entry is not a MAP_ENTRY
        """

        for entry in entries:
            if not isinstance(entry, MAP_ENTRY):
                raise TypeError(
                    f"MAP entries must be MAP_ENTRY, got {entry!r}")
        self._entries = entries


    def lower(self, ctx):
        """
        Emit each entry then a MAP node chaining them
        """

        idxs = [entry.lower(ctx) for entry in self._entries]
        return ctx.append_node(("MAP",) + tuple(idxs))


    def __repr__(self):
        inner = ", ".join(repr(entry) for entry in self._entries)
        return f"MAP({inner})"


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


    def __repr__(self):
        if not self._children:
            return "MATCH()"
        return f"MATCH({', '.join(map(repr, self._children))})"


    def __kos_repr__(self):
        if not self._children:
            return "(MATCH)"
        return f"(MATCH {' '.join(map(kos_repr, self._children))})"


class GROUP(Pattern):
    """
    Nested capture frame: inner captures go into a sub-dict. The
    GROUP node has no slot; when ``slot`` is a non-empty str this pattern
    lowers to ``CAPTURE(slot, GROUP(children...))`` so naming stays on
    CAPTURE only. When ``slot`` is None the sub-dict is matched and then
    discarded unless a parent CAPTURE or REPEAT row collects it
    """


    def __init__(self, slot, *children):
        """
        :param slot: dict key for the nested result when lowering wraps
            CAPTURE, or None for an anonymous group
        :param children: zero or more patterns matched in order inside
            the nested dict
        """

        if slot is not None:
            check_str_arg(slot, "GROUP slot")
        for c in children:
            check_pattern_arg(c, "GROUP child")
        self._slot = slot
        self._children = children


    def lower(self, ctx):
        """
        Emit a slot-free GROUP node; when named, wrap with CAPTURE so the
        slot is claimed only on the outer CAPTURE
        """

        if self._slot is not None:
            ctx.claim_slot_name(self._slot, ("group", id(self)))
            slot_idx = ctx.intern_slot(self._slot)
        ctx.begin_nested_claims(inherit_formals=False)
        try:
            idxs = [c.lower(ctx) for c in self._children]
        finally:
            ctx.end_nested_claims()
        g_idx = ctx.append_node(("GROUP",) + tuple(idxs))
        if self._slot is None:
            return g_idx
        return ctx.append_node(("CAPTURE", slot_idx, g_idx))


    def __repr__(self):
        slot = self._slot
        if self._children:
            ch = ', '.join(map(repr, self._children))
            return f"GROUP({slot}, {ch})"
        return f"GROUP({slot})"


    def __kos_repr__(self):
        head = 'nil' if self._slot is None else self._slot
        return f"(GROUP {head} {' '.join(map(kos_repr, self._children))})"


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
        alternatives = {}
        for b in self._branches:
            ctx.begin_nested_claims(alternatives=alternatives)
            try:
                idxs.append(b.lower(ctx))
            finally:
                ctx.end_nested_claims()
        return ctx.append_node(("ONEOF",) + tuple(idxs))


    def __repr__(self):
        return f"ONEOF({', '.join(map(repr, self._branches))})"


    def __kos_repr__(self):
        return f"(ONEOF {' '.join(map(kos_repr, self._branches))})"


class REPEAT(Pattern):
    """
    Greedily match the inner pattern repeatedly; require at least
    ``min_count`` successful iterations (default 0: Kleene star)
    """


    def __init__(self, sub, min_count=0):
        """
        :param sub: pattern repeated against successive elements
        :param min_count: minimum iterations (0 allows zero matches
            when wrapped in CAPTURE / repeat_capture)
        :raises TypeError: when sub is not a pattern or min_count is
            not an int
        :raises ValueError: when min_count is negative
        """

        check_pattern_arg(sub, "REPEAT sub")
        if isinstance(min_count, bool) or not isinstance(min_count, int):
            raise TypeError(
                f"REPEAT min_count must be an int, got {type(min_count).__name__!r}")
        if min_count < 0:
            raise ValueError(
                f"REPEAT min_count must be non-negative, got {min_count}")
        self._sub = sub
        self._min_count = min_count


    def lower(self, ctx):
        """
        Emit the inner pattern then a REPEAT node referencing it
        """

        ctx.begin_nested_claims(boundary=True)
        try:
            sub_idx = self._sub.lower(ctx)
        finally:
            ctx.end_nested_claims()
        return ctx.append_node(("REPEAT", sub_idx, self._min_count))


    def __repr__(self):
        if self._min_count == 0:
            return f"REPEAT({repr(self._sub)})"
        return f"REPEAT({repr(self._sub)}, min_count={self._min_count})"


    def __kos_repr__(self):
        if self._min_count == 0:
            return f"(REPEAT {kos_repr(self._sub)})"
        return f"(REPEAT {kos_repr(self._sub)} min: {self._min_count})"


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


    def __repr__(self):
        return f"OPTIONAL({repr(self._sub)})"


    def __kos_repr__(self):
        return f"(OPTIONAL {kos_repr(self._sub)})"


class REST(Pattern):
    """
    Consume the remainder of the current spine subject. ``min_tail`` is
    the minimum number of spine cells this REST must take (default 0).
    When ``slot`` is set, lowering emits ``CAPTURE(slot, REST(...))`` so
    only CAPTURE carries the slot
    """


    def __init__(self, slot=None, min_tail=0):
        """
        :param slot: optional capture slot name for the tail value
        :param min_tail: minimum spine elements consumed (default 0)
        :raises TypeError: when slot is not None and not str, or
            min_tail is not an int
        :raises ValueError: when slot is empty or min_tail is negative
        """

        if slot is not None:
            check_str_arg(slot, "REST slot")
        if isinstance(min_tail, bool) or not isinstance(min_tail, int):
            raise TypeError(
                f"REST min_tail must be an int, got {type(min_tail).__name__!r}")
        if min_tail < 0:
            raise ValueError(
                f"REST min_tail must be non-negative, got {min_tail}")
        self._slot = slot
        self._min_tail = min_tail


    def lower(self, ctx):
        """
        Emit REST with min_tail only; when named, wrap with CAPTURE
        """

        if self._slot is not None:
            ctx.claim_slot_name(self._slot, ("rest", id(self)))
            slot_idx = ctx.intern_slot(self._slot)
        rest_idx = ctx.append_node(("REST", self._min_tail))
        if self._slot is None:
            return rest_idx
        return ctx.append_node(("CAPTURE", slot_idx, rest_idx))


    def __repr__(self):
        if self._slot is None and self._min_tail == 0:
            return "REST()"
        if self._slot is None:
            return f"REST(min_tail={self._min_tail})"
        if self._min_tail == 0:
            return f"REST({self._slot})"
        return f"REST({self._slot}, min_tail={self._min_tail})"


    def __kos_repr__(self):
        if self._slot is None:
            if self._min_tail == 0:
                return "(REST)"
            return f"(REST min: {self._min_tail})"
        if self._min_tail != 0:
            return f"(REST {self._slot} min: {self._min_tail})"
        return f"(REST {self._slot})"


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


    def __repr__(self):
        return f"NOT({repr(self._sub)})"


    def __kos_repr__(self):
        return f"(NOT {kos_repr(self._sub)})"


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
                    f"got {f!r}")
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


    def __repr__(self):
        if not self._fields:
            return f"CLASS({self._extern!r})"
        return f"CLASS({self._extern!r}, {', '.join(map(repr, self._fields))})"


    def __kos_repr__(self):
        if not self._fields:
            return f"(CLASS {self._extern})"
        return f"(CLASS {self._extern} {' '.join(map(kos_repr, self._fields))})"


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


    def __repr__(self):
        if self._name is None:
            return f"FIELD({repr(self._sub)})"
        return f"FIELD({repr(self._sub)}, name={self._name!r})"


    def __kos_repr__(self):
        if self._name is None:
            return f"(FIELD {kos_repr(self._sub)})"
        return f"(FIELD {kos_repr(self._sub)} {self._name})"


def repeat_capture(slot, inner):
    """
    Capture the results of repeating inner across successive subject
    elements. captures[slot] is a Python list: each element is one
    iteration result (often a dict for a GROUP row dict, or a sparse
    change dict / Datum when inner does not use that shape)

    :param slot: outer slot receiving the list of per-iteration values
    :param inner: pattern run once per repetition (often FORM around
        a MATCH of captures)
    :rtype: CAPTURE
    """

    check_str_arg(slot, "repeat_capture slot")
    check_pattern_arg(inner, "repeat_capture inner")
    if isinstance(inner, REPEAT):
        sub = inner._sub
        mc = inner._min_count
        return CAPTURE(
            slot,
            REPEAT(GROUP(None, sub), min_count=mc))
    return CAPTURE(slot, REPEAT(GROUP(None, inner)))


# === Ergonomic shorthands ===


def BIND(slot):
    """
    Capture any single element into the named slot
    """

    return CAPTURE(slot)


def SYMBOL(slot):
    """
    Capture a symbol element into the named slot; symbol is a core type,
    tested with no extern
    """

    return CAPTURE(slot, TYPE("symbol"))


def KEYWORD(slot):
    """
    Capture a keyword element into the named slot; keyword is a core
    type, tested with no extern
    """

    return CAPTURE(slot, TYPE("keyword"))


def PAIR(slot):
    """
    Capture a pair element into the named slot; pair is a core type,
    tested with no extern
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


# === One-shot matching ===


def match_pattern_raw(source, patterns):
    """
    Try each matcher pattern against ``source`` (pair spine or ``nil``).
    Return the captures of the first match, else ``None``
    """

    if not is_nil(source) and not is_pair(source):
        raise TypeError(
            f"match source must be nil or a pair, got {source!r}")

    if not isinstance(patterns, (list, tuple)):
        patterns = [patterns]

    for pat in patterns:
        if not isinstance(pat, Pattern):
            raise TypeError(
                f"match_pattern_raw expects a matcher Pattern, got {pat!r}")
        captures = demand(pat.build())(source)
        if captures is not None:
            return captures

    return None


def match(source, patterns):
    """
    Try each matcher pattern against ``source`` (pair spine or ``nil``).
    Returns the captures of the first match, else ``None``.
    """

    return match_pattern_raw(source, patterns)


def match1(value, patterns):
    """
    Match a single element by wrapping ``cons(value, nil)``
    """

    return match(cons(value, nil), patterns)


def make_matcher(pattern, externs=None):
    """
    Compile a pattern AST to its matcher function, as
    :meth:`Pattern.build` does

    :param pattern: a matcher pattern object
    :param externs: the extern names mapped to their values, or None
    :raises TypeError: when pattern is not a matcher pattern
    :rtype: Function
    """

    if isinstance(pattern, (pair, symbol)):
        # the dialect builds on these patterns, so it is read at the call
        from .heads import parse_matcher
        pattern = parse_matcher(pattern)

    if not isinstance(pattern, Pattern):
        raise TypeError(
            f"make_matcher requires a matcher pattern, got {type(pattern).__name__!r}")

    return pattern.build(externs)


# The end.
