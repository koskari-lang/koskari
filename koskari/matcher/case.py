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
koskari.matcher.case

The case-pattern dialect of the match special: the element and
delimiter meta-patterns and the parsers that lower a case pattern to a
matcher pattern

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


from koskari.lib import cons, demand, nil, pair, symbol
from koskari.types import (
    is_keyword, is_nil, is_pair, is_symbol, strip_mark, unpack)

from .heads import MATCHER_REGISTRY, _lower_spine_children, parse_matcher
from .patterns import (
    APP, CAPTURE, CLASS, FIELD, FORM, LITERAL, LITERAL_KW, LITERAL_SYM, MAP,
    MAP_ENTRY, MATCH, NOT, ONEOF, PREDICATE, REST, TEST_ANY, TYPE,
    make_matcher)


__all__ = (
    'CASE_DELIM_MATCHER',
    'CASE_DELIM_PATTERN',
    'CASE_ELEMENT_MATCHER',
    'CASE_ELEMENT_PATTERN',
    'parse_case_element',
    'parse_case_pattern',
)


# === Runtime case-pattern dialect (match/case surface) ===
#
# The runtime match/case surface classifies case patterns the same way
# parse_matcher classifies matcher syntax: an ordered ONEOF meta-pattern
# recognizes one pattern element, and a walker dispatches on which
# capture slot bound, recursing one level at a time. Pattern *kind* is
# decided entirely by head symbol or marker -- reader sugars mint the
# distinguishing heads (#list / #tuple for sequences, item for class,
# #dict for mappings) -- never by cell or bracket type. See
# design/archive/match-pattern-language.md.


CASE_ELEMENT_PATTERN = ONEOF(
    # 'sym -- literal symbol
    FORM(MATCH(
        LITERAL_SYM("quote"),
        CAPTURE("quoted_symbol", TYPE("symbol")))),

    # sequence sugar: (#list ...) / (#tuple ...); the head capture keeps
    # the branch distinguishable even when the element list is empty
    FORM(MATCH(
        CAPTURE("seq_head",
                ONEOF(LITERAL_SYM("#list"), LITERAL_SYM("#tuple"))),
        REST("seq_elems"))),

    # mapping sugar: (#dict (#tuple KEY VAL) ...)
    FORM(MATCH(
        CAPTURE("map_head", LITERAL_SYM("#dict")),
        REST("map_entries"))),

    # class sugar: (item Name field...)
    FORM(MATCH(
        CAPTURE("class_head", LITERAL_SYM("item")),
        CAPTURE("class_name", TYPE("symbol")),
        REST("class_fields"))),

    # combinators
    FORM(MATCH(
        CAPTURE("or_head", LITERAL_SYM("or")),
        REST("or_branches"))),
    FORM(MATCH(LITERAL_SYM("not"), CAPTURE("not_operand"))),
    FORM(MATCH(LITERAL_SYM("?"), CAPTURE("pred_name", TYPE("symbol")))),

    # computed extraction: (app f) / (app f pat)
    FORM(MATCH(
        LITERAL_SYM("app"),
        CAPTURE("app_fn", TYPE("symbol")),
        CAPTURE("app_pat"))),
    FORM(MATCH(
        LITERAL_SYM("app"),
        CAPTURE("app_fn", TYPE("symbol")))),

    # marked typed element: (name :: ...) / (:: ...); the whole form is
    # re-fed to parse_matcher, which owns the constraint grammar
    FORM(MATCH(
        CAPTURE("marked_name", TYPE("symbol")),
        LITERAL_KW("::"),
        REST("marked_args"))),
    FORM(MATCH(
        CAPTURE("anon_marker", LITERAL_KW("::")),
        REST("marked_args"))),

    # explicit matcher heads (CLASS, FORM, ONEOF, REST, ...); the walker
    # checks the registry -- a bare (a b c) is never a silent sequence
    FORM(MATCH(
        CAPTURE("known_head", TYPE("symbol")),
        REST("known_args"))),

    # atoms
    CAPTURE("atom_symbol", TYPE("symbol")),
    CAPTURE("atom_literal"),
)


