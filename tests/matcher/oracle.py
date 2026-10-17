# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.


"""
tests.matcher.oracle

The matcher oracle: a corpus of pattern specs, subjects and inline
match arms, with the outcomes master's native engine gave them, and the
comparison of this tree's outcomes against it.

A spec is plain JSON data that both trees turn into a Pattern through
the builder API they share; a value (subject, literal, capture) is
encoded by encode and rebuilt by decode. The externs a pattern names
come from EXTERNS, the one fixed set both trees use. The generator that
wrote the corpus runs this module under master's build, so everything
above the comparison section uses only names both trees have.

compare loads the corpus, runs every case on this tree, and applies the
deliberate changes from master as named rules. Run it by hand with
``uv run python3 -m tests.matcher.oracle`` from the repository root.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import json
import sys

from os.path import dirname, join

from koskari import match_core
from koskari.lib import (
    Datum, DatumFrame, cons, demand, keyword, nil, pair, setcar, symbol)
from koskari.matcher import (
    APP, CAPTURE, CLASS, FIELD, FORM, GROUP, LITERAL, MAP, MAP_ENTRY,
    MATCH, NOT, ONEOF, OPTIONAL, PREDICATE, REPEAT, REST, SAME, TEST_ANY,
    TYPE)
from koskari.module import (
    compile_top_level, load_facets, new_module, run_codes_demand)
from koskari.reader import source_str


__all__ = (
    "EXTERNS",
    "MASTER_ALREADY",
    "ORACLE_DIR",
    "RULES",
    "build_pattern",
    "compare",
    "decode",
    "encode",
    "format_report",
    "load_inline",
)


ORACLE_DIR = join(dirname(__file__), "data", "oracle")

# captures and subjects are encoded no deeper than this; a deeper value
# is written as {"deep": true}
ENCODE_DEPTH = 200

# a pair spine is encoded up to this many cells
SPINE_LIMIT = 1000

# the value a poke marker writes into the cell it targets
POKE_VALUE = 99


# === The extern set ===


class Pt:
    """
    A class with two positional match fields
    """

    __match_args__ = ("x", "y")


    def __init__(self, x, y):
        self.x = x
        self.y = y


    def __repr__(self):
        return f"Pt({self.x!r}, {self.y!r})"


class Box:
    """
    A class with no __match_args__
    """

    def __init__(self, v):
        self.v = v


    def __repr__(self):
        return f"Box({self.v!r})"


class Poke:
    """
    A subject element that, when the poke predicate tests it, writes
    POKE_VALUE into the element ahead elements after it. decode sets
    the target when it builds the enclosing sequence
    """

    def __init__(self, ahead):
        self.ahead = ahead
        self.cell = None
        self.target = None


    def fire(self):
        """
        Write POKE_VALUE into the target cell or list entry
        """

        if self.cell is not None:
            setcar(self.cell, POKE_VALUE)
        elif self.target is not None:
            seq, index = self.target
            seq[index] = POKE_VALUE


    def __repr__(self):
        return f"Poke({self.ahead})"


def peel(value):
    """
    The value under any Datum boxes
    """

    while isinstance(value, Datum):
        value = value.datum
    return value


def even(value):
    """
    Whether the value is an even int
    """

    value = peel(value)
    return type(value) is int and value % 2 == 0


def big(value):
    """
    Whether the value is an int above 3
    """

    value = peel(value)
    return type(value) is int and value > 3


def boom(value):
    """
    Raise ValueError for an int above 8, else true
    """

    value = peel(value)
    if type(value) is int and value > 8:
        raise ValueError("boom")
    return True


def poke(value):
    """
    Fire a Poke element; true for a Poke, false otherwise
    """

    value = peel(value)
    if isinstance(value, Poke):
        value.fire()
        return True
    return False


def inc(value):
    """
    The int plus one, or None
    """

    value = peel(value)
    if type(value) is int:
        return value + 1
    return None


def first(value):
    """
    The first item of a tuple or list (IndexError when empty), or None
    """

    value = peel(value)
    if isinstance(value, (tuple, list)):
        return value[0]
    return None


def twice(value):
    """
    A 2-tuple of the value
    """

    value = peel(value)
    return (value, value)


EXTERNS = {
    "Pt": Pt,
    "Box": Box,
    "Num": int,
    "even": even,
    "big": big,
    "boom": boom,
    "poke": poke,
    "inc": inc,
    "first": first,
    "twice": twice,
    "kx": "k",
    "kj": "j",
}


# === Values ===


def encode_items(mapping, depth):
    """
    The items of a mapping as encoded [key, value] lists, in the order
    of their encoded keys
    """

    items = [
        [encode(k, depth + 1), encode(v, depth + 1)]
        for k, v in mapping.items()]
    items.sort(key=lambda kv: json.dumps(kv[0], sort_keys=True))
    return items


def encode(value, depth=0):
    """
    Encode a value as JSON data: int, str, bool and None as themselves,
    everything else as a one-key tagged object (tuple, list, pair with
    an optional tail, symbol, keyword, datum with optional position and
    binding, frame and dict as sorted item lists, Pt, Box, poke), or
    repr for anything else
    """

    if depth > ENCODE_DEPTH:
        return {"deep": True}
    if value is None or type(value) in (bool, int, str):
        return value
    if isinstance(value, symbol):
        return {"symbol": str(value)}
    if isinstance(value, keyword):
        return {"keyword": str(value)}
    if isinstance(value, Datum):
        out = {"datum": encode(value.datum, depth + 1)}
        if value.position is not None:
            out["position"] = encode(value.position, depth + 1)
        if value.binding is not None:
            out["binding"] = str(value.binding)
        return out
    if isinstance(value, DatumFrame):
        return {"frame": encode_items(value, depth)}
    if isinstance(value, dict):
        return {"dict": encode_items(value, depth)}
    if isinstance(value, pair):
        items = []
        cell = value
        while isinstance(cell, pair) and cell is not nil:
            if len(items) >= SPINE_LIMIT:
                return {"pair": items, "truncated": True}
            items.append(encode(cell.car, depth + 1))
            cell = cell.cdr
        out = {"pair": items}
        if cell is not nil:
            out["tail"] = encode(cell, depth + 1)
        return out
    if isinstance(value, tuple):
        return {"tuple": [encode(v, depth + 1) for v in value]}
    if isinstance(value, list):
        return {"list": [encode(v, depth + 1) for v in value]}
    if isinstance(value, Pt):
        return {"Pt": [encode(value.x, depth + 1), encode(value.y, depth + 1)]}
    if isinstance(value, Box):
        return {"Box": encode(value.v, depth + 1)}
    if isinstance(value, Poke):
        return {"poke": value.ahead}
    return {"repr": repr(value), "type": type(value).__name__}


def decode_nest(enc):
    """
    A list nested enc["nest"] deep around enc["leaf"], built without
    recursion
    """

    value = decode(enc["leaf"])
    for _ in range(enc["nest"]):
        value = [value]
    return value


def decode(enc):
    """
    Rebuild a value from its encoding. Every call builds fresh objects,
    and a Poke in a pair spine or list gets its target
    """

    if not isinstance(enc, dict):
        return enc
    if "pair" in enc:
        items = [decode(v) for v in enc["pair"]]
        tail = decode(enc["tail"]) if "tail" in enc else nil
        value = cons(*items, tail)
        cell = value
        cells = []
        while isinstance(cell, pair) and cell is not nil:
            cells.append(cell)
            cell = cell.cdr
        for index, item in enumerate(items):
            if isinstance(item, Poke) and index + item.ahead < len(cells):
                item.cell = cells[index + item.ahead]
        return value
    if "list" in enc:
        value = [decode(v) for v in enc["list"]]
        for index, item in enumerate(value):
            if isinstance(item, Poke) and index + item.ahead < len(value):
                item.target = (value, index + item.ahead)
        return value
    if "tuple" in enc:
        return tuple(decode(v) for v in enc["tuple"])
    if "symbol" in enc:
        return symbol(enc["symbol"])
    if "keyword" in enc:
        return keyword(enc["keyword"])
    if "datum" in enc:
        position = decode(enc["position"]) if "position" in enc else None
        return Datum(decode(enc["datum"]), position, enc.get("binding"))
    if "dict" in enc:
        return {decode(k): decode(v) for k, v in enc["dict"]}
    if "Pt" in enc:
        return Pt(decode(enc["Pt"][0]), decode(enc["Pt"][1]))
    if "Box" in enc:
        return Box(decode(enc["Box"]))
    if "poke" in enc:
        return Poke(enc["poke"])
    if "nest" in enc:
        return decode_nest(enc)
    raise ValueError(f"cannot decode {enc!r}")


# === Specs ===
#
# A spec is a JSON list whose first item is its tag:
#
#   ["ANY"]                         TEST_ANY
#   ["LIT", value]                  LITERAL of the decoded value
#   ["TYPE", name]                  TYPE (a core type or an extern)
#   ["PRED", name]                  PREDICATE
#   ["SAME", slot, depth]           SAME
#   ["FORM", sub]                   FORM
#   ["APP", name, sub-or-null]      APP
#   ["CLASS", name, field...]       CLASS; field ["FIELD", sub, kw-or-null]
#   ["MAP", entry...]               MAP; entry ["ENTRY", key, extern, sub]
#                                   (key an encoded constant, or the
#                                   extern's name when extern is true)
#   ["MATCH", child...]             MATCH
#   ["GROUP", slot-or-null, child...]  GROUP
#   ["ONEOF", branch...]            ONEOF
#   ["OPT", sub]                    OPTIONAL
#   ["REPEAT", sub, min]            REPEAT
#   ["REST", slot-or-null, min_tail]   REST
#   ["NOT", sub]                    NOT
#   ["CAP", slot, sub-or-null]      CAPTURE
#   ["NEST", depth, sub]            sub under depth FORM(MATCH(...))
#                                   levels, built without recursion


def spec_children(spec):
    """
    The sub-specs of a spec, in order
    """

    tag = spec[0]
    if tag in ("FORM", "OPT", "NOT", "NEST"):
        return [spec[-1]]
    if tag == "REPEAT":
        return [spec[1]]
    if tag in ("APP", "CAP"):
        return [spec[2]] if spec[2] is not None else []
    if tag == "CLASS":
        return [field[1] for field in spec[2:]]
    if tag == "MAP":
        return [entry[3] for entry in spec[1:] if entry[3] is not None]
    if tag in ("MATCH", "ONEOF"):
        return list(spec[1:])
    if tag == "GROUP":
        return list(spec[2:])
    return []


def build_optional(spec):
    """
    The Pattern for spec, or None for a null spec
    """

    if spec is None:
        return None
    return build_pattern(spec)


def build_pattern(spec):
    """
    The Pattern a spec describes

    :param spec: a spec (see the table above)
    :rtype: Pattern
    """

    tag = spec[0]
    if tag == "ANY":
        return TEST_ANY()
    if tag == "LIT":
        return LITERAL(decode(spec[1]))
    if tag == "TYPE":
        return TYPE(spec[1])
    if tag == "PRED":
        return PREDICATE(spec[1])
    if tag == "SAME":
        return SAME(spec[1], depth=spec[2])
    if tag == "FORM":
        return FORM(build_pattern(spec[1]))
    if tag == "APP":
        return APP(spec[1], build_optional(spec[2]))
    if tag == "CLASS":
        return CLASS(spec[1], *[
            FIELD(build_pattern(field[1]), name=field[2])
            for field in spec[2:]])
    if tag == "MAP":
        return MAP(*[
            MAP_ENTRY(
                entry[1] if entry[2] else decode(entry[1]),
                build_optional(entry[3]),
                extern=entry[2])
            for entry in spec[1:]])
    if tag == "MATCH":
        return MATCH(*[build_pattern(c) for c in spec[1:]])
    if tag == "GROUP":
        return GROUP(spec[1], *[build_pattern(c) for c in spec[2:]])
    if tag == "ONEOF":
        return ONEOF(*[build_pattern(c) for c in spec[1:]])
    if tag == "OPT":
        return OPTIONAL(build_pattern(spec[1]))
    if tag == "REPEAT":
        return REPEAT(build_pattern(spec[1]), min_count=spec[2])
    if tag == "REST":
        return REST(spec[1], min_tail=spec[2])
    if tag == "NOT":
        return NOT(build_pattern(spec[1]))
    if tag == "CAP":
        return CAPTURE(spec[1], build_optional(spec[2]))
    if tag == "NEST":
        pattern = build_pattern(spec[2])
        for _ in range(spec[1]):
            pattern = FORM(MATCH(pattern))
        return pattern
    raise ValueError(f"unknown spec tag {tag!r}")


# === Outcomes ===
#
#   {"hit": captures}               a match, the captures encoded
#   {"miss": true}                  no match
#   {"refused": type, "message": m} the build (or the arm's compile)
#                                   raised
#   {"raised": type, "message": m}  the match raised
#
# An inline arm's hit and miss are both {"result": value}, the value
# its function returned.


def load_inline(source):
    """
    Compile and run Koskari source in a fresh module whose namespace
    holds EXTERNS, and return that namespace

    :param source: Koskari source text, one form per line
    :rtype: ModuleNamespace
    """

    module = new_module("oracle_inline")
    ns = vars(module)
    ns["__file__"] = "<oracle>"
    for name, value in EXTERNS.items():
        ns[name] = value
    with load_facets(module, "<oracle>", seal=False, detach=True) \
            as facets:
        run_codes_demand(
            module, compile_top_level(*facets, source_str(source, "<oracle>")))
    return ns


# === Running on this tree ===


def run_standalone(record):
    """
    This tree's outcome for each subject of a standalone record
    """

    try:
        call = demand(build_pattern(record["spec"]).build(EXTERNS))
    except Exception as exc:
        refused = {"refused": type(exc).__name__, "message": str(exc)[:200]}
        return [refused for _ in record["cases"]]

    outcomes = []
    for case in record["cases"]:
        subject = decode(case["subject"])
        try:
            captures = call(subject)
        except Exception as exc:
            outcomes.append({"raised": type(exc).__name__, "message": str(exc)[:200]})
            continue
        if captures is None:
            outcomes.append({"miss": True})
        else:
            outcomes.append({"hit": encode(captures)})
    return outcomes


def run_inline(record):
    """
    This tree's outcome for each subject of an inline record
    """

    try:
        call = demand(load_inline(record["source"])["case_fn"])
    except Exception as exc:
        refused = {"refused": type(exc).__name__, "message": str(exc)[:200]}
        return [refused for _ in record["cases"]]

    outcomes = []
    for case in record["cases"]:
        subject = decode(case["subject"])
        try:
            outcomes.append({"result": encode(call(subject))})
        except Exception as exc:
            outcomes.append({"raised": type(exc).__name__, "message": str(exc)[:200]})
    return outcomes


# === Static facts about a spec ===


def spec_width(spec):
    """
    The (min, max) count of elements spec consumes on its cursor; max
    is None when unbounded
    """

    tag = spec[0]
    if tag in ("ANY", "LIT", "TYPE", "PRED", "SAME", "FORM", "APP",
               "CLASS", "MAP", "NEST"):
        return (1, 1)
    if tag == "CAP":
        return (1, 1) if spec[2] is None else spec_width(spec[2])
    if tag in ("MATCH", "GROUP"):
        lo, hi = 0, 0
        for child in spec_children(spec):
            clo, chi = spec_width(child)
            lo += clo
            hi = None if hi is None or chi is None else hi + chi
        return (lo, hi)
    if tag == "ONEOF":
        widths = [spec_width(b) for b in spec[1:]]
        lo = min(w[0] for w in widths)
        hi = None if any(w[1] is None for w in widths) \
            else max(w[1] for w in widths)
        return (lo, hi)
    if tag == "OPT":
        return (0, spec_width(spec[1])[1])
    if tag == "REPEAT":
        lo, hi = spec_width(spec[1])
        if hi == 0:
            return (0, 0)
        return (lo * spec[2], None)
    if tag == "REST":
        return (spec[2], None)
    if tag == "NOT":
        return (0, 0)
    raise ValueError(f"unknown spec tag {tag!r}")


def spec_nodes(spec):
    """
    Every spec in the tree under spec, spec first
    """

    out = []
    stack = [spec]
    while stack:
        node = stack.pop()
        out.append(node)
        stack.extend(reversed(spec_children(node)))
    return out


def is_capture_node(spec):
    """
    Whether the node binds a slot: CAPTURE, a named GROUP, a named REST
    """

    tag = spec[0]
    return tag == "CAP" or tag in ("GROUP", "REST") and spec[1] is not None


NON_POOL_TAGS = ("list", "pair", "dict", "Pt", "Box", "poke", "nest")


def is_non_pool(enc):
    """
    Whether an encoded literal value is one the constant pool does not
    hold
    """

    if not isinstance(enc, dict):
        return False
    if "pair" in enc:
        return bool(enc["pair"]) or "tail" in enc
    if "tuple" in enc:
        return any(is_non_pool(v) for v in enc["tuple"])
    return any(tag in enc for tag in NON_POOL_TAGS)


def single_value_subs(spec):
    """
    The sub-specs in a single-value position: an APP or MAP value
    """

    out = []
    for node in spec_nodes(spec):
        if node[0] == "APP" and node[2] is not None:
            out.append(node[2])
        elif node[0] == "MAP":
            out.extend(e[3] for e in node[1:] if e[3] is not None)
    return out


def width_has_one(width):
    """
    Whether a (min, max) width range includes 1
    """

    lo, hi = width
    return lo <= 1 and (hi is None or hi >= 1)


def scan_after_rest(spec, state):
    """
    Walk the nodes on one cursor in order, setting state["bad"] when a
    variable-width node follows a REST on it. Projections start their
    own cursor
    """

    tag = spec[0]
    lo, hi = spec_width(spec)
    if state["rest"] and lo != hi:
        state["bad"] = True

    if tag in ("MATCH", "GROUP"):
        for child in spec_children(spec):
            scan_after_rest(child, state)
    elif tag == "CAP" and spec[2] is not None:
        scan_after_rest(spec[2], state)
    elif tag == "ONEOF":
        after = state["rest"]
        for branch in spec[1:]:
            sub = {"rest": state["rest"], "bad": False}
            scan_after_rest(branch, sub)
            after = after or sub["rest"]
            state["bad"] = state["bad"] or sub["bad"]
        state["rest"] = after
    elif tag == "OPT":
        sub = {"rest": state["rest"], "bad": False}
        scan_after_rest(spec[1], sub)
        state["bad"] = state["bad"] or sub["bad"]
        state["rest"] = state["rest"] or sub["rest"]
    elif tag == "NOT":
        sub = {"rest": state["rest"], "bad": False}
        scan_after_rest(spec[1], sub)
        state["bad"] = state["bad"] or sub["bad"]
    elif tag == "REST":
        state["rest"] = True
    else:
        for child in spec_children(spec):
            sub = {"rest": False, "bad": False}
            scan_after_rest(child, sub)
            state["bad"] = state["bad"] or sub["bad"]


def variable_after_rest(spec):
    """
    Whether a variable-width node follows a REST on the same cursor
    """

    state = {"rest": False, "bad": False}
    scan_after_rest(spec, state)
    return state["bad"]


def contains_improper(enc):
    """
    Whether an encoded value holds an improper pair spine
    """

    stack = [enc]
    while stack:
        value = stack.pop()
        if isinstance(value, list):
            stack.extend(value)
        elif isinstance(value, dict):
            if "pair" in value and "tail" in value:
                return True
            stack.extend(value.values())
    return False


def contains_tag(enc, tag):
    """
    Whether an encoded value holds an object tagged tag
    """

    stack = [enc]
    while stack:
        value = stack.pop()
        if isinstance(value, list):
            stack.extend(value)
        elif isinstance(value, dict):
            if tag in value:
                return True
            stack.extend(value.values())
    return False


def case_specs(case):
    """
    The pattern specs of a case: the standalone spec, or the arm specs
    of an inline record
    """

    if case["kind"] == "standalone":
        return [case["spec"]]
    return case["arms"]


# === The rules ===
#
# A rule is (name, predicate). The predicate takes the case (a dict
# with kind, spec or arms, subject), master's outcome and this tree's
# outcome for it, and is consulted only when the two differ. A case is
# explained by the first rule whose predicate holds.


def rule_improper_subject(case, master, branch):
    """
    An improper subject is a miss: master raised TypeError for an
    improper spine; this tree does not raise
    """

    return (master.get("raised") == "TypeError"
            and contains_improper(case["subject"])
            and ("miss" in branch or "hit" in branch or "result" in branch))


def rule_depth_refused(case, master, branch):
    """
    A pattern nested past the depth limit is refused at build: this
    tree's limit is the interpreter's recursion limit, so the refusal
    is a RecursionError
    """

    return (branch.get("refused") == "RecursionError"
            and any(node[0] == "NEST"
                    for s in case_specs(case) for node in spec_nodes(s)))


def rule_non_pool_literal(case, master, branch):
    """
    A LITERAL whose value is not a pool constant is refused at build
    """

    return (branch.get("refused") is not None
            and any(node[0] == "LIT" and is_non_pool(node[1])
                    for s in case_specs(case) for node in spec_nodes(s)))


def rule_single_value_width(case, master, branch):
    """
    A sub-pattern in a single-value position (a match arm root, an APP
    or MAP value) whose width range does not include 1 is refused
    """

    if branch.get("refused") is None:
        return False
    for spec in case_specs(case):
        if case["kind"] == "inline" and not width_has_one(spec_width(spec)):
            return True
        if any(not width_has_one(spec_width(s))
               for s in single_value_subs(spec)):
            return True
    return False


def rule_field_width(case, master, branch):
    """
    A FIELD sub-pattern whose width is not exactly 1 is refused
    """

    return (branch.get("refused") is not None
            and any(node[0] == "CLASS"
                    and any(spec_width(f[1]) != (1, 1) for f in node[2:])
                    for s in case_specs(case) for node in spec_nodes(s)))


def rule_repeat_zero_body(case, master, branch):
    """
    A REPEAT whose body has minimum width 0 is refused
    """

    return (branch.get("refused") is not None
            and any(node[0] == "REPEAT" and spec_width(node[1])[0] == 0
                    for s in case_specs(case) for node in spec_nodes(s)))


def rule_variable_after_rest(case, master, branch):
    """
    A node of variable width after a REST on the same cursor is refused
    """

    return (branch.get("refused") is not None
            and any(variable_after_rest(s) for s in case_specs(case)))


def rule_capture_under_not(case, master, branch):
    """
    A CAPTURE anywhere in a NOT subtree is refused
    """

    return (branch.get("refused") is not None
            and any(is_capture_node(inner)
                    for s in case_specs(case)
                    for node in spec_nodes(s) if node[0] == "NOT"
                    for inner in spec_nodes(node[1])))


def rule_element_read_when_pulled(case, master, branch):
    """
    An element is read when it is pulled: a poke predicate edits an
    element ahead of the cursor, and the walk reads the edit
    """

    return (contains_tag(case["subject"], "poke")
            and any(node[0] == "PRED" and node[1] == "poke"
                    for s in case_specs(case) for node in spec_nodes(s))
            and branch.get("refused") is None)


SEQUENCE_TAGS = ("pair", "tuple", "list")


def rule_non_sequence_root(case, master, branch):
    """
    A standalone matcher's root subject that is not a sequence (a pair
    spine, tuple or list, read through a Datum) misses; master raised
    TypeError, or iterated a mapping's keys or a str
    """

    if case["kind"] != "standalone" or "miss" not in branch:
        return False
    subject = case["subject"]
    while isinstance(subject, dict) and "datum" in subject:
        subject = subject["datum"]
    return not (isinstance(subject, dict)
                and any(tag in subject for tag in SEQUENCE_TAGS))


def bare_repeat_slots(spec):
    """
    The slots bound inside a REPEAT that no CAPTURE holds
    """

    captured = set()
    slots = set()
    for node in spec_nodes(spec):
        if node[0] == "CAP" and node[2] is not None \
                and node[2][0] == "REPEAT":
            captured.add(id(node[2]))
    for node in spec_nodes(spec):
        if node[0] == "REPEAT" and id(node) not in captured:
            slots.update(inner[1] for inner in spec_nodes(node[1])
                         if is_capture_node(inner))
    return slots


def last_pass_of(master_value, branch_value):
    """
    Whether branch_value is a list of per-pass values whose last pass
    is master_value, looking through nested REPEATs' lists
    """

    if not (isinstance(branch_value, dict) and "list" in branch_value):
        return False
    items = branch_value["list"]
    return bool(items) and (items[-1] == master_value
                            or last_pass_of(master_value, items[-1]))


def per_pass_lists(master_value, branch_value, slots):
    """
    Whether the two encodings are equal but for slots in slots, each of
    which holds on this tree the list of per-pass values where master
    held the last pass, or a list where master did not bind it: the
    empty list after no pass, or the per-pass values of a REPEAT whose
    GROUP body's captures master discarded
    """

    if master_value == branch_value:
        return True
    if not (isinstance(master_value, dict) and isinstance(branch_value, dict)
            and master_value.keys() == branch_value.keys()):
        if isinstance(master_value, list) and isinstance(branch_value, list) \
                and len(master_value) == len(branch_value):
            return all(per_pass_lists(m, b, slots)
                       for m, b in zip(master_value, branch_value))
        return False
    for tag in ("dict", "frame"):
        if tag in master_value:
            mitems = {json.dumps(k): v for k, v in master_value[tag]}
            bitems = {json.dumps(k): v for k, v in branch_value[tag]}
            if not set(mitems) <= set(bitems):
                return False
            for key, bv in bitems.items():
                name = json.loads(key)
                if key not in mitems:
                    if name not in slots or not (
                            isinstance(bv, dict) and "list" in bv):
                        return False
                elif mitems[key] != bv and not (
                        name in slots and last_pass_of(mitems[key], bv)
                        or per_pass_lists(mitems[key], bv, slots)):
                    return False
            return True
    return all(per_pass_lists(master_value[k], branch_value[k], slots)
               for k in master_value)


def rule_repeat_slot_lists(case, master, branch):
    """
    A slot inside an uncaptured REPEAT binds the list of its per-pass
    values, where master kept the last pass (and left the slot unbound
    after no pass)
    """

    slots = set()
    for spec in case_specs(case):
        slots |= bare_repeat_slots(spec)
    if not slots:
        return False
    if "hit" in master and "hit" in branch:
        return per_pass_lists(master["hit"], branch["hit"], slots)
    if case["kind"] != "inline" or "result" not in master \
            or "result" not in branch:
        return False
    mlabel, mvalues = master["result"]["tuple"]
    blabel, bvalues = branch["result"]["tuple"]
    if mlabel != blabel:
        return False
    for name, mv, bv in zip(
            case["names"], mvalues["tuple"], bvalues["tuple"]):
        if mv == bv:
            continue
        if name not in slots and per_pass_lists(mv, bv, slots):
            continue
        if name not in slots or case["owners"][name] != mlabel:
            return False
        if mv == UNSET and bv == {"list": []}:
            continue
        if not last_pass_of(mv, bv):
            return False
    return True


def rule_same_reads_repeat_list(case, master, branch):
    """
    A SAME after an uncaptured REPEAT compares against the list of the
    per-pass values of the slot it names, where master compared against
    the last pass
    """

    if branch.get("refused") is not None or "raised" in branch:
        return False
    for spec in case_specs(case):
        slots = bare_repeat_slots(spec)
        if any(node[0] == "SAME" and node[1] in slots
               for node in spec_nodes(spec)):
            return True
    return False


def encoded_length(enc):
    """
    The element count of an encoded tuple, list or proper pair spine,
    or None
    """

    if not isinstance(enc, dict):
        return None
    if "pair" in enc and "tail" not in enc and "truncated" not in enc:
        return len(enc["pair"])
    for tag in ("tuple", "list"):
        if tag in enc:
            return len(enc[tag])
    return None


def rule_root_length_first(case, master, branch):
    """
    A standalone root checks the subject's length against its widths
    before it tests an element: a subject of a length the root cannot
    match misses, where master tested elements first and an extern
    raised
    """

    if case["kind"] != "standalone" or "raised" not in master \
            or "miss" not in branch:
        return False
    count = encoded_length(case["subject"])
    if count is None:
        return False
    low, high = spec_width(case["spec"])
    return count < low or (high is not None and count > high)


UNSET = "UNB"
EMPTY = "EMPTY"


def inline_variables(case, outcome):
    """
    The (arm label, {name: encoded value}) of an inline outcome, or
    None when it is not a result
    """

    if "result" not in outcome:
        return None
    label, values = outcome["result"]["tuple"]
    return label, dict(zip(case["names"], values["tuple"]))


def inline_variable_rule(case, master, branch, hit_arm_rule):
    """
    Whether the arm labels agree and every variable that differs is the
    unset marker on this tree and, under hit_arm_rule, belongs to the
    hit arm and is empty on master, or otherwise belongs to an arm that
    missed and was written on master
    """

    if case["kind"] != "inline":
        return False
    mv = inline_variables(case, master)
    bv = inline_variables(case, branch)
    if mv is None or bv is None or mv[0] != bv[0]:
        return False
    label = mv[0]
    differing = [n for n in case["names"] if mv[1][n] != bv[1][n]]
    if not differing:
        return False
    for name in differing:
        if bv[1][name] != UNSET:
            return False
        owner = case["owners"][name]
        if hit_arm_rule:
            if owner != label or mv[1][name] != EMPTY:
                return False
        elif owner == label:
            return False
    return True


def rule_missed_arm_writes_nothing(case, master, branch):
    """
    A missed inline arm writes no variable
    """

    return inline_variable_rule(case, master, branch, False)


def rule_empty_slot_left(case, master, branch):
    """
    A hit leaves the variable of an empty slot as it was
    """

    return inline_variable_rule(case, master, branch, True)


def rule_pair_literal_inline(case, master, branch):
    """
    A pair LITERAL in a compile/match pattern is refused
    """

    return (case["kind"] == "inline"
            and branch.get("refused") is not None
            and any(node[0] == "LIT" and isinstance(node[1], dict)
                    and "pair" in node[1]
                    for s in case["arms"] for node in spec_nodes(s)))


RULES = (
    ("improper_subject_is_miss", rule_improper_subject),
    ("depth_refused", rule_depth_refused),
    ("pair_literal_refused_inline", rule_pair_literal_inline),
    ("non_pool_literal_refused", rule_non_pool_literal),
    ("refused_single_value_width", rule_single_value_width),
    ("refused_field_width", rule_field_width),
    ("refused_repeat_zero_body", rule_repeat_zero_body),
    ("refused_variable_after_rest", rule_variable_after_rest),
    ("refused_capture_under_not", rule_capture_under_not),
    ("element_read_when_pulled", rule_element_read_when_pulled),
    ("non_sequence_root_miss", rule_non_sequence_root),
    ("repeat_slot_lists", rule_repeat_slot_lists),
    ("same_reads_repeat_list", rule_same_reads_repeat_list),
    ("root_length_first", rule_root_length_first),
    ("missed_arm_writes_nothing", rule_missed_arm_writes_nothing),
    ("empty_slot_left_as_was", rule_empty_slot_left),
)


# Changes from the current lowering where master already behaves as
# the design states; they are not acceptance exceptions. Each is
# reported with its reason and no predicate, so a divergence on them
# stays unexplained.
MASTER_ALREADY = (
    ("not_tests_after_position",
     "master's NOT tests its sub-pattern at the cursor"),
    ("arm_roots_lowered",
     "master's MATCH_CASE runs a root of any kind over a one-element"
     " spine"),
    ("no_trailing_oneof_yield",
     "master's ONEOF commits to the first branch that matches"),
)


# === Comparison ===


def strip_binding(enc):
    """
    The encoding with every Datum binding removed
    """

    if isinstance(enc, list):
        return [strip_binding(v) for v in enc]
    if isinstance(enc, dict):
        return {k: strip_binding(v) for k, v in enc.items()
                if not (k == "binding" and "datum" in enc)}
    return enc


def frames_as_dicts(enc):
    """
    The encoding with every frame tag written as dict
    """

    if isinstance(enc, list):
        return [frames_as_dicts(v) for v in enc]
    if isinstance(enc, dict):
        return {("dict" if k == "frame" else k): frames_as_dicts(v)
                for k, v in enc.items()}
    return enc


def outcome_kind(outcome):
    """
    hit, miss, refused, raised or result
    """

    for kind in ("hit", "miss", "refused", "raised", "result"):
        if kind in outcome:
            return kind
    raise ValueError(f"not an outcome: {outcome!r}")


def same_outcome(master, branch):
    """
    Whether two outcomes agree: the same kind, the same captures or
    result, the same exception type
    """

    kind = outcome_kind(master)
    if kind != outcome_kind(branch):
        return False
    return master[kind] == branch[kind]


def divergence_class(master, branch):
    """
    A short description of how two differing outcomes differ
    """

    mk, bk = outcome_kind(master), outcome_kind(branch)
    if mk != bk:
        if bk in ("refused", "raised"):
            return f"{mk} -> {bk} {branch[bk]}"
        if mk in ("refused", "raised"):
            return f"{mk} {master[mk]} -> {bk}"
        return f"{mk} -> {bk}"
    if mk in ("refused", "raised"):
        return f"{mk} {master[mk]} -> {branch[bk]}"
    mv, bv = master[mk], branch[bk]
    if strip_binding(mv) == strip_binding(bv):
        return f"{mk} differs: Datum binding only"
    if frames_as_dicts(strip_binding(mv)) == frames_as_dicts(strip_binding(bv)):
        return f"{mk} differs: frame/dict and binding only"
    return f"{mk} differs: values"


def load_corpus(directory=None):
    """
    The standalone and inline records of the corpus
    """

    directory = directory or ORACLE_DIR
    with open(join(directory, "standalone.json")) as fd:
        standalone = json.load(fd)
    with open(join(directory, "inline.json")) as fd:
        inline = json.load(fd)
    return standalone, inline


def compare(selector=None, directory=None):
    """
    Run the corpus on this tree and compare each outcome with master's

    :param selector: chooses the matcher path the cases run on: true
        for the matcher core, false for the current path, None for the
        path match_core.SELECTED names
    :param directory: the corpus directory, ORACLE_DIR by default
    :return: a dict: total, agree, explained (rule name to count),
        explained_ids (rule name to case ids) and unexplained (a list
        of the divergent cases no rule covers)
    """

    if selector is not None:
        selected = match_core.SELECTED
        match_core.SELECTED = bool(selector)
        try:
            return compare(None, directory)
        finally:
            match_core.SELECTED = selected

    standalone, inline = load_corpus(directory)
    report = {
        "total": 0,
        "agree": 0,
        "explained": {name: 0 for name, _ in RULES},
        "explained_ids": {name: [] for name, _ in RULES},
        "unexplained": [],
    }

    def tally(case, master, branch):
        report["total"] += 1
        if same_outcome(master, branch):
            report["agree"] += 1
            return
        for name, predicate in RULES:
            if predicate(case, master, branch):
                report["explained"][name] += 1
                report["explained_ids"][name].append(case["id"])
                return
        report["unexplained"].append(dict(
            case, master=master, branch=branch,
            divergence=divergence_class(master, branch)))

    for record in standalone["records"]:
        outcomes = run_standalone(record)
        for index, (entry, branch) in enumerate(
                zip(record["cases"], outcomes)):
            case = {
                "kind": "standalone",
                "id": f"{record['id']}:{index}",
                "spec": record["spec"],
                "subject": entry["subject"],
            }
            tally(case, entry["master"], branch)

    for record in inline["records"]:
        outcomes = run_inline(record)
        for index, (entry, branch) in enumerate(
                zip(record["cases"], outcomes)):
            case = {
                "kind": "inline",
                "id": f"{record['id']}:{index}",
                "arms": record["arms"],
                "names": record["names"],
                "owners": record["owners"],
                "source": record["source"],
                "subject": entry["subject"],
            }
            tally(case, entry["master"], branch)

    return report


def format_report(report, limit=20):
    """
    The report as text: totals, per-rule counts, the unexplained
    divergences counted by class, and up to limit examples per class
    """

    lines = [
        f"total {report['total']}, agree {report['agree']},"
        f" explained {sum(report['explained'].values())},"
        f" unexplained {len(report['unexplained'])}",
        "",
        "rules:",
    ]
    for name, count in report["explained"].items():
        lines.append(f"  {name}: {count}")
    for name, reason in MASTER_ALREADY:
        lines.append(f"  {name}: no predicate; {reason}")

    by_class = {}
    for case in report["unexplained"]:
        key = (case["kind"], case["divergence"])
        by_class.setdefault(key, []).append(case)

    lines.append("")
    lines.append("unexplained:")
    for (kind, divergence), cases in sorted(
            by_class.items(), key=lambda kv: -len(kv[1])):
        lines.append(f"  {len(cases):5d}  {kind}  {divergence}")
        for case in cases[:limit]:
            lines.append(f"         {case['id']}")
    return "\n".join(lines)


if __name__ == "__main__":
    print(format_report(compare(), limit=int(sys.argv[1]) if len(sys.argv) > 1 else 5))


# The end.
