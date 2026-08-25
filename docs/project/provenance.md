# Development and provenance

Koskari is developed with help from various AI coding agents. The project
treats the practice as a matter of transparency rather than something to
obscure.

---

## The short version

Koskari is designed, reviewed, and maintained by a human author. AI coding
agents -- across more than one tool (Cursor, Claude Code) and several
models -- are used as leverage: to draft changes, generate boilerplate, and
help maintain supporting material. Every contribution is read, judged, and
accepted or rejected by the author. Nothing is merged on faith.

Agent involvement is **not** tracked per file. Earlier revisions carried a
per-module `:ai-assistant:` field, but that field drifted into a dozen stale,
inconsistent values and claimed a precision it never actually had. It has been
removed.

---

## Where the work is human, and where agents help

The **core of the language is human-crafted**: the evaluator and its virtual
machine, delimited continuations, dynamic wind/unwind, macro hygiene, and the
compiler semantics, and native types. These carry invariants that have to be
held in one head to be correct, and they are.

Agents contribute most in the areas around that core:

- Editor support and the LSP. I had no familiarity in this space, and Cursor
  was able to put something together for emacs and textmate very quickly.
- The lexer support for repl_toolkit
- Extras for Pygments and a lot of the setuptools integration.
- Large portions of the unit-test suite (always under review, because tests are
  what assert the core is correct).
- Upkeep of internal design documentation. Many of these are generated entirely
  after changes are made. eg. "ensure design docs reflect the changes"
- User-facing documentation. While I fully intend to give all docs an eventual
  "human touch" I needed an initial set to get me off of the ground. After a
  few revisions I'm happy with the starting set, but a lot of it is generated.
- "Make x copies of this" was used for a number of opcodes. I'd create some
  form of ADD for example, then have an agent create the other ops.
- Boilerplate and mechanical refactors. Move this to there, change this
  signature, identify and update all callers, etc.
- Commit messages as a whole. My commit messages are usually short and
  bordering on useless, eg. "wip," "damnit why won't it work," or "yay it's
  fixed" and not much else. I have gotten into the habit of having agents
  write every commit message, because they actually put in detail as to what
  changed.
- Debugging the dynnode chain. The unwinding and transfer mechanisms are
  really easy to end up in an infinite loop. I still feel it is the right
  architecture, but it's nowhere near the perfect generalization I dreamt
  it would be, and the complexity of tracking down where an exception failed
  to propagate has gotten to the point where I rely on an agent helping me
  hunt through the code a lot.

The dividing line is deliberate. Judgment over what defines correct behavior
stays with the author, as does a finnicky sense of aesthetic.

I've had some really good successes with agents doing targeted refactors.
I've also had some really insane-making experiences that needed a lot of
manual cleanup. Overall it's been a fantastic accelerator though it has
definitely been the cause of unhealthy episodes of rage as well.

---

## Why disclose this

Two reasons. First, honesty: readers deserve an accurate picture of how the
software they are evaluating was built. Second, calibration: Koskari is an early
`0.x` project (see [release-and-versioning](release-and-versioning.md)), and
knowing how it is produced is part of judging what to rely on. The disclosure is
a statement of practice, not a limitation of liability.