CASE_ELEMENT_MATCHER = demand(make_matcher(MATCH(CASE_ELEMENT_PATTERN)))


CASE_DELIM_PATTERN = ONEOF(
    # one form inside the delimiter: the pattern itself
    FORM(MATCH(CAPTURE("single"))),

    # multi-token marked pattern: [[x :: int]] / [[:: int]]
    FORM(MATCH(
        CAPTURE("multi_name", TYPE("symbol")),
        LITERAL_KW("::"),
        REST("multi_args"))),
    FORM(MATCH(
        CAPTURE("multi_marker", LITERAL_KW("::")),
        REST("multi_args"))),
)


CASE_DELIM_MATCHER = demand(make_matcher(MATCH(CASE_DELIM_PATTERN)))


_symbol_hash_tuple = symbol("#tuple")


# expected shapes for the case-dialect heads, used to diagnose an
# arity / argument-shape mistake that fell past the meta-branches
_CASE_DIALECT_USAGE = {
    "app": "(app extractor) or (app extractor PATTERN)",
    "?": "(? predicate) with a symbol predicate name",
    "not": "(not PATTERN)",
    "or": "(or PATTERN ...)",
    "item": "(item ClassName field...) -- the ClassName[field...] sugar",
}


def _lower_case_map(entries):
    """
    Lower the entries of a mapping sugar pattern ((#dict (#tuple KEY
    VAL) ...), written #{KEY: VAL ...}). A symbol key is an extern
    resolved from the namespace at match time (the same channel as type
    and class names); any other atom is a literal key. Each value is a
    case-pattern element for the looked-up value
    """

    lowered = []
    for entry in unpack(entries):
        items = list(unpack(entry)) if is_pair(entry) else []
        if len(items) != 3 or items[0] is not _symbol_hash_tuple:
            raise ValueError(
                f"#{{...}} pattern entry must be a key: value pair,"
                f" got {entry!r}")
        key, val = items[1], items[2]
        sub = parse_case_element(val)
        if is_symbol(key):
            lowered.append(MAP_ENTRY(str(key), sub, extern=True))
        elif is_pair(key):
            raise ValueError(
                f"#{{...}} pattern key must be an atom, got {key!r}")
        else:
            lowered.append(MAP_ENTRY(key, sub))
    return MAP(*lowered)


def _lower_case_class(name_sym, fields):
    """
    Lower the fields of a class sugar pattern ((item Name field...),
    written Name[field...]) into CLASS(name, FIELD...). Python-style
    field rules: elements before the first keyword are positional
    fields (matched via __match_args__); ``k: pat`` is a keyword field
    on attribute k; a bare symbol after keywords have started is the
    self-binding keyword field ``k: k``. Positional fields may not
    follow keyword fields
    """

    # the class name is a reference the use site resolves and a field
    # keyword names an attribute: both read by their bare spelling. A
    # self-binding field binds under its marked spelling and names the
    # attribute under the bare one
    type_name = str(name_sym.base)
    field_objs = []
    items = list(unpack(fields))
    seen_keyword = False
    i = 0
    n = len(items)
    while i < n:
        elem = items[i]
        tag = strip_mark(elem)
        if is_keyword(tag):
            if i + 1 >= n:
                raise ValueError(
                    f"{type_name}[...] keyword field"
                    f" {tag!s} missing its pattern")
            field_objs.append(FIELD(
                parse_case_element(items[i + 1]),
                name=str(tag).rstrip(":")))
            seen_keyword = True
            i += 2
        elif seen_keyword:
            if not is_symbol(tag):
                raise ValueError(
                    f"{type_name}[...] field after keyword fields"
                    f" must be a symbol, got {elem!r}")
            field_objs.append(FIELD(CAPTURE(str(elem)), name=str(tag)))
            i += 1
        else:
            field_objs.append(FIELD(parse_case_element(elem)))
            i += 1
    return CLASS(type_name, *field_objs)


