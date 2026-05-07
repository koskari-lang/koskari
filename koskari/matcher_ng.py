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
in Python that lower to the same frozen form. Named repeat captures
(CAPTURE wrapping REPEAT) produce per-iteration frame dicts; see
repeat_capture and binding_inner_pattern

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude Opus 4.7 via Cursor
"""


from koskari.lib import ABSENT, Matcher, MatchResult, keyword, symbol


__all__ = [
    "ABSENT",
    "Matcher",
    "MatchResult",

    "thaw",
    "make_matcher",

    "TEST_ANY",
    "LITERAL",
    "TYPE",
    "PREDICATE",
    "CAPTURE",
    "FORM",
    "GROUP",
    "ONEOF",
    "REPEAT",
    "OPTIONAL",
    "REST",
    "NOT",
    "CLASS",
    "FIELD",

    "BIND",
    "VALUE",
    "SYMBOL",
    "PAIR",
    "LITERAL_KW",
    "LITERAL_SYM",
    "LIST",

    "repeat_capture",
    "slot_prefix",
    "binding_inner_pattern",
    "BINDING_CELL",
]


def thaw(tagged_tuple):
    """
    Reconstruct a native Matcher from its frozen tagged-tuple
    representation
    """

    return Matcher.from_tuple(tagged_tuple)


# === Builder infrastructure ===


class _FreezeCtx(object):
    """
    Mutable state shared across a single freeze pass. Holds the four
    insertion-ordered tables that become the slots, externs, consts, and
    nodes positions of the frozen 8-tuple, plus bookkeeping used by the
    eager-validation checks
    """


    def __init__(self):
        """
        Construct an empty freeze context
        """

        self.slot_indices = {}
        self.extern_indices = {}
        self.consts = []
        self.nodes = []
        self.capture_scope_stack = [set()]
        self.in_class_fields = False


    def push_capture_scope(self):
        """
        Begin an isolated duplicate-slot scope (used per ONEOF branch)
        """

        self.capture_scope_stack.append(set())


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
        Return the slot index for the named slot, allocating one when
        first seen
        """

        if name in self.slot_indices:
            return self.slot_indices[name]

        idx = len(self.slot_indices)
        self.slot_indices[name] = idx
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


    def claim_slot_name(self, name):
        """
        Reject duplicate slot bindings within the current capture scope.
        ONEOF branches use a fresh scope per branch so alternate arms may
        reuse the same slot names
        """

        top = self.capture_scope_stack[-1]
        if name in top:
            raise ValueError(
                "duplicate slot name %r in matcher pattern" % (name,))
        top.add(name)


class _Pattern(object):
    """
    Base class for matcher_ng pattern builders. Subclasses implement
    _emit to append their node tuple to a freeze context and return the
    resulting node index. The freeze, build, and match helpers are the
    public lowering surface
    """


    def freeze(self, meta=None):
        """
        Lower this pattern to a frozen 8-tuple suitable for
        Matcher.from_tuple
        """

        ctx = _FreezeCtx()
        entry = self._emit(ctx)

        if meta is None:
            meta = {}
        elif not isinstance(meta, dict):
            raise TypeError(
                "meta must be a dict, got %r" % type(meta).__name__)

        return (
            "MATCHER", 1,
            tuple(self._slot_names(ctx)),
            tuple(self._extern_names(ctx)),
            tuple(ctx.consts),
            tuple(ctx.nodes),
            entry,
            meta,
        )


    def build(self, meta=None):
        """
        Lower this pattern and thaw it into a native Matcher
        """

        return Matcher.from_tuple(self.freeze(meta))


    def match(self, subject, **externs):
        """
        Convenience: build and match against subject
        """

        return self.build().match(subject, **externs)


    def _emit(self, ctx):
        """
        Subclass responsibility: emit nodes for this pattern into the
        context and return the index of the resulting top node
        """

        raise NotImplementedError


    @staticmethod
    def _slot_names(ctx):
        """
        Return slot names in declaration order
        """

        return list(ctx.slot_indices.keys())


    @staticmethod
    def _extern_names(ctx):
        """
        Return extern names in declaration order
        """

        return list(ctx.extern_indices.keys())


def _check_pattern_arg(arg, what):
    """
    Validate that arg is a _Pattern instance and raise a clear TypeError
    when it is not
    """

    if not isinstance(arg, _Pattern):
        raise TypeError(
            "%s must be a matcher_ng pattern, got %r"
            % (what, type(arg).__name__))


