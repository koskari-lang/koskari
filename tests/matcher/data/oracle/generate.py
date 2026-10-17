"""
Matcher oracle corpus generator.

Generates, from a fixed seed, standalone pattern specs with subjects and
inline match arm cases (Koskari source), runs them on the tree it is
started under, and writes standalone.json and inline.json to OUTDIR.
With --seed N it uses seed N and writes standalone-N.json and
inline-N.json. The specs, the value codec, the extern set and the
inline loader come from tests/matcher/oracle.py, imported as the
top-level module oracle.

Master (the corpora):

    MASTER_PATH=$PWD/tests/matcher \
        tests/matcher/data/oracle/run-master.sh tests/matcher/data/oracle/generate.py OUTDIR
    MASTER_PATH=$PWD/tests/matcher \
        tests/matcher/data/oracle/run-master.sh tests/matcher/data/oracle/generate.py OUTDIR --seed 77777

Branch (a self-check of the generator only):

    uv run python3 -I tests/matcher/data/oracle/generate.py OUTDIR --path tests/matcher
"""

import json
import random
import sys

if "--path" in sys.argv:
    sys.path.insert(0, sys.argv[sys.argv.index("--path") + 1])

import koskari  # noqa: F401
import koskari.lib as L
from koskari.lib import cons, demand, nil
from koskari.matcher import MATCH, MatchCompiler, parse_case_pattern
from koskari.reader import IndentAwareStream, default_reader, source_str

import oracle
from oracle import (
    EXTERNS, build_pattern, decode, encode, load_inline)


MASTER = hasattr(L, "Matcher")

SEED = 20261017
# the suffix of the two file names: none for the default seed, -N for
# --seed N
SUFFIX = ""
if "--seed" in sys.argv:
    SEED = int(sys.argv[sys.argv.index("--seed") + 1])
    SUFFIX = f"-{SEED}"
PATTERNS = 420
SUBJECTS = 8
INLINE = 300
INLINE_SUBJECTS = 6
NEST_DEPTHS = (40, 400, 3000, 60000)


# === Values ===


def atom(r):
    k = r.random()
    if k < 0.40:
        return r.choice([1, 2, 9, 4, 0])
    if k < 0.48:
        return "k"
    if k < 0.52:
        return "x"
    if k < 0.55:
        return True
    if k < 0.58:
        return None
    if k < 0.63:
        return {"keyword": "k"}
    if k < 0.68:
        return {"symbol": "s"}
    if k < 0.75:
        return {"dict": [["j", {"tuple": [1, 2]}], ["k", r.choice([1, 2])]]}
    if k < 0.82:
        return {"Pt": [r.choice([1, 2]), {"tuple": [9]}]}
    if k < 0.84:
        return {"Box": 1}
    if k < 0.87:
        return {"tuple": [1, 2]}
    return r.choice([1, 2, 9])


def boxed(r, enc):
    if r.random() < 0.3:
        return {"datum": enc, "position": {"tuple": [3, r.randint(0, 9)]}}
    return {"datum": enc}


def sequence(r, d):
    items = []
    for _ in range(r.randint(0, 5)):
        if r.random() < 0.3 and d > 0:
            items.append(subject(r, d - 1))
        else:
            items.append(atom(r))
        if r.random() < 0.1:
            items[-1] = boxed(r, items[-1])
    return items


def subject(r, d=2, single=0.04):
    """
    One encoded subject: mostly a sequence (pair spine, tuple, list),
    sometimes improper, a single value, a mapping, a Pt, or boxed
    """

    k = r.random()
    if k < single:
        return atom(r)
    k = r.random()
    items = sequence(r, d)
    if k < 0.50:
        return {"pair": items}
    if k < 0.74:
        return {"tuple": items}
    if k < 0.87:
        return {"list": items}
    if k < 0.91:
        return {"pair": items or [1], "tail": r.choice([2, "x"])}
    if k < 0.94:
        return {"dict": [["k", r.choice([1, 2])], ["j", {"pair": items}]]}
    if k < 0.96:
        return {"Pt": [r.choice([1, 2]), {"tuple": items}]}
    return boxed(r, {"pair": items})