def parse_case_element(elem):
    """
    Lower one runtime case-pattern element to a matcher pattern object.
    Runs CASE_ELEMENT_MATCHER against (elem,), then dispatches on which
    capture slot bound. Raises ValueError when elem does not satisfy any
    case-pattern surface form

    :param elem: one koskari source element (atom or pair-tree)
    :rtype: Pattern
    """

    if is_nil(elem):
        raise ValueError(
            "nil is not a valid case pattern element")
    c = CASE_ELEMENT_MATCHER(cons(elem, nil))
    if c is None:
        raise ValueError(
            f"not a valid case pattern: {elem!r}")

    if "atom_symbol" in c:
        return CAPTURE(str(c["atom_symbol"].datum))

    if "atom_literal" in c:
        atom = c["atom_literal"].datum
        if isinstance(atom, pair):
            raise ValueError(
                f"improper list is not a valid case pattern: {atom!r}")
        return LITERAL(atom)

    if "quoted_symbol" in c:
        return LITERAL_SYM(str(c["quoted_symbol"].datum.base))

    if "seq_head" in c:
        elems = c["seq_elems"].datum if "seq_elems" in c else nil
        return FORM(MATCH(
            *_lower_spine_children(elems, parse_case_element)))

    if "map_head" in c:
        entries = c["map_entries"].datum if "map_entries" in c else nil
        return _lower_case_map(entries)

    if "class_head" in c:
        name = c["class_name"].datum
        fields = c["class_fields"].datum if "class_fields" in c else nil
        return _lower_case_class(name, fields)

    if "or_head" in c:
        if "or_branches" not in c:
            raise ValueError("(or ...) requires at least one branch")
        branches = [
            parse_case_element(e)
            for e in unpack(c["or_branches"].datum)]
        return ONEOF(*branches)

    if "not_operand" in c:
        # the NOT pattern is a zero-width lookahead; the case dialect's
        # (not p) is a whole-element pattern, so pair the check with a
        # consuming any -- the element must be present and must not
        # match p. (The canonical registry (NOT ...) head keeps the raw
        # zero-width semantics.)
        return MATCH(
            NOT(parse_case_element(c["not_operand"].datum)),
            TEST_ANY())

    if "pred_name" in c:
        return PREDICATE(str(c["pred_name"].datum))

    if "app_fn" in c:
        fn = str(c["app_fn"].datum)
        if "app_pat" in c:
            return APP(fn, parse_case_element(c["app_pat"].datum))
        return APP(fn)

    if "marked_name" in c or "anon_marker" in c:
        return parse_matcher(elem)

    if "known_head" in c:
        head = c["known_head"].datum
        name = str(head)
        if name in MATCHER_REGISTRY:
            return parse_matcher(elem)
        # a dialect head that missed its meta-branch got the arity or
        # argument shape wrong -- diagnose that, not "unknown head"
        usage = _CASE_DIALECT_USAGE.get(name)
        if usage is not None:
            raise ValueError(
                f"malformed ({name} ...) case pattern {elem!r};"
                f" expected {usage}")
        raise ValueError(
            f"unknown case pattern head {name};"
            " sequence patterns use #[...] or #(...)")

    raise ValueError(
        f"internal: case meta-matcher matched with no slot bound: {elem!r}")


def parse_case_pattern(content):
    """
    Lower the content of one match/case clause delimiter to a matcher
    pattern object. The delimiter ([[PATTERN] BODY...]) holds either a
    single pattern form, or the multi-token spelling of a marked typed
    pattern (x :: int); a bare multi-token content is an error -- it is
    never a silent sequence

    :param content: the delimiter list (pair-tree)
    :rtype: Pattern
    """

    if is_nil(content):
        raise ValueError("empty case pattern")
    c = CASE_DELIM_MATCHER(cons(content, nil))
    if c is None:
        raise ValueError(
            f"invalid case pattern {content!r};"
            " sequence patterns use #[...] or #(...)")

    if "single" in c:
        return parse_case_element(c["single"].datum)

    if "multi_name" in c or "multi_marker" in c:
        return parse_matcher(content)

    raise ValueError(
        f"internal: case delimiter matched with no slot bound: {content!r}")


# The end.
