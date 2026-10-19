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
koskari.matcher.forms

The composite patterns the special forms declare their shapes with:
the called-by head, definitions and declarations, bindings, formals,
using cells, values and bodies, and the binding and using cell forms
with their matchers

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


from koskari.lib import Datum, nil, symbol
from koskari.types import is_keyword, is_symbol

from .patterns import (
    CAPTURE, FORM, GROUP, LIST, LITERAL, LITERAL_KW, LITERAL_SYM, MATCH, NOT,
    ONEOF, OPTIONAL, PAIR, Pattern, REPEAT, REST, SYMBOL, TYPE, check_str_arg,
    make_matcher, repeat_capture)


__all__ = (
    'BINDING',
    'BINDINGS',
    'BINDING_MATCHERS',
    'BODY',
    'CALLEDBY',
    'DECL',
    'DEFINE',
    'FORMALS',
    'USING',
    'USING_FORMS',
    'USING_MATCHERS',
    'USING_UNPACK',
    'VALUE',
    'frame_formal_name',
)


def binding_inner_pattern():
    """
    Build the inner shape of a single koskari binding cell as matcher
    primitives: FORM around ONEOF of (SYM EXPR) or (EXPR as: SYM).
    The outer FORM descends into one spine element that is already a
    binding pair (SYM EXPR). For a grouped cell such as ``((t 1))`` from
    ``[[t 1]]``, use :func:`bindings_grouped_list_cell_pattern` inside
    :class:`BINDINGS` instead; a single FORM here would yield an inner
    spine of one pair element, and ONEOF would run MATCH(SYMBOL, ...) on
    that pair and fail

    Does not wrap an outer CAPTURE; when embedding multiple binding
    cells in one matcher without duplicate-slot errors, use distinct
    capture slot names, :class:`ONEOF` per-branch capture scopes, or
    separate :class:`GROUP` arms

    :rtype: FORM
    """

    return FORM(ONEOF(
        MATCH(SYMBOL("name"), VALUE("expr")),
        MATCH(VALUE("expr"), LITERAL_KW("as"), SYMBOL("name")),
    ))


def bindings_grouped_list_cell_pattern():
    """
    Match one binding-list element that wraps a single pair in an extra
    list cell, e.g. ``((t 1))`` from ``[[t 1]]``. FORM enters the outer
    list; MATCH consumes the sole inner element; inner FORM enters the
    pair tree for ONEOF(MATCH, MATCH)
    """

    inner = binding_inner_pattern()
    return FORM(MATCH(inner))


# === Composite matchers ===


class Composite(Pattern):
    """
    Base class for matcher composite matchers. Subclasses return
    a tree of primitive Pattern nodes from _expand; lower on the
    composite lowers that tree. Composites contribute no match-time Python
    behaviour: every semantic concern is encoded structurally
    """


    def lower(self, ctx):
        """
        Emit the expanded primitive pattern
        """

        primitive = self._expand()
        return primitive.lower(ctx)


    def _expand(self):
        """
        Subclass responsibility: return the primitive Pattern tree
        that performs the structural match for this composite
        """

        ...


class CALLEDBY(Composite):
    """
    Capture the compile-head symbol into the called-by slot
    """

    def __init__(self, slot=None):
        """
        :param slot: target slot name
        """

        self._slot = slot


    def _expand(self):
        slot = self._slot or "called-by"
        return CAPTURE(slot, TYPE("symbol"))


    def __repr__(self):
        if self._slot is None:
            return "CALLEDBY()"
        return f"CALLEDBY({self._slot!r})"


    def __kos_repr__(self):
        if self._slot is None:
            return "(CALLEDBY)"
        return f"(CALLEDBY {self._slot!r})"


