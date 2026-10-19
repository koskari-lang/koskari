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
koskari.matcher.heads

The explicit matcher dialect: the registry of matcher heads, the
meta-pattern that classifies one element of matcher syntax, and the
parsers and factories that lower matcher syntax to patterns

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


from koskari.lib import cons, demand, keyword, nil, pair
from koskari.types import (
    is_keyword, is_nil, is_pair, is_proper, is_symbol, strip_mark, unpack)

from .forms import BINDING, BINDINGS, BODY, DECL, DEFINE, FORMALS, USING, VALUE
from .patterns import (
    APP, BIND, CAPTURE, CLASS, FIELD, FORM, GROUP, KEYWORD, LIST, LITERAL,
    LITERAL_KW, LITERAL_SYM, MATCH, NOT, ONEOF, OPTIONAL, PAIR, PREDICATE,
    REPEAT, REST, SYMBOL, TEST_ANY, TYPE, make_matcher, repeat_capture)


__all__ = (
    'parse_explicit_rule_pattern',
)


MATCHER_REGISTRY = {}


def register_matcher(name, factory):
    """
    Register a factory for the surface head name. Raises ValueError on
    collision unless the existing entry is the same callable
    (idempotent re-registration is permitted)

    :param name: head atom string (the symbol name appearing in
        surface position, e.g. 'FORM' or 'BINDING')
    :param factory: callable invoked as factory(head_sym, raw_args)
        returning a matcher pattern object; raw_args is the
        koskari pair-list tail of the surface form
    :raises TypeError: when name is not a str or factory is not
        callable
    :raises ValueError: when name is empty or already registered to
        a different callable
    """

    if not isinstance(name, str):
        raise TypeError(
            f"register_matcher name must be a str, got {name!r}")
    if not name:
        raise ValueError("register_matcher name must be non-empty")
    if not callable(factory):
        raise TypeError(
            f"register_matcher factory must be callable, got {factory!r}")
    existing = MATCHER_REGISTRY.get(name)
    if existing is not None and existing is not factory:
        raise ValueError(
            f"matcher {name!r} already registered to a different factory")
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
KW_STAR = keyword("*")


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
        CAPTURE("head", TYPE("symbol")),
        REST("head_args"))),
    CAPTURE("bare_symbol", TYPE("symbol")),
    CAPTURE("atom_literal"),
)


# the matcher functions the parsers run from Python, each wrapped with
# demand once
META_MATCHER = demand(make_matcher(MATCH(META_PATTERN)))


def parse_matcher(elem):
    """
    Lower one matcher-syntax element to a matcher pattern object.
    Runs META_MATCHER against (elem,), then dispatches on which
    capture slot is bound. Raises ValueError when elem does not
    satisfy any known surface form

    :param elem: one koskari source element (atom or pair-tree)
    :rtype: Pattern
    """

    if is_nil(elem):
        raise ValueError(
            "nil is not a valid matcher pattern element")
    # the element is source, so it is walked as a one-cell spine:
    # captures come back as Datum with position intact
    c = META_MATCHER(cons(elem, nil))
    if c is None:
        raise ValueError(
            f"not a valid matcher pattern: {elem!r}")

    if "bare_symbol" in c:
        return CAPTURE(str(c["bare_symbol"].datum))

    if "atom_literal" in c:
        atom = c["atom_literal"].datum
        if isinstance(atom, pair):
            raise ValueError(
                f"improper list is not a valid matcher pattern: {atom!r}")
        return LITERAL(atom)

    if "quoted_symbol" in c:
        return LITERAL_SYM(str(c["quoted_symbol"].datum.base))

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
        f"internal: meta-matcher matched {elem!r} with no recognised slot bound")