def _check_str_arg(arg, what, *, allow_empty=False):
    """
    Validate that arg is a non-empty (by default) str. Raises TypeError
    for the wrong type and ValueError for the empty-string case
    """

    if not isinstance(arg, str):
        raise TypeError(
            "%s must be a str, got %r" % (what, type(arg).__name__))
    if not allow_empty and not arg:
        raise ValueError("%s must be a non-empty str" % (what,))


# === Primitive builders ===


class TEST_ANY(_Pattern):
    """
    Match any single element
    """


    def __init__(self):
        """
        TEST_ANY takes no parameters
        """

        pass


    def _emit(self, ctx):
        """
        Emit the TEST_ANY node tuple
        """

        return ctx.append_node(("TEST", "ANY", -1))


class LITERAL(_Pattern):
    """
    Match a single element equal to the given literal value
    """


    def __init__(self, value):
        """
        Capture the literal value to be referenced through the consts
        table at freeze time

        :param value: any concrete value, must not be a pattern
        :raises TypeError: when value is itself a _Pattern
        """

        if isinstance(value, _Pattern):
            raise TypeError(
                "LITERAL value must be a concrete value, "
                "not a pattern: %r" % (type(value).__name__,))
        self._value = value


    def _emit(self, ctx):
        """
        Intern the literal in consts and emit a TEST/LITERAL node
        """

        idx = ctx.intern_const(self._value)
        return ctx.append_node(("TEST", "LITERAL", idx))


class TYPE(_Pattern):
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

        _check_str_arg(extern_name, "TYPE extern name")
        self._extern = extern_name


    def _emit(self, ctx):
        """
        Intern the extern and emit a TEST/TYPE node
        """

        idx = ctx.intern_extern(self._extern)
        return ctx.append_node(("TEST", "TYPE", idx))


class PREDICATE(_Pattern):
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

        _check_str_arg(extern_name, "PREDICATE extern name")
        self._extern = extern_name


    def _emit(self, ctx):
        """
        Intern the extern and emit a TEST/PREDICATE node
        """

        idx = ctx.intern_extern(self._extern)
        return ctx.append_node(("TEST", "PREDICATE", idx))


class CAPTURE(_Pattern):
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

        _check_str_arg(slot, "CAPTURE slot")
        if sub is not None:
            _check_pattern_arg(sub, "CAPTURE sub")
        if scope_slot is not None:
            _check_str_arg(scope_slot, "CAPTURE scope_slot")
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


    def _emit(self, ctx):
        """
        Reserve the slot, lower the optional sub-pattern, and emit the
        CAPTURE node
        """

        ctx.claim_slot_name(self._slot)
        slot_idx = ctx.intern_slot(self._slot)
        if self._sub is None:
            sub_idx = -1
        else:
            sub_idx = self._sub._emit(ctx)
        if self._scope_slot is None:
            scope_idx = -1
        else:
            scope_idx = ctx.intern_slot(self._scope_slot)
        return ctx.append_node(
            ("CAPTURE", slot_idx, sub_idx,
             self._hygiene_role, scope_idx))


class FORM(_Pattern):
    """
    Descend into a single source element treated as a pair-tree and
    apply the inner pattern against its contents
    """


    def __init__(self, sub):
        """
        :param sub: pattern applied to the inner sequence
        :raises TypeError: when sub is not a pattern
        """

        _check_pattern_arg(sub, "FORM sub")
        self._sub = sub


    def _emit(self, ctx):
        """
        Emit the inner pattern then a FORM node referencing it
        """

        sub_idx = self._sub._emit(ctx)
        return ctx.append_node(("FORM", sub_idx))


class GROUP(_Pattern):
    """
    Sequence of patterns matched in order against successive elements
    """


    def __init__(self, *children):
        """
        :param children: zero or more patterns
        :raises TypeError: when any child is not a pattern
        """

        for c in children:
            _check_pattern_arg(c, "GROUP child")
        self._children = children


    def _emit(self, ctx):
        """
        Emit each child then a GROUP node listing them in order
        """

        idxs = [c._emit(ctx) for c in self._children]
        return ctx.append_node(("GROUP",) + tuple(idxs))