class DEFINE(Composite):
    """
    Capture one element constrained to be a symbol. Used as the
    introduced-name slot for define-shaped specials
    """


    def __init__(self, slot):
        """
        :param slot: target slot name
        """

        check_str_arg(slot, "DEFINE slot")
        self._slot = slot


    def _expand(self):
        return CAPTURE(self._slot, TYPE("symbol"))


    def __repr__(self):
        return f'DEFINE({self._slot!r})'


    def __kos_repr__(self):
        return f'(DEFINE {self._slot})'


class DECL(Composite):
    """
    Capture one element shaped as a declaration: a bare symbol, a
    one-element list naming the symbol, the (type as: name) form,
    or a (name . tail) form where tail captures the keyword
    attribute pairs verbatim. After a successful match captures[slot]
    is a dict (the GROUP's captures) or :class:`DatumFrame` for KosPair-backed
    subjects. Keys include ``name`` where applicable, ``type`` from the
    as-form, ``decl_tail`` from the dotted tail arm; other keys are omitted.
    The whole declaration cell spine is available as ``.source`` on
    :class:`DatumFrame` results (not a dict key)
    """


    def __init__(self, slot):
        """
        :param slot: target slot name
        """

        check_str_arg(slot, "DECL slot")
        self._slot = slot


    def __repr__(self):
        return f'DECL({self._slot!r})'


    def __kos_repr__(self):
        return f'(DECL {self._slot})'


    def _expand(self):
        return GROUP(self._slot, ONEOF(
            TYPE("symbol"),
            FORM(MATCH(SYMBOL("name"))),
            FORM(MATCH(
                VALUE("type"),
                LITERAL_KW("as"),
                SYMBOL("name"))),
            FORM(MATCH(SYMBOL("name"), REST("decl_tail"))),
        ))


class BINDING(Composite):
    """
    Capture a single binding-shaped cell, (NAME EXPR) or
    (EXPR as: NAME). After a successful match captures[slot] is a dict
    (the GROUP's captures) or :class:`DatumFrame` with ``name`` and ``expr`` as
    produced by binding_inner_pattern. For KosPair-backed subjects the
    whole binding cell spine is ``.source`` on :class:`DatumFrame`
    """


    def __init__(self, slot):
        """
        :param slot: target slot name
        """

        check_str_arg(slot, "BINDING slot")
        self._slot = slot


    def _expand(self):
        return GROUP(self._slot, binding_inner_pattern())


    def __repr__(self):
        return f'BINDING({self._slot!r})'


    def __kos_repr__(self):
        return f'(BINDING {self._slot})'


class BINDINGS(Composite):
    """
    Capture a binding-list cell: a single element which is either nil
    or a proper list of binding-shaped cells. The nonempty arm uses
    repeat_capture so captures[slot] is a bare Python list of per-cell
    frame dicts (same keys as :class:`BINDING`: ``name``, ``expr``), not
    Datum-wrapped; the empty arm matches nil and binds
    captures[slot] = nil (a Datum in the usual CAPTURE sense). The two
    arms share an outer slot name through ONEOF's per-branch capture
    scoping
    """


    def __init__(self, slot):
        """
        :param slot: target slot name
        """

        check_str_arg(slot, "BINDINGS slot")
        self._slot = slot


    def __repr__(self):
        return f'BINDINGS({self._slot!r})'


    def __kos_repr__(self):
        return f'(BINDINGS {self._slot})'


    def _expand(self):
        """
        Match one subject cell (``nil`` or a proper binding list). The repeat
        must run inside that cell via ``FORM`` so we do not treat following
        special-form siblings (e.g. let body) as extra binding rows.
        """

        row = GROUP(None, ONEOF(
            bindings_grouped_list_cell_pattern(),
            binding_inner_pattern(),
        ))
        return ONEOF(
            CAPTURE(self._slot, LITERAL(nil)),
            FORM(CAPTURE(self._slot, REPEAT(row))),
        )


