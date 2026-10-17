# Matcher oracle corpus

Pattern specs, subjects and inline `match` arms, with the outcomes
master's native matcher engine gave them. `tests/matcher/oracle.py`
reads these files, runs every case on this tree and compares.

Generated once, by `generate.py` (beside this file) under master
4d079fa3 (`run-master.sh` after `build-master.sh`, master's own build
environment), with the spec builder, value codec and extern set of
`tests/matcher/oracle.py`. Rerunning it reproduces both files byte for
byte.

## Seed and sizes

| | |
|---|---|
| seed | 20261017 (one `random.Random` for both files) |
| standalone.json | 420 generated patterns x 8 subjects (4 random, 4 built from the pattern), 6 poke patterns x 8 subjects, 4 nesting-depth patterns (40, 400, 3000, 60000) x 2 subjects: 430 records, 3416 cases |
| inline.json | 300 functions of one or two `match` arms each, x 6 single-value subjects (3 random, 3 built from an arm): 1800 cases |

## standalone.json

`records`: `{id, spec, cases: [{subject, master}]}`. The spec is the
JSON form `oracle.build_pattern` reads (its table is in oracle.py); the
pattern is built with `Pattern.build` and run against each subject with
`oracle.EXTERNS`.

## inline.json

`records`: `{id, arms, names, owners, source, cases: [{subject,
master}]}`. `source` defines `case_fn`: it binds each slot name of every
arm to `"UNB"`, runs `(match data [[ARM1] "arm1"] [[ARM2] "arm2"] [else:
"miss"])`, and returns the arm label with each name's value (`"EMPTY"`
when reading the name raises). `arms` are the arm specs the source
writes in the matcher dialect, `names` the slot names in order, and
`owners` the arm that names each slot.

## Values and outcomes

A value is JSON data: int, str, bool and null as themselves; otherwise a
one-key object, `tuple`, `list`, `pair` (with `tail` when improper),
`symbol`, `keyword`, `datum` (with `position` and `binding` when set),
`frame` and `dict` (sorted `[key, value]` lists), `Pt`, `Box`, `poke`,
`nest`, or `repr`.

An outcome is `{"hit": captures}`, `{"miss": true}`, `{"refused": type,
"message": ...}` (the build or the arm's compile raised), `{"raised":
type, "message": ...}` (the match raised), or, for an inline case,
`{"result": value}`. Exception types are compared; messages are not.

OUTDIR must be an absolute path: `run-master.sh` runs the generator from
the master build's directory.