class ONEOF(_Pattern):
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
            _check_pattern_arg(b, "ONEOF branch")
        self._branches = branches


    def _emit(self, ctx):
        """
        Emit each branch then a ONEOF node listing them in order. Each
        branch gets its own capture scope so identical slot names across
        mutually exclusive arms remain valid
        """

        idxs = []
        for b in self._branches:
            ctx.push_capture_scope()
            try:
                idxs.append(b._emit(ctx))
            finally:
                ctx.pop_capture_scope()
        return ctx.append_node(("ONEOF",) + tuple(idxs))


class REPEAT(_Pattern):
    """
    Greedily match the inner pattern one-or-more times
    """


    def __init__(self, sub):
        """
        :param sub: pattern repeated against successive elements
        :raises TypeError: when sub is not a pattern
        """

        _check_pattern_arg(sub, "REPEAT sub")
        self._sub = sub


    def _emit(self, ctx):
        """
        Emit the inner pattern then a REPEAT node referencing it
        """

        sub_idx = self._sub._emit(ctx)
        return ctx.append_node(("REPEAT", sub_idx))


class OPTIONAL(_Pattern):
    """
    Match the inner pattern at most once. When the inner pattern is
    absent any captures it would have made resolve to the ABSENT
    sentinel
    """


    def __init__(self, sub):
        """
        :param sub: pattern that may or may not match
        :raises TypeError: when sub is not a pattern
        """

        _check_pattern_arg(sub, "OPTIONAL sub")
        self._sub = sub


    def _emit(self, ctx):
        """
        Emit the inner pattern then an OPTIONAL node referencing it
        """

        sub_idx = self._sub._emit(ctx)
        return ctx.append_node(("OPTIONAL", sub_idx))


class REST(_Pattern):
    """
    Capture the remainder of the source list into the named slot
    """


    def __init__(self, slot):
        """
        :param slot: target slot name
        :raises TypeError: when slot is not a str
        :raises ValueError: when slot is empty
        """

        _check_str_arg(slot, "REST slot")
        self._slot = slot


    def _emit(self, ctx):
        """
        Reserve the slot and emit a REST node
        """

        ctx.claim_slot_name(self._slot)
        slot_idx = ctx.intern_slot(self._slot)
        return ctx.append_node(("REST", slot_idx))


class NOT(_Pattern):
    """
    Negative lookahead: succeed when the inner pattern does not match
    the current single element. Width 1 with no captures inside
    """


    def __init__(self, sub):
        """
        :param sub: pattern whose match would cause NOT to fail
        :raises TypeError: when sub is not a pattern
        """

        _check_pattern_arg(sub, "NOT sub")
        self._sub = sub


    def _emit(self, ctx):
        """
        Emit the inner pattern then a NOT node referencing it
        """

        sub_idx = self._sub._emit(ctx)
        return ctx.append_node(("NOT", sub_idx))


class CLASS(_Pattern):
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

        _check_str_arg(type_extern, "CLASS type_extern")
        for f in fields:
            if not isinstance(f, FIELD):
                raise TypeError(
                    "CLASS field arguments must be FIELD instances, "
                    "got %r" % (type(f).__name__,))
        self._extern = type_extern
        self._fields = fields


    def _emit(self, ctx):
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
            field_idxs = [f._emit(ctx) for f in self._fields]
        finally:
            ctx.in_class_fields = prev
        return ctx.append_node(
            ("CLASS", ext_idx) + tuple(field_idxs))


class FIELD(_Pattern):
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

        _check_pattern_arg(sub, "FIELD sub")
        if name is not None:
            _check_str_arg(name, "FIELD name")
        self._sub = sub
        self._name = name


    def _emit(self, ctx):
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
            sub_idx = self._sub._emit(ctx)
        finally:
            ctx.in_class_fields = prev

        if self._name is None:
            return ctx.append_node(("FIELD_POS", sub_idx))
        return ctx.append_node(("FIELD_KW", sub_idx, self._name))


def repeat_capture(slot, inner):
    """
    Capture the results of repeating inner across successive subject
    elements. The native engine stores matched.captures[slot] as a
    list of dict objects: each dict maps slot names touched during
    one iteration to their captured values (ABSENT for unset slots).
    Inner patterns should use CAPTURE nodes for fields to collect per
    iteration

    :param slot: outer slot receiving the list of frame dicts
    :param inner: pattern run once per repetition (often FORM around
        a GROUP of captures)
    :rtype: CAPTURE
    """

    _check_str_arg(slot, "repeat_capture slot")
    _check_pattern_arg(inner, "repeat_capture inner")
    return CAPTURE(slot, REPEAT(inner))


