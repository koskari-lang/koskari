# Expressive facets Koskari could claim

## Status

Exploratory. Nothing here is a committed plan. Each section is a sketch of a
language facility that (a) Koskari does not have, (b) has no existing entry in
[TODO.md](TODO.md) or [ideas.md](ideas.md), and (c) is unusually cheap for
Koskari specifically, because the VM already carries the hard machinery.

Deliberately **out of scope** here, because they already have owners:

| Facility | Where it lives |
| -------- | -------------- |
| Runtime `match` / destructuring binding | [archive/match-case-form.md](archive/match-case-form.md), [language/seq-values-kwargs-formals-apply.md](language/seq-values-kwargs-formals-apply.md) |
| Pure parser, lint, formatter | [koskari-lint.md](koskari-lint.md) |
| Test runner (`koskari.unittest`, pytest collection) | not yet written up |
| Bundles and namespace facets | [bundles-and-namespace-facets.md](bundles-and-namespace-facets.md) |
| Concurrent `async` fan-out form | [api/async-form.md](api/async-form.md) |

The `async` form appears in both lists: § Structured concurrency below argues
the planned design should change shape, so it is treated as an open question
rather than settled work.

---

## The organizing idea

Almost everything below is the same primitive wearing a different hat.

Koskari already has **delimited continuations** (`compile/reset` /
`compile/shift`), **undelimited continuations** (`compile/call/cc`), and an
**event-driven dynamic extent chain** (`dynchain.c`, `compile/dyn-wind`,
`compile/parameterize`). Those three together are the substrate that the
following are all built from:

| Facility | What it does with the substrate |
| -------- | ------------------------------- |
| Generators | capture at yield, resume once, in order (**have**) |
| Ambient async | capture at await, resume once, when the loop says so (**have**) |
| Effect handlers | capture at the operation, let an outer handler decide whether to resume |
| Conditions / restarts | same, with a fixed protocol and named recovery points |
| `amb` / search | capture at the choice point, resume **more than once** |
| Transactional retry | capture at block entry, resume from the top on conflict |

Filinski's result (*Representing Monads*, POPL 1994) is the formal version of
this: `shift`/`reset` is enough to express any monadic effect. Koskari paid for
that machinery to get coroutines and continuations. Right now it is spending it
on two things when it could be spending it on eight.

This also answers a reasonable first reaction -- "effect handlers sound like
transactions." They are cousins in *machinery* (both capture a computation and
decide whether to resume it) and unrelated in *intent*. A transaction resumes a
block from the top to retry it. An effect handler resumes a block from the
middle to answer a question it asked.

---

## 1. Effect handlers

### What it is

Take `raise`, and make it resumable.

When Python raises, the stack between the raise site and the `except` is
destroyed before the handler runs. The handler can only decide *how to fail*.
An effect handler runs while the stack is still intact and holds the
continuation of the computation that asked, so it can decide *how to answer*.

The vocabulary:

- A computation **performs** an operation: `(perform config-lookup "kafka.servers")`.
- The nearest enclosing **handler** for that operation runs. It receives the
  operation's arguments and the delimited continuation from the perform point
  up to the handler.
- The handler either **resumes** with a value (the computation continues from
  where it asked, as if `perform` had returned that value), **does not resume**
  (behaves like an exception), or **resumes more than once** (backtracking,
  generators, probabilistic search).

A *deep* handler reinstalls itself around the resumed computation; a *shallow*
one does not. Deep is the usual default.

### Why it matters for Koskari specifically

The pitch for the language is that you model a system as a DSL. The recurring
problem with that pitch is that DSL bodies acquire dependencies -- the clock,
the config store, the network, the metrics sink -- and then the DSL is no
longer declarative, it is Python with better indentation.

Effect handlers are the clean answer. The DSL body performs; the *host* decides
what performing means. Same source, different handler:

```koskari
;; A job body. It has no idea where config comes from, what time it is,
;; or where metrics go. It never imports any of them.

define drain-orders []:
  let [[servers (perform config-lookup "kafka.servers")]
       [started (perform now)]]:
    for-each [msg (consume servers "topic.orders")]:
      (handle-order msg)
    (perform emit-metric "orders.drain.seconds" (- (perform now) started))
```

