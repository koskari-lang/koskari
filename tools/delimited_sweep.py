"""
Delimited-continuation exercise sweep.

Each case is a standalone .ki program printing exactly one line starting
with RESULT. Cases run in separate subprocesses because a dynchain
failure in one case corrupts state for anything that follows it in the
same process (observed: a broken shift leaves CLEAR_TRY reporting "top
is not a TRY region" for a later, unrelated try block).

Run: uv run python tools/delimited_sweep.py
"""


import os
import subprocess
import sys
import tempfile


# (id, category, description, source, expected-RESULT-line)
CASES = []


def case(cid, cat, desc, src, want):
    CASES.append((cid, cat, desc, src.strip() + "\n", want))


# === 1. Where the shift sits relative to its reset ===

case("pos-lexical", "position",
     "shift lexically in reset body",
     '(define r (reset (shift k (k 1))))\n(print "RESULT" r)',
     "RESULT 1")

case("pos-arith", "position",
     "shift lexical, one pending operand in region",
     '(define r (reset (+ 1 (shift k (k 5)))))\n(print "RESULT" r)',
     "RESULT 6")

case("pos-let", "position",
     "shift lexical, inside a let in the region",
     '(define r (reset (let [[z 1]] (+ z (shift k (k 5))))))\n'
     '(print "RESULT" r)',
     "RESULT 6")

case("pos-nested-build", "position",
     "shift lexical, pending #list and + in region",
     '(define r (reset (#list "x" (+ 1 (shift k (k 5))) "y")))\n'
     '(print "RESULT" r)',
     "RESULT ['x', 6, 'y']")

case("pos-iife", "position",
     "shift one frame deep via immediately-invoked lambda",
     '(define r (reset ((lambda [] (shift k (k 1))))))\n(print "RESULT" r)',
     "RESULT 1")

case("pos-callee", "position",
     "shift one frame deep in a named function",
     'define f []:\n  (shift k (k 1))\n'
     '(define r (reset (f)))\n(print "RESULT" r)',
     "RESULT 1")

case("pos-callee-arith", "position",
     "shift one frame deep, pending operand in caller region",
     'define f []:\n  (shift k (k 5))\n'
     '(define r (reset (+ 1 (f))))\n(print "RESULT" r)',
     "RESULT 6")

case("pos-callee-own-arith", "position",
     "shift one frame deep, pending operand in callee frame",
     'define f [n]:\n  (+ n (shift k (k 100)))\n'
     '(define r (reset (+ 1 (f 2))))\n(print "RESULT" r)',
     "RESULT 103")

case("pos-callee2", "position",
     "shift two frames deep",
     'define inner []:\n  (shift k (k 1))\n'
     'define outer []:\n  (inner)\n'
     '(define r (reset (outer)))\n(print "RESULT" r)',
     "RESULT 1")


case("xframe-abandon", "position",
     "cross-frame shift that never invokes k",
     'define f []:\n  (shift k "abandoned")\n'
     '(define r (reset (+ 1 (f))))\n(print "RESULT" r)',
     "RESULT abandoned")

case("xframe-in-let", "position",
     "cross-frame shift, reset nested in a let (NS below prompt)",
     'define f []:\n  (shift k (k 5))\n'
     '(define r (let [[z 9]] (reset (+ 1 (f)))))\n'
     '(print "RESULT" r)',
     "RESULT 6")

case("xframe-in-wind", "position",
     "cross-frame shift, reset nested in a dyn-wind (WIND below prompt)",
     'define f []:\n  (shift k (k 5))\n'
     '(define log (#list))\n'
     '(define r\n'
     '  (dyn-wind (log.append "in") (reset (+ 1 (f))) (log.append "out")))\n'
     '(print "RESULT" r log)',
     "RESULT 6 ['in', 'out']")


# === 1b. Provenance: k escaping its prompt ===

case("remote-invoke", "remote",
     "k stored, invoked after the prompt unwound naturally",
     '(define-global saved None)\n'
     '(define r (reset (+ 1 (shift k (set! saved k) 100))))\n'
     '(define r2 (saved 5))\n'
     '(print "RESULT" r r2)',
     "RESULT 100 6")

case("remote-invoke-twice", "remote",
     "escaped k invoked twice after its prompt unwound",
     '(define-global saved None)\n'
     '(define r (reset (+ 1 (shift k (set! saved k) 100))))\n'
     '(define a (saved 5))\n'
     '(define b (saved 30))\n'
     '(print "RESULT" a b)',
     "RESULT 6 31")


# === 2. Operands pending in the ENCLOSING call, same frame ===

case("encl-none", "enclosing",
     "reset as sole argument",
     '(print "RESULT" (reset (+ 1 (shift k (k 5)))))',
     "RESULT 6")

case("encl-clean-region", "enclosing",
     "one enclosing operand, nothing pending in region",
     '(print "RESULT" "B" (reset (shift k (k 5))))',
     "RESULT B 5")

case("encl-one", "enclosing",
     "one enclosing operand, one pending in region",
     '(print "RESULT" "B" (reset (+ 1 (shift k (k 5)))))',
     "RESULT B 6")

case("encl-two", "enclosing",
     "two enclosing operands, two pending in region",
     '(print "RESULT" "C" "D" (reset (+ 1 2 (shift k (k 5)))))',
     "RESULT C D 8")

case("encl-three", "enclosing",
     "PREDICTION: one-slot leak -> stale 6 (=1+2+3), args slid down",
     '(print "RESULT" "B" (reset (+ 1 2 3 (shift k (k 4)))))',
     "RESULT B 10")

case("encl-nonarith", "enclosing",
     "DISCRIMINATOR: pending operand is a call arg, not a reducing op",
     '(print "RESULT" "B" (reset (max 1 (shift k (k 5)))))',
     "RESULT B 5")