def formal_param_pattern():
    """
    Build a one-element matcher that recognises a single formal
    parameter cell. Accepted shapes: bare symbol, (name), (name default), (type as: name),
    (name kw1 v1 kw2 v2 ...) with one-or-more keyword/value pairs
    in the tail, and (name: default kw1 v1 ...) where a keyword heads
    the cell and its value is the default. Captures inside this
    pattern appear in the enclosing REPEAT frame as 'name' (or 'kw'
    for the keyword-headed shape), 'default', 'type', and 'attrs' (a
    list of {'kw', 'attr_val'} frame dicts from CAPTURE/VALUE pairs)
    depending on which arm fires
    """

    attrs = repeat_capture("attrs", MATCH(
        CAPTURE("kw", TYPE("keyword")),
        VALUE("attr_val")))

    return ONEOF(
        SYMBOL("name"),
        FORM(MATCH(SYMBOL("name"))),
        FORM(MATCH(
            LITERAL_SYM("ref"),
            SYMBOL("ref_name"))),
        FORM(MATCH(SYMBOL("name"), VALUE("default"))),
        FORM(MATCH(VALUE("type"), LITERAL_KW("as"), SYMBOL("name"))),
        FORM(MATCH(SYMBOL("name"), attrs)),
        FORM(MATCH(
            CAPTURE("kw", TYPE("keyword")),
            VALUE("default"),
            OPTIONAL(attrs))),
    )


def frame_formal_name(fr):
    """
    Resolve one positional formal frame to (name, by_ref).

    The binding symbol lives under 'ref_name' for the (ref x) and
    (ref x) by-ref shapes. Otherwise it lives under 'name'; a 'name' carrying
    a leading & is the reader-disabled by-ref fallback and is stripped. A
    keyword-headed cell such as (y: 1 :: int) names the parameter under
    'kw' and the symbol that keyword spells is returned; a leading & on
    the keyword marks it by-ref. The returned name is the raw symbol
    (Datum-unwrapped) or None
    """

    v = fr.get("ref_name")
    if v is not None:
        n = v.datum if isinstance(v, Datum) else v
        return n, True

    v = fr.get("name")
    if v is not None:
        n = v.datum if isinstance(v, Datum) else v
        if is_symbol(n) and str(n)[:1] == "&":
            return symbol(str(n)[1:]), True
        return n, False

    v = fr.get("kw")
    if v is None:
        return None, False
    kw = v.datum if isinstance(v, Datum) else v
    if not is_keyword(kw):
        return None, False
    s = str(kw)
    if s[:1] == "&":
        return symbol(s[1:]), True
    return symbol(s), False


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


class FORMALS(Composite):
    """
    Capture one element shaped as a proper-list formals cell and
    decompose its structure into a nested capture dict. Improper-list
    formals are rejected because koskari does not allow them as a
    function-formals shape. After a successful match captures[slot] is
    a dict (the GROUP's captures) or :class:`DatumFrame` whose keys include:

    * 'positionals': bare Python list of frame dicts (one per positional formal)
    * 'defaults': bare Python list of frame dicts (one per default keyword/value)
    * 'star': the symbol after *: (or nil for the *: nil marker)
    * 'kwonly': bare Python list of frame dicts (one per kw-only entry)
    * 'starstar': the symbol after **:

    Inner keys for phases that did not run are omitted from the group
    dict. For KosPair-backed subjects the whole formals list spine is
    ``.source`` on :class:`DatumFrame`. Use one FORMALS per matcher
    """


    def __init__(self, slot):
        """
        :param slot: target slot name
        """

        check_str_arg(slot, "FORMALS slot")
        self._slot = slot


    def _expand(self):
        return GROUP(self._slot, formals_inner_pattern())


    def __repr__(self):
        return f'FORMALS({self._slot!r})'


    def __kos_repr__(self):
        return f'(FORMALS {self._slot})'