```koskari
;; Production host.
with-handler:
  handle config-lookup [key]:
    (resume (live-config.get key))
  handle now []:
    (resume (time.monotonic))
  handle emit-metric [name value]:
    (statsd.timing name value)
    (resume None)
  (drain-orders)
```

```koskari
;; Test host. No mocks, no patching, no injection plumbing.
with-handler:
  handle config-lookup [key]:
    (resume "localhost:9092")
  handle now []:
    (resume 1000.0)          ;; frozen clock
  handle emit-metric [name value]:
    (recorded.append #(name value))
    (resume None)
  (drain-orders)
```

That is dependency injection with no injection: nothing is threaded through
signatures, nothing is patched, and the set of effects a body performs is a
readable description of what it touches.

The second payoff is that a handler that declines to resume is an early exit
through arbitrarily deep DSL code, and a handler that resumes twice is a
retry-with-different-answer. Both fall out for free.

### Sketch of the Koskari shape

```koskari
;; Declaration exists to give the operation an identity and a docstring;
;; it is a value, not a type.
define-effect config-lookup [key]:
  "Ask the host for a configuration value."

;; `with-handler` is a layout block. Each `handle` clause binds the
;; operation's formals; `resume` is bound in the clause body.
with-handler:
  handle config-lookup [key]:
    (resume (defaults.get key))
  BODY ...
```

Open questions worth settling before any implementation:

- **Is `resume` a bound name or a captured continuation object?** A first-class
  continuation is more honest and composes better (you can stash it and resume
  later, which is how you would build async on top). A bound `resume` reads
  better in the common case. Probably: `resume` is sugar over the
  continuation value, which is also available.
- **Deep or shallow by default.** Deep, almost certainly.
- **One-shot enforcement.** Multi-shot resumption is where the power is and also
  where the surprises are (side effects between the two resumptions happen
  twice). A per-handler declaration -- `handle ... once:` -- would let the
  common case be checked.
- **Unhandled operations.** Should raise a specific, greppable error naming the
  operation, not a `NameError`.
- **Interaction with `dyn-wind`.** An operation performed inside a wind, handled
  outside it, and resumed, must not run the after-thunk on the way out and the
  before-thunk on the way back in -- the extent is not being left. This is the
  main VM-side question and the existing transfer-plan machinery in
  [archive/interrupted-transfer-plans.md](archive/interrupted-transfer-plans.md)
  is where the answer lives.

### What it needs

Mostly a library and a pair of macros over `compile/shift` / `compile/reset`.
`with-handler` installs a prompt tagged with the handled operation set;
`perform` is a `shift` to the nearest prompt carrying that tag. The genuinely
new VM surface is **tagged prompts** -- today the prompt stack is untagged, so
`shift` finds the nearest prompt rather than the nearest prompt *for this
operation*. That is the one piece of real work.

### Reading

- Andrej Bauer and Matija Pretnar, *Programming with Algebraic Effects and
  Handlers* (2012) -- the readable introduction; the Eff language.
- Ohad Kammar, Sam Lindley, Nicolas Oury, *Handlers in Action* (ICFP 2013) --
  how handlers are actually implemented.
- The **OCaml 5** effects manual. Important precedent: OCaml shipped effects
  that are **not** tracked in the type system. A dynamically-checked effect
  system is a legitimate design point, not a compromise, and it is exactly
  Koskari's situation.
- **Koka** (`koka-lang.github.io`) and Daan Leijen's papers, for the typed
  version and for the compilation strategy.
- **Unison**'s "abilities" -- the most approachable docs of any effect system.
- Andrzej Filinski, *Representing Monads* (POPL 1994) -- why `shift`/`reset` is
  already enough.

---

## 2. Conditions and restarts

### What it is

Common Lisp got most of the way to effect handlers in 1984, with a fixed
protocol instead of a general mechanism, and the ergonomics are arguably better
for the error-handling case specifically.

Three pieces:

- **`signal`** raises a condition *without unwinding*. Handlers run on the
  signaler's stack.
- **`handler-bind`** establishes handlers that run in that position. A handler
  that returns normally **declines**, and the next handler out gets a look. A
  handler that wants to act performs a non-local transfer.
- **`restart-case`** establishes *named recovery strategies* at the point that
  knows how to recover. A handler far up the stack calls
  **`invoke-restart`**, and control transfers back down to that restart,
  unwinding only as far as the `restart-case`.