def parse_explicit_rule_pattern(form, source_ctx=None):
    """
    Lower MATCH-dialect pattern source (a cons tree) to a matcher Pattern.

    Distinct from :func:`parse_matcher`, which lowers a single META element.
    This walks a rule-level spine: either a registered declaration
    ``(HEAD slot . opts)`` when ``HEAD`` is in :data:`MATCHER_REGISTRY` and
    ``slot`` is a symbol, or a sequential ``MATCH`` of per-cell patterns.

    :param form: koskari pattern datum
    :param source_ctx: optional value appended to error messages
    :return: Pattern instance (often ``MATCH`` wrapping a sequence)
    :raises ValueError: on ellipsis, improper lists, empty cells, or
        factory failures
    """

    ctx = "" if source_ctx is None else f" (context {source_ctx!r})"

    if form is Ellipsis:
        raise ValueError(f"invalid ellipsis in MATCH pattern{ctx}")

    if not is_pair(form):
        return LITERAL(form)

    if not is_proper(form):
        raise ValueError(f"MATCH pattern must be a proper list{ctx}")

    # nil is a pair and a proper list; as a cell it would be an empty
    # inline group, which matches nothing and names nothing
    if is_nil(form):
        raise ValueError(f"empty MATCH pattern cell{ctx}")

    head, tail = form

    # a pattern a template wrote arrives marked: the head names a
    # registered matcher by its bare spelling, and the slot keeps its
    # mark so the template's marked reference finds it
    if not is_nil(tail) and is_pair(tail):
        decl_bind = tail.car
        factory = lookup_matcher(str(strip_mark(head)))
        if factory is not None and is_symbol(decl_bind):
            return factory(head, tail)

    specs = []
    cur = form
    while not is_nil(cur):
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
            f"({tag}) expects exactly one payload, got {len(items)} in {source!r}")
    payload = strip_mark(items[0])
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
                f"(CLASS {type_name} ...) field elements must lower to FIELD,"
                f" got {lowered!r}")
        field_objs.append(lowered)
    return CLASS(type_name, *field_objs)


def lower_named_capture(name_sym, args):
    """
    Lower (NAME args...) where NAME is not a registered head. Walks
    args for constraints. Inline ::, type:, and val: markers are
    recognised inside the args list as constraint shorthand. The
    returned CAPTURE accepts at most one constraint child; multiple
    constraints raise ValueError
    """

    name = str(name_sym)
    constraints = []
    items = list(unpack(args))
    i = 0
    n = len(items)
    while i < n:
        elem = strip_mark(items[i])
        if is_keyword(elem):
            if elem is KW_DCOLON or elem is KW_TYPE:
                if i + 1 >= n:
                    raise ValueError(
                        f"({name} ...) constraint marker missing payload")
                payload = strip_mark(items[i + 1])
                if is_symbol(payload):
                    constraints.append(TYPE(str(payload)))
                else:
                    constraints.append(parse_matcher(payload))
                i += 2
                continue
            if elem is KW_VAL:
                if i + 1 >= n:
                    raise ValueError(
                        f"({name} ...) val: marker missing payload")
                constraints.append(LITERAL(items[i + 1]))
                i += 2
                continue
            raise ValueError(
                f"({name} ...) unrecognised keyword {elem!r} in capture body")
        constraints.append(parse_matcher(elem))
        i += 1

    if len(constraints) > 1:
        raise ValueError(
            f"({name} ...) named capture accepts at most one constraint;"
            f" got {len(constraints)}")
    sub = constraints[0] if constraints else None
    return CAPTURE(name, sub)


# === Built-in factory helpers ===


def expect_arity(name, items, expected):
    """
    Assert that items has exactly expected length; raise ValueError
    on mismatch
    """

    if len(items) != expected:
        raise ValueError(
            f"({name} ...) expects {expected} argument(s), got {len(items)}")


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
        f"({name} ...) slot name must be a symbol or string, got {type(value).__name__!r}")


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
        f"({name} ...) argument must be a symbol, keyword, or string,"
        f" got {type(value).__name__!r}")


def slot_only_factory(name, ctor):
    """
    Build a factory for a slot-only composite: (NAME slot)
    """

    def factory(head_sym, raw_args):
        items = list(unpack(raw_args))
        expect_arity(name, items, 1)
        return ctor(coerce_slot(name, items[0]))

    factory.__name__ = f"factory_{name}"
    factory.matcher_wire_class = ctor
    return factory