def inline_subject(r):
    """
    One encoded single-value subject for an inline arm
    """

    if r.random() < 0.35:
        value = atom(r)
        return boxed(r, value) if r.random() < 0.1 else value
    return subject(r, 2, single=0.0)


def inline_witness(r, arms):
    """
    A single-value subject from one arm's witness: its one element, or
    an inline subject when the witness is not one element
    """

    items = witness(r, r.choice(arms), {})
    if len(items) == 1:
        return items[0]
    return inline_subject(r)


TYPE_WITNESS = {
    "int": 4, "Num": 2, "str": "k", "symbol": {"symbol": "s"},
    "keyword": {"keyword": "k"}, "pair": {"pair": [1, 2]},
    "tuple": {"tuple": [1]}, "list": {"list": [2]},
    "Pt": {"Pt": [1, 2]}, "Box": {"Box": 1},
}
PRED_WITNESS = {"even": [0, 2, 4], "big": [4, 9], "boom": [1, 2],
                "poke": [1]}


def container(r, items):
    k = r.random()
    if k < 0.5:
        return {"pair": items}
    if k < 0.8:
        return {"tuple": items}
    return {"list": items}


def witness(r, spec, env):
    """
    A list of encoded elements that spec is likely to match; env maps
    the slots bound so far to their encoded values
    """

    tag = spec[0]
    if tag == "ANY":
        return [atom(r)]
    if tag == "LIT":
        return [spec[1]]
    if tag == "TYPE":
        return [TYPE_WITNESS[spec[1]]]
    if tag == "PRED":
        return [r.choice(PRED_WITNESS[spec[1]])]
    if tag == "SAME":
        return [env.get(spec[1], atom(r))]
    if tag == "FORM":
        return [container(r, witness(r, spec[1], env))]
    if tag == "NEST":
        return [atom(r)]
    if tag == "APP":
        if spec[1] == "first":
            inner = witness(r, spec[2], env) if spec[2] else [atom(r)]
            return [{"tuple": inner[:1] + [atom(r)]}]
        if spec[1] == "inc":
            return [r.choice([0, 1, 3, 8])]
        return [atom(r)]
    if tag == "CLASS":
        values = []
        for field in spec[2:]:
            item = witness(r, field[1], env)
            values.append(item[0] if item else 1)
        values += [1, 2]
        if spec[1] == "Box":
            return [{"Box": values[0]}]
        return [{"Pt": values[:2]}]
    if tag == "MAP":
        items = []
        for entry in spec[1:]:
            key = EXTERNS[entry[1]] if entry[2] else entry[1]
            value = witness(r, entry[3], env) if entry[3] else [1]
            items.append([key, value[0] if value else 1])
        return [{"dict": items}]
    if tag in ("MATCH", "GROUP"):
        out = []
        for child in oracle.spec_children(spec):
            out += witness(r, child, env)
        return out
    if tag == "ONEOF":
        return witness(r, r.choice(spec[1:]), env)
    if tag == "OPT":
        return witness(r, spec[1], env) if r.random() < 0.5 else []
    if tag == "REPEAT":
        out = []
        for _ in range(spec[2] + r.randint(0, 2)):
            out += witness(r, spec[1], env)
        return out
    if tag == "REST":
        return [atom(r) for _ in range(spec[2] + r.randint(0, 2))]
    if tag == "NOT":
        return []
    if tag == "CAP":
        out = witness(r, spec[2], env) if spec[2] else [atom(r)]
        if len(out) == 1:
            env[spec[1]] = out[0]
        return out
    raise AssertionError(tag)


def witness_subject(r, spec):
    """
    An encoded sequence subject built from spec's witness, sometimes
    with one element changed
    """

    items = witness(r, spec, {})
    if items and r.random() < 0.25:
        items[r.randrange(len(items))] = atom(r)
    return container(r, items)


# === Standalone specs ===