def slot_prefix(pattern, prefix, sep="__"):
    """
    Return a copy of pattern with every nested CAPTURE and REST slot
    renamed to prefix + sep + original, and CAPTURE scope_slot likewise
    prefixed when present. Use before composing the same binding-shaped
    subtree multiple times so freeze-time duplicate-slot detection does
    not fire

    :param pattern: matcher_ng pattern tree
    :param prefix: non-empty prefix string
    :param sep: separator between prefix and original slot name
    :raises TypeError: when pattern is not a matcher_ng pattern
    :raises ValueError: when prefix is empty
    """

    _check_pattern_arg(pattern, "slot_prefix pattern")
    _check_str_arg(prefix, "slot_prefix prefix")
    return _slot_prefix_copy(pattern, prefix, sep)


def _slot_prefix_copy(pattern, prefix, sep):
    """
    Recursive implementation for slot_prefix
    """

    if isinstance(pattern, CAPTURE):
        new_scope = None
        if pattern._scope_slot is not None:
            new_scope = prefix + sep + pattern._scope_slot
        sub = (_slot_prefix_copy(pattern._sub, prefix, sep)
               if pattern._sub is not None else None)
        return CAPTURE(
            prefix + sep + pattern._slot,
            sub,
            hygiene_role=pattern._hygiene_role,
            scope_slot=new_scope,
        )

    if isinstance(pattern, FORM):
        return FORM(_slot_prefix_copy(pattern._sub, prefix, sep))

    if isinstance(pattern, GROUP):
        return GROUP(*[
            _slot_prefix_copy(c, prefix, sep) for c in pattern._children])

    if isinstance(pattern, ONEOF):
        return ONEOF(*[
            _slot_prefix_copy(b, prefix, sep) for b in pattern._branches])

    if isinstance(pattern, REPEAT):
        return REPEAT(_slot_prefix_copy(pattern._sub, prefix, sep))

    if isinstance(pattern, OPTIONAL):
        return OPTIONAL(_slot_prefix_copy(pattern._sub, prefix, sep))

    if isinstance(pattern, NOT):
        return NOT(_slot_prefix_copy(pattern._sub, prefix, sep))

    if isinstance(pattern, REST):
        return REST(prefix + sep + pattern._slot)

    if isinstance(pattern, CLASS):
        new_fields = tuple(
            FIELD(_slot_prefix_copy(f._sub, prefix, sep), name=f._name)
            for f in pattern._fields)
        return CLASS(pattern._extern, *new_fields)

    if isinstance(pattern, FIELD):
        return FIELD(_slot_prefix_copy(pattern._sub, prefix, sep),
                     name=pattern._name)

    if isinstance(pattern, (TEST_ANY, LITERAL, TYPE, PREDICATE)):
        return pattern

    raise TypeError(
        "slot_prefix: unsupported pattern type %r"
        % (type(pattern).__name__,))


def binding_inner_pattern():
    """
    Build the inner shape of a single koskari binding cell as matcher_ng
    primitives: FORM around ONEOF of (SYM EXPR) or (EXPR as: SYM).
    Does not wrap an outer CAPTURE; compose with FORM's subject being
    one binding pair. When embedding multiple binding cells in one
    matcher, pass the result through slot_prefix with a unique prefix
    per cell first

    :rtype: FORM
    """

    return FORM(ONEOF(
        GROUP(SYMBOL("name"), VALUE("expr")),
        GROUP(VALUE("expr"), LITERAL_KW("as"), SYMBOL("name")),
    ))


def BINDING_CELL(slot):
    """
    One binding-shaped subject element: same as binding_inner_pattern
    but with captures rewritten to slot + sep + name and slot + sep +
    expr so multiple cells can coexist in one matcher

    :param slot: logical name prefix for mangled capture slots
    :rtype: FORM
    """

    _check_str_arg(slot, "BINDING_CELL slot")
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

    inner = FORM(GROUP(*specs))
    if key is None:
        return inner
    return CAPTURE(key, inner)


def make_matcher(pattern, *, meta=None):
    """
    Lower a pattern AST into a native Matcher

    :param pattern: a matcher_ng pattern object
    :param meta: optional dict of metadata to attach to the matcher
    :raises TypeError: when pattern is not a matcher_ng pattern
    """

    if not isinstance(pattern, _Pattern):
        raise TypeError(
            "make_matcher requires a matcher_ng pattern, got %r"
            % (type(pattern).__name__,))
    return pattern.build(meta=meta)


# The end.