case("encl-build-list", "enclosing",
     "reset inside a pending #list build",
     '(define r (#list "E" (reset (+ 1 (shift k (k 5))))))\n'
     '(print "RESULT" r)',
     "RESULT ['E', 6]")


# === 3. How k is used ===

case("k-abandon", "invoke",
     "k never invoked; shift body value wins",
     '(define r (reset (+ 1 (shift k "abandoned"))))\n(print "RESULT" r)',
     "RESULT abandoned")

case("k-twice", "invoke",
     "k invoked twice (multi-shot)",
     '(define r (reset (+ 1 (shift k (+ (k 10) (k 20))))))\n'
     '(print "RESULT" r)',
     "RESULT 32")

case("k-loop", "invoke",
     "k invoked once per element of a list",
     '(define r\n'
     '  (reset\n'
     '    (+ 100\n'
     '      (shift k\n'
     '        (let [[out (#list)]]\n'
     '          (for-each [c (#list 1 2 3)]\n'
     '            (out.append (k c)))\n'
     '          out)))))\n'
     '(print "RESULT" r)',
     "RESULT [101, 102, 103]")

case("k-callee-loop", "invoke",
     "k invoked per element, shift one frame deep (the amb shape)",
     'define collect [choices]:\n'
     '  shift k:\n'
     '    let [[out (#list)]]:\n'
     '      for-each [c choices]:\n'
     '        (out.append (k c))\n'
     '      out\n'
     '(define r (reset (+ 100 (collect (#list 1 2 3)))))\n'
     '(print "RESULT" r)',
     "RESULT [101, 102, 103]")


# === 4. Interaction with other dynamic extents ===

case("try-inside", "extent",
     "try block inside the region, shift inside the try",
     '(define r\n'
     '  (reset\n'
     '    (try\n'
     '      (+ 1 (shift k (k 5)))\n'
     '      [[Exception] "caught"])))\n'
     '(print "RESULT" r)',
     "RESULT 6")

case("try-outside", "extent",
     "reset inside a try block",
     '(define r None)\n'
     'try:\n'
     '  (set! r (reset (+ 1 (shift k (k 5)))))\n'
     'except [Exception as: e]:\n'
     '  (set! r (repr e))\n'
     '(print "RESULT" r)',
     "RESULT 6")

case("dynwind-around", "extent",
     "dyn-wind around the shift, k invoked once",
     '(define log (#list))\n'
     '(define r\n'
     '  (reset\n'
     '    (dyn-wind\n'
     '      (log.append "in")\n'
     '      (+ 1 (shift k (k 5)))\n'
     '      (log.append "out"))))\n'
     '(print "RESULT" r log)',
     "RESULT 6 ['in', 'out']")

case("dynwind-multishot", "extent",
     "dyn-wind around the shift, k invoked twice",
     '(define log (#list))\n'
     '(define r\n'
     '  (reset\n'
     '    (dyn-wind\n'
     '      (log.append "in")\n'
     '      (+ 1 (shift k (+ (k 10) (k 20))))\n'
     '      (log.append "out"))))\n'
     '(print "RESULT" r (len log))',
     "RESULT 32 4")

case("param-around", "extent",
     "parameterize around the shift",
     '(define-parameter p 1)\n'
     '(define r\n'
     '  (reset\n'
     '    (parameterize [[p 9]]\n'
     '      (+ p (shift k (k 0))))))\n'
     '(print "RESULT" r)',
     "RESULT 9")


def run_case(cid, src, want, keep):
    fd, path = tempfile.mkstemp(suffix=".ki", prefix=f"sw_{cid}_",
                                dir=keep)
    with os.fdopen(fd, "w") as f:
        f.write(src)

    try:
        proc = subprocess.run(
            ["uv", "run", "koskari", path],
            capture_output=True, text=True, timeout=90)
    except subprocess.TimeoutExpired:
        return "TIMEOUT", ""

    out = [ln for ln in proc.stdout.splitlines()
           if ln.startswith("RESULT")]

    if out:
        got = out[0]
        if got == want:
            return "PASS", ""
        return "WRONG", got

    err = proc.stderr.strip().splitlines()
    if err:
        # last line of a traceback is the exception
        return "ERROR", err[-1][:88]

    # No RESULT line. If anything was printed at all, the RESULT prefix
    # itself was destroyed -- that is operand-stack corruption, not
    # silence, and the raw line is the evidence.
    raw = proc.stdout.strip().splitlines()
    if raw:
        return "CLOBBER", f"stdout {raw[0]!r} (RESULT prefix destroyed)"
    return "SILENT", "(no output, no error)"


def main():
    # Generated cases go to a temp dir, not into the tree. The path is
    # printed so a failing case can still be run by hand.
    keep = tempfile.mkdtemp(prefix="delimited_sweep_")
    print(f"cases: {keep}")

    tally = {}
    cat = None

    for (cid, ccat, desc, src, want) in CASES:
        if ccat != cat:
            cat = ccat
            print(f"\n=== {cat} ===")

        status, detail = run_case(cid, src, want, keep)
        tally[status] = tally.get(status, 0) + 1

        mark = "ok  " if status == "PASS" else "FAIL"
        print(f"  [{mark}] {cid:22s} {status:7s} {desc}")
        if status == "WRONG":
            print(f"         got  {detail}")
            print(f"         want {want}")
        elif status not in ("PASS",):
            print(f"         {detail}")

    print("\n--- tally ---")
    for k in sorted(tally):
        print(f"  {k:8s} {tally[k]}")
    print(f"  {'TOTAL':8s} {len(CASES)}")

    return 0


if __name__ == "__main__":
    sys.exit(main())

# The end.