def unary_recursive_factory(name, ctor):
    """
    Build a factory for a unary-recursive head, e.g. (NOT pat),
    (OPTIONAL pat)
    """

    def factory(head_sym, raw_args):
        items = list(unpack(raw_args))
        expect_arity(name, items, 1)
        return ctor(parse_matcher(items[0]))

    factory.__name__ = f"factory_{name}"
    return factory


_MATCH_MIN_KW = frozenset({"min"})


def _min_option_key(elem):
    """
    Normalize a Koskari keyword to ``min`` for ``min:`` / ``min``
    """

    return str(elem).rstrip(":")


def repeat_match_factory(head_sym, raw_args):
    """
    Lower (REPEAT pat) or (REPEAT slot pat min: N ...).
    Two-or-more args use repeat_capture(slot, REPEAT(inner, min_count=...)).
    """

    items = list(unpack(raw_args))
    if len(items) == 1:
        return REPEAT(parse_matcher(items[0]))
    if len(items) < 2:
        raise ValueError(
            "(REPEAT ...) requires a pattern, or a slot and a pattern")
    slot = coerce_slot("REPEAT", items[0])
    inner = parse_matcher(items[1])
    min_count = 1
    i = 2
    n = len(items)
    while i < n:
        elem = items[i]
        if not is_keyword(elem):
            raise ValueError(
                "(REPEAT ...) trailing arguments must be keyword/value pairs,"
                f" got {elem!r}")
        if _min_option_key(elem) not in _MATCH_MIN_KW:
            raise ValueError(
                f"(REPEAT ...) unknown option {elem!r}")
        if i + 1 >= n:
            raise ValueError(
                f"(REPEAT ...) option {elem!r} missing value")
        mv = items[i + 1]
        if isinstance(mv, bool) or not isinstance(mv, int):
            raise TypeError(
                f"(REPEAT ...) min value must be int, got {type(mv).__name__!r}")
        if mv < 0:
            raise ValueError(
                f"(REPEAT ...) min must be non-negative, got {mv}")
        min_count = mv
        i += 2
    return repeat_capture(slot, REPEAT(inner, min_count=min_count))


def rest_match_factory(head_sym, raw_args):
    """
    Lower (REST), (REST slot), or (REST slot min: N). Default min_tail is
    0; bare (REST) has no capture slot
    """

    items = list(unpack(raw_args))
    if not items:
        return REST(None, min_tail=0)
    slot = coerce_slot("REST", items[0])
    min_tail = 0
    i = 1
    n = len(items)
    while i < n:
        elem = items[i]
        if not is_keyword(elem):
            raise ValueError(
                "(REST ...) trailing arguments must be keyword/value pairs,"
                f" got {elem!r}")
        if _min_option_key(elem) not in _MATCH_MIN_KW:
            raise ValueError(
                f"(REST ...) unknown option {elem!r}")
        if i + 1 >= n:
            raise ValueError(
                f"(REST ...) option {elem!r} missing value")
        mv = items[i + 1]
        if isinstance(mv, bool) or not isinstance(mv, int):
            raise TypeError(
                f"(REST ...) min value must be int, got {type(mv).__name__!r}")
        if mv < 0:
            raise ValueError(
                f"(REST ...) min must be non-negative, got {mv}")
        min_tail = mv
        i += 2
    return REST(slot, min_tail=min_tail)


def tail_match_factory(head_sym, raw_args):
    """
    Lower (TAIL slot) to OPTIONAL(REST(slot, min_tail=1))
    """

    items = list(unpack(raw_args))
    expect_arity("TAIL", items, 1)
    slot = coerce_slot("TAIL", items[0])
    return OPTIONAL(REST(slot, min_tail=1))


def _lower_spine_children(raw_args, child_parser=None):
    """
    Lower the child patterns of a spine composite (FORM / MATCH),
    recognizing the ``*: name`` rest marker: a ``*:`` keyword followed by
    a slot name lowers to a named REST capture. Every other child is
    lowered through ``child_parser`` (parse_matcher by default; the
    runtime case-pattern dialect passes parse_case_element).
    """

    if child_parser is None:
        child_parser = parse_matcher
    items = list(unpack(raw_args))
    out = []
    i = 0
    n = len(items)
    while i < n:
        elem = items[i]
        if elem == KW_STAR:
            if i + 1 >= n:
                raise ValueError("`*:` must be followed by a rest slot name")
            out.append(REST(coerce_slot("*:", items[i + 1])))
            i += 2
        else:
            out.append(child_parser(elem))
            i += 1
    return out