CORE_TYPES = ["int", "str", "symbol", "keyword", "pair", "tuple", "list"]
POOL_LITERALS = [
    1, 2, 9, 0, True, None, "k", "x", {"keyword": "k"}, {"symbol": "s"},
    {"tuple": [1, 2]}]
NON_POOL_LITERALS = [
    {"list": [1]}, {"pair": [1, 2]}, {"dict": [["k", 1]]}]


class SpecGen:
    """
    Random standalone specs over the base node set
    """

    def __init__(self, r):
        self.r = r
        self.n = 0
        self.bound = []
        self.scopes = [[]]

    def slot(self):
        self.n += 1
        name = f"s{self.n}"
        self.bound.append(name)
        self.scopes[-1].append(name)
        return name

    def scoped(self, make):
        """
        make() run inside a new capture scope
        """

        self.scopes.append([])
        try:
            return make()
        finally:
            self.scopes.pop()

    def literal(self):
        if self.r.random() < 0.03:
            return ["LIT", self.r.choice(NON_POOL_LITERALS)]
        return ["LIT", self.r.choice(POOL_LITERALS)]

    def type_test(self):
        r = self.r
        if r.random() < 0.75:
            return ["TYPE", r.choice(CORE_TYPES)]
        return ["TYPE", r.choice(["Num", "Pt", "Box"])]

    def predicate(self):
        r = self.r
        k = r.random()
        if k < 0.75:
            return ["PRED", "even"]
        if k < 0.93:
            return ["PRED", "big"]
        return ["PRED", "boom"]

    def test(self):
        """
        A capture-free element test
        """

        k = self.r.random()
        if k < 0.45:
            return self.literal()
        if k < 0.75:
            return self.type_test()
        if k < 0.9:
            return self.predicate()
        return ["ANY"]

    def same(self):
        r = self.r
        k = r.random()
        visible = [(depth, name)
                   for depth, scope in enumerate(reversed(self.scopes))
                   for name in scope]
        if visible and k < 0.85:
            depth, name = r.choice(visible)
            return ["SAME", name, depth]
        depth = 0 if k < 0.9 else (1 if k < 0.96 else 2)
        return ["SAME", r.choice(self.bound), depth]

    def odd_width(self, d):
        """
        A sub-pattern whose width is not 1, for a single-value position
        """

        r = self.r
        k = r.random()
        if k < 0.3:
            return ["OPT", self.value(0)]
        if k < 0.5:
            return ["NOT", self.literal()]
        if k < 0.75:
            return ["MATCH", self.value(0), self.value(0)]
        return ["CAP", self.slot(), ["REST", None, 0]]

    def value(self, d):
        """
        A width-1 sub-pattern (rarely another width) for a projected
        value or an element
        """

        r = self.r
        opts = ["cap", "lit", "any", "type", "captype", "pred"]
        if d > 0:
            opts += ["form", "form", "oneof1", "class", "map", "app", "odd"]
        if self.bound:
            opts.append("same")
        k = r.choice(opts)
        if k == "cap":
            return ["CAP", self.slot(), None]
        if k == "lit":
            return self.literal()
        if k == "any":
            return ["ANY"]
        if k == "type":
            return self.type_test()
        if k == "captype":
            return ["CAP", self.slot(), self.type_test()]
        if k == "pred":
            return ["CAP", self.slot(), self.predicate()] \
                if r.random() < 0.6 else self.predicate()
        if k == "same":
            return self.same()
        if k == "form":
            return ["FORM", ["MATCH", *self.seq(d - 1)]]
        if k == "oneof1":
            return ["ONEOF", self.value(d - 1), self.value(d - 1)]
        if k == "class":
            name = "Pt" if r.random() < 0.9 else "Box"
            fields = [
                ["FIELD", self.value(d - 1), None]
                for _ in range(r.choice([0, 1, 1, 2, 2, 3]))]
            if r.random() < 0.5:
                fields.append(
                    ["FIELD", self.value(d - 1), r.choice(["x", "y", "z"])])
            if r.random() < 0.05:
                fields.append(["FIELD", self.odd_width(d - 1), None])
            return ["CLASS", name, *fields]
        if k == "map":
            entries = []
            for _ in range(r.randint(1, 2)):
                sub = self.value(d - 1) if r.random() < 0.8 else None
                if r.random() < 0.25:
                    entries.append(
                        ["ENTRY", r.choice(["kx", "kj"]), True, sub])
                else:
                    entries.append(
                        ["ENTRY", r.choice(["k", "j", "q", 1]), False, sub])
            if r.random() < 0.05:
                entries.append(["ENTRY", "k", False, self.odd_width(d - 1)])
            return ["MAP", *entries]
        if k == "app":
            name = r.choice(["inc", "first", "twice"])
            if r.random() < 0.2:
                return ["APP", name, None]
            if name == "twice" and r.random() < 0.6:
                return ["APP", name, ["FORM", ["MATCH", *self.seq(d - 1)]]]
            return ["APP", name, self.value(d - 1)]
        if k == "odd":
            if r.random() < 0.15:
                return ["APP", "inc", self.odd_width(d - 1)]
            return self.value(d - 1)
        raise AssertionError(k)

    def oneof(self, d, need1):
        r = self.r
        if r.random() < 0.3:
            name = self.slot()
            other = self.slot()
            return ["ONEOF", ["CAP", name, None],
                    ["MATCH", ["CAP", name, None], ["CAP", other, None]]]
        return ["ONEOF", *[self.elem(d - 1, need1)
                           for _ in range(r.randint(2, 3))]]

    def elem(self, d, need1=False):
        """
        One element of a sequence
        """

        r = self.r
        opts = ["value", "value", "value", "value"]
        if d > 0:
            opts += ["oneof", "group", "namedgroup", "capgroup", "caprep",
                     "rep", "caponeof", "capspan"]
            if not need1:
                opts += ["opt", "not", "not", "rest"]
        k = r.choice(opts)
        if k == "value":
            return self.value(d)
        if k == "oneof":
            return self.oneof(d, need1)
        if k == "group":
            return ["GROUP", None, *self.seq(d - 1, need1=True)]
        if k == "namedgroup":
            children = self.scoped(lambda: self.seq(d - 1, need1=True))
            return ["GROUP", self.slot(), *children]
        if k == "capgroup":
            children = self.scoped(lambda: self.seq(d - 1, True))
            return ["CAP", self.slot(), ["GROUP", None, *children]]
        if k == "caprep":
            if r.random() < 0.3:
                body = ["GROUP", None,
                        *self.scoped(lambda: self.seq(d - 1, True))]
            else:
                body = self.scoped(lambda: self.elem(d - 1, r.random() > 0.1))
            return ["CAP", self.slot(), ["REPEAT", body, r.choice([0, 0, 1, 2])]]
        if k == "rep":
            body = self.scoped(lambda: self.elem(d - 1, r.random() > 0.1))
            return ["REPEAT", body, r.choice([0, 0, 1])]
        if k == "caponeof":
            return ["CAP", self.slot(),
                    ["ONEOF", self.elem(d - 1, True), self.elem(d - 1, True)]]
        if k == "capspan":
            return ["CAP", self.slot(), ["MATCH", *self.seq(d - 1, True)]]
        if k == "opt":
            return ["OPT", self.elem(d - 1)]
        if k == "not":
            k = r.random()
            if k < 0.75:
                return ["NOT", self.test()]
            if k < 0.85:
                return ["NOT", self.value(0)]
            return ["NOT", self.elem(d - 1)]
        if k == "rest":
            slot = self.slot() if r.random() < 0.7 else None
            rest = ["REST", None, r.choice([0, 0, 1, 2])]
            return ["CAP", slot, rest] if slot else rest
        raise AssertionError(k)

    def seq(self, d, need1=False):
        n = self.r.randint(1 if need1 else 0, 3)
        out = [self.elem(d) for _ in range(n)]
        if need1 and not out:
            out = [["CAP", self.slot(), None]]
        return out