The separation that makes it valuable: the code that knows **how** to recover is
deep (it has the context -- the half-parsed record, the open socket), and the
code that knows **whether** to recover is shallow (it has the policy). Python
fuses them, and by the time `except` runs, everything the deep code knew is
gone.

### Why it matters for Koskari specifically

Every long-running job scheduler eventually grows this by hand, badly, as a
tangle of retry flags and sentinel return values. The scheduler in
[examples/scheduler](../examples/scheduler) is exactly the shape that wants it:

```koskari
;; Deep: the handler for one message knows what recovery options exist.
define handle-order [msg]:
  restart-case:
    (process (parse-order msg))
    restart skip []:
      "Discard this message and continue the drain."
      None
    restart use-value [order]:
      "Process a substitute order supplied by the handler."
      (process order)
    restart requeue []:
      "Put the message back on the topic."
      (republish msg)
```

```koskari
;; Shallow: the jobset owner sets policy, and never sees a message.
job kafka orders "topic.orders":
    on-condition MalformedOrder [c]:
        if (< poison-count 100):
            (invoke-restart 'skip)
        ;; else: decline by falling off the end, and let it propagate
    on-condition BrokerTimeout [c]:
        (invoke-restart 'requeue)
    handler [msg]:
        (handle-order msg)
```

Note what did not happen: `handle-order` has no policy in it, no retry counter,
and no flags in its return type. And the policy block has no parsing knowledge.
Neither had to be written with the other in mind, which is precisely what makes
a DSL survive contact with production.

The other thing conditions buy is the debugger story. In Common Lisp, an
unhandled condition drops you into a REPL *at the signal point, before
unwinding*, and offers the restarts as menu items. Combined with a live REPL
attached to a running jobset (§ 8), that is a genuinely striking demo: a job
fails at 3am, you attach, and you are standing at the failure with `skip`,
`use-value`, and `requeue` on offer.

### Relationship to effects

Restarts are effect handlers with a fixed protocol: `signal` is `perform`,
declining is "resume with no answer, ask the next handler", and `invoke-restart`
is a non-local transfer to a marked point. If § 1 gets built, this is a library
on top of it rather than a separate mechanism. If only one gets built, build
effects -- conditions fall out; the reverse does not.

Worth keeping: Python exceptions stay exactly as they are. Conditions are an
additional, opt-in channel. A condition that reaches the top unhandled degrades
into an ordinary raise.

### Reading

- Peter Seibel, *Practical Common Lisp*, ch. 19, "Beyond Exception Handling:
  Conditions and Restarts" -- the best explanation in existence, and the
  log-parsing example maps directly onto job processing.
- Kent Pitman, *Condition Handling in the Lisp Language Family* -- the design
  rationale, including why signalling and unwinding were separated.
- The Common Lisp HyperSpec, section 9.
- Dylan's condition system, for a version with the CL warts filed off.

---

## 3. Structured concurrency (a revision to the planned `async` form)

### What it is

One rule: **a block may not exit while it still has running children.**

The consequence is that concurrency becomes a *block*, with a lexical extent,
instead of a *statement* that scatters lifetimes into the void. Children are
cancelled if a sibling fails; errors propagate out of the block instead of
vanishing into a task nobody awaited; and reading the code tells you where a
task can still be running.

Cancellation gets the same treatment: a **cancel scope** is a region with an
optional deadline, and cancelling it cancels everything dynamically inside it.

### Why the planned form should change shape

[api/async-form.md](api/async-form.md) currently specifies `gather` over
`(create-task (defer …))`. That is the asyncio-shaped answer, and it inherits
the asyncio problems that ambient async does not solve:

- a task created and not awaited outlives its creator and dies silently
- an exception in an un-awaited task surfaces as a garbage-collection warning
- cancellation does not compose; each layer re-implements timeout handling

The claim the language is making is that ambient async removes the pain of
Python async. Removing `await` removes the *syntactic* pain. What is left is the
*lifetime* pain, and that is the larger half. Shipping `gather` leaves the
larger half on the table, in the one language that has the dynamic-extent
machinery to fix it properly.

Koskari has a real advantage here: a cancel scope is a `KosDynNode`. Trio has to
implement cancel scopes on top of a runtime that does not want them; Koskari
already tracks dynamic extent as a first-class chain, with wind/unwind events
and transfer goals. The natural implementation is a new extent node kind, not a
new subsystem.