def form_factory(head_sym, raw_args):
    """
    Lower (FORM pat) and (FORM pat1 pat2 ...). The single-arg form
    becomes FORM(parse_matcher(pat)); the multi-arg form implicitly
    wraps the children in MATCH per the rollup convention. A ``*: name``
    child lowers to a named REST capture
    """

    items = _lower_spine_children(raw_args)
    if not items:
        raise ValueError("(FORM ...) requires at least one sub-pattern")
    if len(items) == 1:
        return FORM(items[0])
    return FORM(MATCH(*items))


def match_factory(head_sym, raw_args):
    """
    Lower (MATCH pat...). Empty MATCH is permitted (matches zero
    elements). A ``*: name`` child lowers to a named REST capture
    """

    return MATCH(*_lower_spine_children(raw_args))


def group_named_factory(head_sym, raw_args):
    """
    Lower (GROUP slot pat...) or (GROUP nil pat...) for an anonymous
    value-only group row (slot names the nested dict key when named)
    """

    items = list(unpack(raw_args))
    if not items:
        raise ValueError("(GROUP ...) requires at least one argument")
    first = items[0]
    if is_nil(first):
        return GROUP(None, *[parse_matcher(i) for i in items[1:]])
    slot = coerce_slot("GROUP", first)
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
    lower_named_capture so the constraint splitting matches
    the implicit named-capture form
    """

    items = list(unpack(raw_args))
    if not items:
        raise ValueError("(CAPTURE ...) requires a slot name")
    slot_sym = items[0]
    if not is_symbol(slot_sym):
        raise ValueError(
            f"(CAPTURE ...) slot must be a symbol, got {slot_sym!r}")
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

    factory.__name__ = f"factory_{name}"
    return factory


def app_factory(head_sym, raw_args):
    """
    Lower (APP fn) or (APP fn pat). fn is the extractor's extern name
    (a symbol); the optional pat matches the call's result
    """

    items = list(unpack(raw_args))
    if not items or len(items) > 2:
        raise ValueError(
            f"(APP ...) expects 1 or 2 arguments, got {len(items)}")
    fn = coerce_slot("APP", items[0])
    if len(items) == 1:
        return APP(fn)
    return APP(fn, parse_matcher(items[1]))


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
                f"(FIELD NAME pat) NAME must be a symbol, got {first!r}")
        # the name reaches getattr and carries no marks: a template-written
        # NAME is a gensym, read by its base as the case dialect does
        return FIELD(parse_matcher(items[1]), name=str(strip_mark(first)))
    raise ValueError(
        f"(FIELD ...) expects 1 or 2 arguments, got {len(items)}")


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
    register_matcher("REPEAT", repeat_match_factory)
    register_matcher(
        "OPTIONAL", unary_recursive_factory("OPTIONAL", OPTIONAL))

    register_matcher("CAPTURE", capture_factory)
    register_matcher("REST", rest_match_factory)
    register_matcher("TAIL", tail_match_factory)
    register_matcher("BIND", slot_only_factory("BIND", BIND))
    register_matcher("VALUE", slot_only_factory("VALUE", VALUE))
    register_matcher("KEYWORD", slot_only_factory("KEYWORD", KEYWORD))
    register_matcher("SYMBOL", slot_only_factory("SYMBOL", SYMBOL))
    register_matcher("PAIR", slot_only_factory("PAIR", PAIR))
    register_matcher("BODY", slot_only_factory("BODY", BODY))

    register_matcher("DEFINE", slot_only_factory("DEFINE", DEFINE))
    register_matcher("DECL", slot_only_factory("DECL", DECL))
    register_matcher("BINDING", slot_only_factory("BINDING", BINDING))
    register_matcher("BINDINGS", slot_only_factory("BINDINGS", BINDINGS))
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
    register_matcher("APP", app_factory)


register_builtin_matchers()


# The end.