def standalone_records(r):
    records = []
    for index in range(PATTERNS):
        spec = ["MATCH", *SpecGen(r).seq(3)]
        subjects = [subject(r) for _ in range(SUBJECTS // 2)]
        subjects += [witness_subject(r, spec)
                     for _ in range(SUBJECTS - SUBJECTS // 2)]
        records.append({"id": f"p{index:04d}", "spec": spec,
                        "subjects": subjects})

    poke_specs = [
        ["MATCH", ["PRED", "poke"], ["CAP", "a", None]],
        ["MATCH", ["PRED", "poke"], ["CAP", "a", None], ["CAP", "b", None]],
        ["MATCH", ["CAP", "p", ["PRED", "poke"]], ["LIT", 99]],
        ["MATCH", ["PRED", "poke"], ["CAP", "xs", ["REPEAT", ["ANY"], 0]]],
        ["MATCH", ["PRED", "poke"], ["ANY"], ["CAP", "b", None]],
        ["MATCH", ["CAP", "xs", ["REPEAT", ["ONEOF", ["PRED", "poke"],
                                             ["CAP", "v", None]], 0]]],
    ]
    poke_subjects = [
        {"pair": [{"poke": 1}, 1]},
        {"pair": [{"poke": 1}, 1, 2]},
        {"pair": [{"poke": 2}, 1, 2]},
        {"list": [{"poke": 1}, 1]},
        {"list": [{"poke": 2}, 1, 2]},
        {"tuple": [{"poke": 1}, 1]},
        {"pair": [1, {"poke": 1}, 2]},
        {"pair": [{"poke": 1}, {"poke": 1}, 3]},
    ]
    for index, spec in enumerate(poke_specs):
        records.append({"id": f"poke{index}", "spec": spec,
                        "subjects": poke_subjects})

    for depth in NEST_DEPTHS:
        records.append({
            "id": f"nest{depth}",
            "spec": ["MATCH", ["NEST", depth, ["ANY"]]],
            "subjects": [{"nest": depth, "leaf": {"list": [0]}},
                         {"nest": depth - 1, "leaf": {"list": [0]}}]})
    return records


# === Inline arms ===


class InlineGen(SpecGen):
    """
    Specs the matcher dialect can write in an arm, rendered by render
    """

    def literal(self):
        if self.r.random() < 0.05:
            return ["LIT", {"pair": [1, 2]}]
        return ["LIT", self.r.choice([1, 2, 9, "k", {"keyword": "k"},
                                      {"symbol": "s"}])]

    def value(self, d):
        r = self.r
        opts = ["cap", "lit", "any", "type", "captype", "pred"]
        if d > 0:
            opts += ["form", "form", "oneof1", "class", "app"]
        k = r.choice(opts)
        if k == "form":
            return ["FORM", ["MATCH", *self.seq(d - 1)]]
        if k == "class":
            fields = [["FIELD", self.value(d - 1), None]
                      for _ in range(r.choice([0, 1, 2]))]
            if r.random() < 0.4:
                fields.append(["FIELD", self.value(d - 1), r.choice(["x", "y"])])
            return ["CLASS", "Pt", *fields]
        if k == "app":
            name = r.choice(["inc", "first", "twice"])
            if r.random() < 0.2:
                return ["APP", name, None]
            if name == "twice":
                return ["APP", name, ["FORM", ["MATCH", *self.seq(d - 1)]]]
            return ["APP", name, self.value(d - 1)]
        if k == "pred":
            return ["CAP", self.slot(), self.predicate()] \
                if r.random() < 0.5 else self.predicate()
        if k == "cap":
            return ["CAP", self.slot(), None]
        if k == "lit":
            return self.literal()
        if k == "any":
            return ["ANY"]
        if k == "type":
            return self.type_test()
        if k == "captype":
            return ["CAP", self.slot(), self.type_test()]
        if k == "oneof1":
            return ["ONEOF", self.value(d - 1), self.value(d - 1)]
        raise AssertionError(k)

    def predicate(self):
        return ["PRED", self.r.choice(["even", "even", "big"])]

    def elem(self, d, need1=False):
        r = self.r
        opts = ["value", "value", "value"]
        if d > 0:
            opts += ["oneof", "group", "namedgroup", "caprep", "rep",
                     "capspan"]
            if not need1:
                opts += ["opt", "not", "rest"]
        k = r.choice(opts)
        if k == "value":
            return self.value(d)
        if k == "oneof":
            return ["ONEOF", *[self.elem(d - 1, need1)
                               for _ in range(r.randint(2, 3))]]
        if k == "caprep":
            return ["CAP", self.slot(),
                    ["REPEAT", ["GROUP", None, self.elem(d - 1, True)],
                     r.choice([0, 1, 2])]]
        if k == "rep":
            return ["REPEAT", self.elem(d - 1, True), 0]
        if k == "rest":
            if r.random() < 0.7:
                return ["REST", self.slot(), r.choice([0, 0, 1])]
            return ["REST", None, 0]
        if k == "not":
            return ["NOT", self.test() if r.random() < 0.85 else self.value(0)]
        if k == "capspan":
            return ["CAP", self.slot(), ["MATCH", *self.seq(d - 1, True)]]
        if k == "group":
            return ["GROUP", None, *self.seq(d - 1, need1=True)]
        if k == "namedgroup":
            return ["GROUP", self.slot(), *self.seq(d - 1, need1=True)]
        if k == "opt":
            return ["OPT", self.elem(d - 1)]
        raise AssertionError(k)

    def one_wide(self):
        """
        Children whose width range mostly includes 1: one element, with
        zero-width or optional neighbours
        """

        r = self.r
        if r.random() < 0.25:
            return self.seq(1, need1=True)
        out = [self.value(1)]
        if r.random() < 0.4:
            out.insert(r.randint(0, 1), ["NOT", self.test()])
        if r.random() < 0.3:
            out.append(["OPT", self.value(0)])
        return out

    def root(self):
        """
        An arm root of any node kind
        """

        r = self.r
        k = r.choice(["value", "value", "value", "form", "match", "match",
                      "group", "namedgroup", "oneof", "opt", "rep", "caprep",
                      "rest", "not", "map", "capspan"])
        if k == "value":
            return self.value(2)
        if k == "form":
            return ["FORM", ["MATCH", *self.seq(2)]]
        if k == "match":
            return ["MATCH", *self.one_wide()]
        if k == "group":
            return ["GROUP", None, *self.one_wide()]
        if k == "namedgroup":
            children = self.one_wide()
            return ["GROUP", self.slot(), *children]
        if k == "capspan":
            children = self.one_wide()
            return ["CAP", self.slot(), ["MATCH", *children]]
        if k == "oneof":
            return ["ONEOF", *[
                self.value(1) if r.random() < 0.6
                else ["MATCH", *self.one_wide()]
                for _ in range(r.randint(2, 3))]]
        if k == "opt":
            return ["OPT", self.value(1) if r.random() < 0.7 else self.elem(1)]
        if k == "rep":
            return ["REPEAT", self.value(1), 0]
        if k == "caprep":
            return ["CAP", self.slot(),
                    ["REPEAT", ["GROUP", None, self.value(1)],
                     r.choice([0, 0, 1, 2])]]
        if k == "rest":
            slot = self.slot() if r.random() < 0.7 else None
            return ["REST", slot, r.choice([0, 0, 1, 2]) if slot else 0]
        if k == "not":
            return ["NOT", self.test()]
        if k == "map":
            return ["MAP", *[["ENTRY", key, False, self.value(1)]
                             for key in r.sample(["k", "j"], r.randint(1, 2))]]
        raise AssertionError(k)


def render_literal(enc):
    if isinstance(enc, bool) or enc is None:
        raise ValueError(enc)
    if isinstance(enc, int):
        return f"(LITERAL {enc})"
    if isinstance(enc, str):
        return f"(LITERAL {json.dumps(enc)})"
    if "keyword" in enc:
        return f"(LITERAL_KW {json.dumps(enc['keyword'])})"
    if "symbol" in enc:
        return f"(LITERAL_SYM {json.dumps(enc['symbol'])})"
    if "pair" in enc:
        return "(LITERAL (" + " ".join(str(v) for v in enc["pair"]) + "))"
    raise ValueError(enc)


def render(spec):
    """
    The matcher dialect source of a spec
    """

    tag = spec[0]
    if tag == "ANY":
        return "(TEST_ANY)"
    if tag == "LIT":
        return render_literal(spec[1])
    if tag == "TYPE":
        return f"(TYPE {spec[1]})"
    if tag == "PRED":
        return f"(PREDICATE {spec[1]})"
    if tag == "FORM":
        return f"(FORM {render(spec[1])})"
    if tag == "APP":
        if spec[2] is None:
            return f"(APP {spec[1]})"
        return f"(APP {spec[1]} {render(spec[2])})"
    if tag == "CLASS":
        fields = []
        for field in spec[2:]:
            if field[2] is None:
                fields.append(f"(FIELD {render(field[1])})")
            else:
                fields.append(f"(FIELD {field[2]} {render(field[1])})")
        return f"(CLASS {spec[1]} {' '.join(fields)})".replace(" )", ")")
    if tag == "MATCH":
        return "(MATCH " + " ".join(render(c) for c in spec[1:]) + ")"
    if tag == "GROUP":
        name = "()" if spec[1] is None else spec[1]
        return f"(GROUP {name} " + " ".join(render(c) for c in spec[2:]) + ")"
    if tag == "ONEOF":
        return "(ONEOF " + " ".join(render(c) for c in spec[1:]) + ")"
    if tag == "OPT":
        return f"(OPTIONAL {render(spec[1])})"
    if tag == "NOT":
        return f"(NOT {render(spec[1])})"
    if tag == "REPEAT":
        if spec[2] != 0:
            raise ValueError(spec)
        return f"(REPEAT {render(spec[1])})"
    if tag == "REST":
        if spec[1] is None:
            if spec[2]:
                raise ValueError(spec)
            return "(REST)"
        return f"(REST {spec[1]} min: {spec[2]})"
    if tag == "CAP":
        sub = spec[2]
        if sub is None:
            return spec[1]
        if sub[0] == "REPEAT" and sub[1][0] == "GROUP" and sub[1][1] is None \
                and len(sub[1]) == 3:
            return f"(REPEAT {spec[1]} {render(sub[1][2])} min: {sub[2]})"
        return f"(CAPTURE {spec[1]} {render(sub)})"
    raise ValueError(spec)


def render_root(spec):
    """
    The arm delimiter's content for a root spec
    """

    if spec[0] == "CLASS":
        raise ValueError("the case dialect has no CLASS head")
    if spec[0] == "MAP":
        entries = " ".join(
            f"{json.dumps(e[1])}: {render(e[3])}" for e in spec[1:])
        return "#{" + entries + "}"
    return render(spec)


def slot_names(spec):
    out = []
    for node in oracle.spec_nodes(spec):
        if oracle.is_capture_node(node) and node[1] not in out:
            out.append(node[1])
    return out


def read_delimiter(text):
    stream = IndentAwareStream.from_source(source_str(f"[{text}]", "<gen>"))
    return default_reader.read(stream)


def renders_as_spec(text, spec):
    """
    Whether the delimiter text lowers to the same tables as the spec
    """

    parsed = parse_case_pattern(read_delimiter(text))
    return (MatchCompiler().lower(MATCH(parsed))
            == MatchCompiler().lower(MATCH(build_pattern(spec))))


def inline_records(r):
    records = []
    dropped = 0
    while len(records) < INLINE:
        gen = InlineGen(r)
        arms = [gen.root() for _ in range(1 if r.random() < 0.4 else 2)]
        try:
            texts = [render_root(a) for a in arms]
            if not all(renders_as_spec(t, a) for t, a in zip(texts, arms)):
                dropped += 1
                continue
        except Exception:
            dropped += 1
            continue
        names = []
        owners = {}
        for index, arm in enumerate(arms):
            for name in slot_names(arm):
                names.append(name)
                owners[name] = f"arm{index + 1}"
        defines = " ".join(f'(define {n} "UNB")' for n in names)
        clauses = " ".join(
            f'[[{t}] "arm{i + 1}"]' for i, t in enumerate(texts))
        reads = " ".join(f'(try {n} (except: "EMPTY"))' for n in names)
        source = (
            f"(define case_fn [data] {defines} (define outcome (match data"
            f' {clauses} [else: "miss"])) #(outcome #({reads})))\n')
        records.append({
            "id": f"i{len(records):04d}",
            "arms": arms,
            "names": names,
            "owners": owners,
            "source": source,
            "subjects": [inline_subject(r)
                         for _ in range(INLINE_SUBJECTS // 2)]
            + [inline_witness(r, arms)
               for _ in range(INLINE_SUBJECTS - INLINE_SUBJECTS // 2)],
        })
    return records, dropped


# === Running ===


def run_master_standalone(record):
    try:
        matcher = build_pattern(record["spec"]).build()
    except Exception as exc:
        return [{"refused": type(exc).__name__, "message": str(exc)[:200]} for _ in record["subjects"]]
    outcomes = []
    for enc in record["subjects"]:
        try:
            res = matcher.match(decode(enc), **EXTERNS)
        except Exception as exc:
            outcomes.append({"raised": type(exc).__name__, "message": str(exc)[:200]})
            continue
        if res.matched:
            outcomes.append({"hit": encode(res.captures)})
        else:
            outcomes.append({"miss": True})
    return outcomes


def run_branch_standalone(record):
    cases = {"cases": [{"subject": s} for s in record["subjects"]]}
    return oracle.run_standalone(dict(record, **cases))


def run_inline(record):
    try:
        call = demand(load_inline(record["source"])["case_fn"])
    except Exception as exc:
        return [{"refused": type(exc).__name__, "message": str(exc)[:200]} for _ in record["subjects"]]
    outcomes = []
    for enc in record["subjects"]:
        try:
            outcomes.append({"result": encode(call(decode(enc)))})
        except Exception as exc:
            outcomes.append({"raised": type(exc).__name__, "message": str(exc)[:200]})
    return outcomes


def finish(record, outcomes):
    subjects = record.pop("subjects")
    record["cases"] = [{"subject": s, "master": o}
                       for s, o in zip(subjects, outcomes)]
    return record


def write_json(path, header, records):
    with open(path, "w") as fd:
        fd.write("{\n")
        for key, value in header.items():
            fd.write(f"{json.dumps(key)}: {json.dumps(value)},\n")
        fd.write('"records": [\n')
        fd.write(",\n".join(
            json.dumps(rec, sort_keys=True, separators=(",", ":"))
            for rec in records))
        fd.write("\n]\n}\n")


def main():
    outdir = sys.argv[1]
    r = random.Random(SEED)
    standalone = standalone_records(r)
    inline, dropped = inline_records(r)

    run_standalone = run_master_standalone if MASTER else run_branch_standalone
    standalone = [finish(rec, run_standalone(rec)) for rec in standalone]
    inline = [finish(rec, run_inline(rec)) for rec in inline]

    header = {
        "seed": SEED,
        "tree": "master" if MASTER else "branch",
        "patterns": PATTERNS,
        "subjects_per_pattern": SUBJECTS,
        "nest_depths": list(NEST_DEPTHS),
    }
    write_json(f"{outdir}/standalone{SUFFIX}.json", header, standalone)
    header = {
        "seed": SEED,
        "tree": "master" if MASTER else "branch",
        "inline": INLINE,
        "subjects_per_inline": INLINE_SUBJECTS,
        "dropped_renderings": dropped,
    }
    write_json(f"{outdir}/inline{SUFFIX}.json", header, inline)
    print(f"standalone records {len(standalone)}"
          f" cases {sum(len(rec['cases']) for rec in standalone)};"
          f" inline records {len(inline)}"
          f" cases {sum(len(rec['cases']) for rec in inline)};"
          f" dropped renderings {dropped}")


main()