### Sketch

```koskari
;; The block does not complete until every child has settled.
;; If any child raises, the others are cancelled and the error
;; propagates out of the `nursery` block.
nursery [n]:
  for-each [job (jobset.enabled)]:
    (n.start (run-job job))
  (print "all jobs started")
;; unreachable until every job has finished
```

```koskari
;; Cancel scopes as extents, with deadlines.
timeout 30:
  (drain-topic "orders")

;; Non-fatal variant: expire quietly and carry on.
move-on-after 5:
  (poll-once)
```

```koskari
;; Nesting is the whole point: the outer scope's cancellation
;; reaches every task the inner block started.
timeout 300:
  nursery [n]:
    (n.start (consume "topic.orders"))
    (n.start (consume "topic.audit"))
```

Points to settle:

- **Cancellation delivery.** Trio delivers a `Cancelled` exception at the next
  checkpoint and requires it to propagate. Koskari could deliver it as a
  condition (§ 2), which would let a job offer a `finish-batch` restart before
  agreeing to die. That is a nicer story than anything in the Python ecosystem
  and worth prototyping.
- **Interaction with `defer`.** A `defer` handle inside a nursery is a task that
  has not been given a parent yet; the nursery is the obvious parent.
- **Whether `create-task` stays public.** Trio's discipline is that unparented
  spawning simply is not available. That is a strong position and probably the
  right one for a new language.

### Reading

- Nathaniel J. Smith, *Notes on structured concurrency, or: Go statement
  considered harmful* (2018) -- the essay that started the modern conversation.
  Read this one first.
- Nathaniel J. Smith, *Timeouts and cancellation for humans* -- the cancel-scope
  design, and why deadlines beat timeout arguments.
- **Trio**'s documentation, particularly the nursery reference.
- Kotlin's `coroutineScope` / `supervisorScope`, and Swift's structured
  concurrency proposals, for two more takes on the same rule.
- Martin Sústrik's original *Structured Concurrency* post, for the C-level view.

---

## 4. Supervision trees

A short section, because it is a composition of § 2 and § 3 rather than a new
mechanism -- but it is the obvious next block type for the scheduler, and it is
what makes the other two concrete.

Erlang/OTP's insight is that error *recovery* is a static, declarative property
of the process tree, not something written inline at each failure site. A
supervisor declares a restart strategy and children; when a child dies, the
strategy decides what restarts.

```koskari
jobset workload:

    supervisor ingest one-for-one max-restarts: 5 within: 60:
        job kafka orders "topic.orders":
            handler [msg]:
                (process msg)
        job kafka audit "topic.audit":
            handler [msg]:
                (audit msg)

    ;; rest-for-one: if the parser dies, the writer downstream
    ;; restarts too, because its input was interrupted
    supervisor pipeline rest-for-one:
        job stream parser ...
        job stream writer ...
```

`one-for-one`, `one-for-all`, `rest-for-one`, plus restart intensity limits, is
the whole vocabulary and it has been stable for thirty years. A supervisor is a
nursery with a restart policy, and the "let it crash" discipline only works
because the tree above is trusted to bring things back.

**Reading:** the Erlang/OTP supervisor documentation and design principles;
Joe Armstrong's thesis, *Making Reliable Distributed Systems in the Presence of
Software Errors* (2003), chapters 4 and 6.

---

## 5. Contracts with blame

### What it is

Not a type checker. A **contract** is a runtime check attached to a value at a
module boundary, which -- crucially -- knows **who to blame** when it fails.

For first-order values that is just an assertion. The interesting case is
higher-order values: you cannot check "is this function `int -> int`" at the
boundary, so you wrap it and check at each call. The wrapper remembers the two
parties, and reports the right one:

- caller passes a string where the contract said `int?` -- **the caller** is at
  fault, and is named, with a source location
- the function returns a string where the contract said `int?` -- **the
  function** is at fault

That blame assignment is the entire value proposition. A stack trace tells you
where things blew up; a blame message tells you which side broke the agreement,
which is nearly always the question you actually have.

### Why it fits Koskari better than types