class USING(Composite):
    """
    Capture one element shaped as a using-form. Four inner shapes
    are recognised through an ordered ONEOF; first match wins:

    1. (EXPR as: NAME)     -- as form, captures expr and name
    2. ((PATTERN...) EXPR) -- unpack form, captures lhs and expr
    3. (NAME EXPR)         -- named binding, captures name and expr
    4. (EXPR)              -- bare expression, captures expr only

    The as form comes first: a two-element shape is committed to once
    it matches, and the as form's three elements skip it on a
    two-element cell

    After a successful match captures[slot] is a dict (the GROUP's captures) or
    :class:`DatumFrame` with whichever of ``lhs``, ``name``, ``expr`` the
    winning arm binds. For KosPair-backed subjects the whole cell spine is
    ``.source`` on :class:`DatumFrame`
    """


    def __init__(self, slot):
        """
        :param slot: target slot name
        """

        check_str_arg(slot, "USING slot")
        self._slot = slot


    def __repr__(self):
        return f'USING({self._slot!r})'


    def __kos_repr__(self):
        return f'(USING {self._slot})'


    def _expand(self):
        return GROUP(self._slot, FORM(ONEOF(
            MATCH(VALUE("expr"), LITERAL_KW("as"), SYMBOL("name")),
            MATCH(PAIR("lhs"), VALUE("expr")),
            MATCH(SYMBOL("name"), VALUE("expr")),
            MATCH(VALUE("expr")),
        )))


class USING_UNPACK(Composite):
    """
    Capture ``((PATTERN...) EXPR)`` for unpack bindings (e.g. for-each).
    After a successful match captures[slot] is a dict (the GROUP's captures) or
    :class:`DatumFrame` with ``lhs`` and ``expr``. For KosPair-backed
    subjects the whole cell spine is ``.source`` on :class:`DatumFrame`
    """


    def __init__(self, slot):
        """
        :param slot: target slot name
        """

        check_str_arg(slot, "USING_UNPACK slot")
        self._slot = slot


    def __repr__(self):
        return f'USING_UNPACK({self._slot!r})'


    def __kos_repr__(self):
        return f'(USING_UNPACK {self._slot})'


    def _expand(self):
        return GROUP(self._slot, FORM(MATCH(PAIR("lhs"), VALUE("expr"))))


class VALUE(Pattern):
    """
    Single-element capture of a slot holding an expression the form
    evaluates. Lowers to the same ``CAPTURE`` node as
    ``BIND(slot)``; the spelling records the slot's role
    """


    def __init__(self, slot):
        """
        :param slot: target slot name
        :raises TypeError: when slot is not a str
        :raises ValueError: when slot is empty
        """

        check_str_arg(slot, "VALUE slot")
        self._slot = slot


    def lower(self, ctx):
        """
        Emit ``CAPTURE``
        """

        return CAPTURE(self._slot).lower(ctx)


    def __repr__(self):
        return f'VALUE({self._slot!r})'


    def __kos_repr__(self):
        return f'(VALUE {self._slot})'


class BODY(Pattern):
    """
    Remainder capture of a slot holding the forms a body evaluates.
    Lowers to the same ``REST`` node as ``REST(slot, min_tail=1)``
    (minimum one spine cell in the body tail); the spelling records
    the slot's role
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
        Emit ``REST`` with ``min_tail=1``
        """

        return REST(self._slot, min_tail=1).lower(ctx)


    def __repr__(self):
        return f"BODY({self._slot!r})"


    def __kos_repr__(self):
        return f"(BODY {self._slot})"


# === The binding cell forms ===


# the shapes of one binding cell, (SYM EXPR) or (EXPR as: SYM), and of
# one using cell, which also admits a bare (EXPR)
BINDING_FORMS = (
    LIST(VALUE("expr"), LITERAL_KW("as"), SYMBOL("name")),
    LIST(SYMBOL("name"), VALUE("expr")),
)

USING_FORMS = (
    *BINDING_FORMS,
    LIST(VALUE("expr")),
)

# the forms' matchers, built once, for first_match over a one-cell
# spine of the form
BINDING_MATCHERS = tuple(make_matcher(form) for form in BINDING_FORMS)
USING_MATCHERS = tuple(make_matcher(form) for form in USING_FORMS)


# The end.