[docs/guide/types-or-contracts.md](../docs/guide/types-or-contracts.md) already
takes the right position: values are Python objects at runtime and annotations
are metadata. Chasing a static type system from there means either fighting
Python's dynamism or building a gradual system, and gradual typing's hard part
*is* blame -- so the contract work is on the critical path either way. Racket
went contracts-first and got the most practically useful part of gradual typing
without the type system.

The proposal costs no new syntax: **reuse `::`, and let a pragma decide what it
means.**

```koskari
(pragma [compiler/contracts check])

define fetch-order :: order? [order-id :: (and? str? non-empty?)
                              retries  :: (integer-in 0 10)]:
  ...
```

With `compiler/contracts` at the baseline `ignore`, that is exactly today's
behaviour: annotations are metadata, zero cost. At `check`, the compiler emits
guards with blame parties recorded from the module boundary. At `assert`,
checks compile out in the same way `_assert.ki` gates assertions. Library
authors ship with contracts on, applications turn them off in production, and
nobody rewrites a signature.

The DSL angle is the more interesting one. A block contract validates the
*shape* of a config block and blames the block author, at the block's source
position:

```koskari
;; in the jobset variety definition
define-block-contract config/kafka:
  required servers   :: str?
  required group_id  :: str?
  optional disabled  :: boolean?  default: False
```

An unrecognised key in `config kafka:` becomes an error at the config line,
naming the key and listing what is accepted -- rather than an `AttributeError`
somewhere in the consumer six seconds after startup. For a DSL that agents
write, that difference is most of the usability.

### Reading

- Robert Bruce Findler and Matthias Felleisen, *Contracts for Higher-Order
  Functions* (ICFP 2002) -- the blame paper. Short and very readable.
- The **Racket Guide**, "Contracts" chapter -- the practical surface, and the
  best-developed contract library anywhere.
- Philip Wadler and Robert Bruce Findler, *Well-typed Programs Can't Be Blamed*
  (ESOP 2009) -- the connection to gradual typing.
- Clojure **spec**, and Rich Hickey's *Maybe Not* talk -- a different take:
  specs as data, usable for validation, destructuring, documentation, and test
  generation from one declaration. The generative-testing payoff is worth
  noting; a spec that can validate can usually also *generate*, which would give
  Koskari property-based testing nearly for free once the test runner exists.

---

## 6. Transducers

### What it is

Rich Hickey's decomposition: separate **what you do to each element** from
**what you do with the results** and from **where the elements come from**.

A reducing function is `(acc, item) -> acc`. A **transducer** is a function from
one reducing function to another. `(mapping f)` does not know about lists; it
takes a reducing function and returns one that applies `f` before reducing.
Compose transducers and you get a single fused reducing function with no
intermediate collections, which you can then run over a list, a file, an async
stream, or a Kafka topic.

```koskari
;; The transform, defined once. It mentions no collection at all.
define parse-and-filter
  (compose (mapping parse-record)
           (filtering valid?)
           (taking 1000))

;; Run it into a list.
(transduce parse-and-filter conj #[] lines)

;; Run the same transform over an async stream, accumulating through a ref.
(transduce parse-and-filter (into-ref &counts) (stream-topic "orders"))
```

Note that `compose` reads left-to-right in application order, which is the
opposite of function composition and the one thing everyone trips on.

### Why it fits Koskari specifically

You raised the pass-an-accumulator-along model as something the ref system
should enable. Transducers are the mature, composable version of exactly that
pattern, and the pieces are already in place:

- **Refs** are the accumulator, and `&acc` by-ref formals mean a reducing step
  can write through to the caller's binding without a return-value dance. The
  ref model currently reads as "output parameters, which Python people were
  taught to distrust." Transducers give it a flagship use where the indirection
  is obviously the right call.
- The **reducing operators already exist in C** ([FEATURES.md](FEATURES.md)
  § Operators), so the fused inner loop can be native.
- **Ambient async** is where this genuinely beats Clojure's version. In Clojure,
  applying a transducer to a channel is a special case. In Koskari, a reducing
  function that awaits is just a reducing function, so the same `parse-and-filter`
  runs over a list and over a Kafka topic with no variant API. That is a
  demonstrable "we do this better" that costs a library rather than VM work.

Also worth stealing from the same lineage: **reducers and early termination**
(`reduced` as a sentinel that stops the fold, which is how `taking` above
works), and **eduction** (a lazy, re-runnable application of a transducer to a
source).

### Reading

- Rich Hickey's *Transducers* talk (Strange Loop 2014) and the transducers
  reference on `clojure.org`.
- The `clojure.core.reducers` design notes, for the early-termination protocol.
- Note the same idea shows up as `Iterator` adaptors in Rust and as Java
  Streams; Clojure's framing is the one that generalises to async sources.

---

## 7. Nondeterministic choice (`amb`)

### What it is

McCarthy's `amb` operator, and the reason SICP spends a chapter on
continuations. `(amb 1 2 3)` evaluates to `1`. If the computation later
**fails**, control backtracks into the `amb` and it evaluates to `2` instead.

```koskari
solve:
  let* [[a (amb (range 1 20))]
        [b (amb (range a 20))]
        [c (amb (range b 20))]]:
    (require (== (+ (* a a) (* b b)) (* c c)))
    #(a b c)
;; => (3 4 5)
```

The implementation is the canonical multi-shot continuation exercise: `amb`
captures the continuation at the choice point and stashes it on a failure
stack; `require` invokes the most recent one when its test fails.

### Why bother

Two reasons, one honest and one strategic.

The strategic one: this is the five-line demo that makes people who have only
written Python stop and look. Continuations are the single hardest Koskari
feature to motivate in a README -- "resumable, multi-shot control flow" is
abstract, and `amb` is what it is *for*. Right now the continuation surface is
the most impressive part of the implementation and the least legible part of the
pitch.

The honest one: constraint search is a real thing schedulers do. Assigning jobs
to time windows without collisions, or fitting workers to shards under a
capacity bound, is `amb` plus `require`. A `solve:` block in a jobset DSL is not
a toy.

If it lands, the natural follow-on is a relational sublanguage -- a miniKanren
or Datalog `fence:` block -- since fences already exist as the mechanism for
embedding a foreign notation. That would give the DSL story a second flagship
next to the scheduler, in a completely different domain.

### Reading

- SICP §4.3, "Variations on a Scheme -- Nondeterministic Computing". The
  reference treatment, including the `amb` evaluator.
- Dorai Sitaram, *Teach Yourself Scheme in Fixnum Days*, ch. 14 -- the compact
  `call/cc` implementation, maybe 30 lines.
- *The Reasoned Schemer* and the miniKanren papers, for where it goes next.
- Note that probabilistic programming languages (Church, Anglican, Gen) are this
  same mechanism with weights attached -- worth knowing, probably not worth
  chasing.

---

## 8. Re-entrant debugging and live redefinition

Two related items, grouped because they share the same demo and the same
caveats.

### Re-entrant ("time-travel") debugging

Multi-shot continuations mean a debugger can capture a point in a computation
and **re-enter it later, more than once, with a changed value**. Step forward,
decide the answer at line 12 was wrong, jump back to line 12, supply a different
value, and run forward again -- without restarting the process or losing the
surrounding state.

Be honest about the limitation, because it is fundamental: **this is re-running,
not rewinding.** Side effects performed between the capture point and now are
not undone. If the code wrote to Kafka, it will write again. `rr` achieves true
reversal by recording and replaying every syscall; Elm's debugger achieves it by
being pure. Koskari can offer neither.

What it *can* offer is re-running with the dynamic extent correctly restored,
which is more than any Python debugger does, and `dyn-wind` is precisely the
hook for making a region re-entry-safe -- a before-thunk that resets the
effects the body is about to redo. Combined with restarts (§ 2), the pitch is
concrete: an unhandled condition drops you at the signal point, you inspect,
you pick a restart, or you edit the value and re-run the block.

### Live redefinition

There is a REPL (`koskari/cli/repl.ki`) and no story around it. Lisps
historically won on this axis, and Clojure's adoption was substantially nREPL
plus editor-integrated evaluation rather than any language feature.

The demo that sells Koskari to a working engineer: *attach to the running
jobset, redefine a handler, and watch the next message use the new code, without
dropping the Kafka consumer or losing the run history.* That is Erlang's hot
code loading and Smalltalk's image, in a language that also talks to Python
libraries.

The infrastructure is mostly there and mostly unexercised. Namespaces are
first-class and mutable, so rebinding a name is straightforward. The real
questions are the awkward ones: code is sealed and arena-packed at module seal
(see [PRAGMA.md](PRAGMA.md)), so redefinition needs an explicitly unsealed
development mode; and a running coroutine holds a reference to the old `Code`,
so redefinition applies to the next invocation rather than the current one --
which is Erlang's answer too, and is fine as long as it is documented rather
than discovered.

**Reading:** Erlang's code-loading documentation (the two-version rule);
Smalltalk image-based development, for the maximal position; `rr` (Mozilla) for
what real reversible debugging requires; Bil Lewis's *Omniscient Debugging*.

---

## 9. Capability-scoped evaluation

### What it is

Namespaces are first-class and the compiler resolves through them. That means a
module can be evaluated in a namespace that simply **does not contain**
`import`, `open`, `run-process`, or anything else you did not choose to put
there. Not a permission check at call time; the name is not reachable.

This is the natural terminus of the `#lang` / bundles direction in
[bundles-and-namespace-facets.md](bundles-and-namespace-facets.md), and it is
the part that is genuinely novel rather than a Racket port. A jobset written by
an agent, or submitted by a user of your product, evaluates against a namespace
containing the jobset vocabulary and nothing else:

```koskari
let [[ns (facet-namespace jobset-lang)]]:
  (load-source-in ns submitted-source)
```

The immediate wins are real and cheap: unknown names become **errors at load
time with a list of what is available**, instead of runtime `NameError`s; the
vocabulary is enumerable, so it can be documented, completed in an editor, and
handed to a model as a closed list; and the blast radius of DSL code is
inspectable.

### The caveat, stated plainly

**This is intent-scoping, not a security boundary.** Python object graphs leak:
give code any object and `.__class__.__base__.__subclasses__()` is a path back
to the whole runtime. Every attempt to build a Python sandbox in-process has
eventually fallen, and Koskari inherits that completely. A restricted namespace
stops accidents and honest mistakes; it does not stop an adversary.

If a real boundary is ever wanted, it is a process boundary or a WASM boundary,
and the namespace facet becomes the *ergonomic* layer on top of it rather than
the mechanism. Worth writing that down early so nobody builds on the wrong
assumption.

### Reading

- Mark S. Miller, *Robust Composition* (2006) -- the object-capability model,
  and why designating and authorising with the same act is the right idea.
- Racket's `racket/sandbox`, for the closest working analogue and its documented
  limits.
- Joe-E (capability-safe Java subset) and Deno's permission model, for two
  different enforcement strategies.

---

## Suggested order

Ranked by value divided by cost, given what the VM already has.

1. **Effect handlers** (§ 1). The highest-leverage item in this document. Mostly
   a library over `shift`/`reset`; the one real VM addition is tagged prompts.
   Unlocks § 2 and § 7 as libraries, and reframes the DSL story from "macros
   that build objects" to "programs whose meaning the host supplies."
2. **Conditions and restarts** (§ 2). Falls out of § 1. Directly serves the
   scheduler, and produces the best single demo the language could have.
3. **Structured concurrency** (§ 3). Needs a decision before
   [api/async-form.md](api/async-form.md) is implemented, because the two
   designs are not compatible and the wrong one is hard to walk back. Cancel
   scopes want a new `KosDynNode` kind, which is modest work in a subsystem
   built for exactly this.
4. **Transducers** (§ 6). Pure library. Gives the ref model its flagship, and
   the async-source generalisation is a concrete advantage over Clojure's
   version.
5. **`amb`** (§ 7). A weekend. Buy it for the demo and the README; keep it for
   the scheduling constraint case.
6. **Contracts** (§ 5). Compiler and pragma work, so more expensive, but the
   block-contract case is what makes an agent-authored DSL usable, and the
   `::`-plus-pragma framing means no new syntax.
7. **Supervision trees** (§ 4). Composition of 2 and 3; do it after both.
8. **Capability-scoped evaluation** (§ 9). Follows bundles; write the caveat
   down now regardless.
9. **Re-entrant debugging and live redefinition** (§ 8). Highest risk, hardest
   to scope, best story. Worth a prototype once § 2 exists, since restarts are
   half the interface.

Two notes on approach. Every item here is an opt-in addition; none of them
change the meaning of existing code, and Python interop is untouched throughout.
And each is worth a prototype in Koskari itself before any C is written -- the
macro layer is strong enough to build a slow version of all nine, which is the
cheapest way to find out which ones actually feel good.

# The end.
