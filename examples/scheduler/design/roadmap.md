# kosrunner roadmap

Working notes for evolving the `kosrunner` scheduler example into something
reliably usable, ahead of forking it into its own project (at which point it
gains a real test suite and standalone docs).

## Why

The concrete goal is a "micro-jenkins": a small, self-hostable job runner that
can replace a UMB + Jenkins setup at work, while introducing Koskari quietly
and at low risk. That framing drives every constraint below.

Operating assumptions:

- **No database server.** State lives on a persistent volume claim (PVC). A
  file-based store is fine; a separate DB pod is not.
- **GitOps for config.** Job definitions are immutable at runtime. Changing a
  definition means commit -> CI builds a new image layer -> pod is replaced.
  There is no runtime config editing.
- **Async-first.** Koskari's evaluator auto-awaits in place, so async code
  reads like straight-line code. We lean into that everywhere rather than
  reaching for threads or sync shims.

## Deployment shape

Two-tier images:

- **Baseline `kosrunner:<ver>`** -- alpine + python + koskari + the `kosrunner`
  package + all deps (aiocron, aiokafka, fastapi, uvicorn, aiosqlite, jinja2).
  Built once, versioned, reused. All framework machinery (run tracking, store,
  admin API, UI) lives here.
- **Per-job layer** -- `FROM kosrunner:<ver>` then `COPY definition.ki
  /job/run.ki`. Three lines. A job definition is pure surface DSL and never
  carries framework code.

`example.ki` is baked into the baseline as the default `run.ki`, so the
baseline runs standalone for a smoke test; a job layer's `COPY` overwrites it.

The pod is immutable and replaced on every config change, so **nothing in the
image survives a deploy** -- all durable state (run history, artifacts, any
persisted runtime overrides) lives on the mounted PVC.

## Settled decisions

### Secrets -- DONE

Config values are ordinary load-time expressions (the `inline-block` layout
splices them flat, uncalled), so a secret is just an env read in the value
position:

```koskari
config kafka:
    sasl_plain_password: (reqenv "SECRET_KAFKA_PASS")
```

`reqenv` resolves an env var and raises when unset, so a misconfigured pod dies
at startup rather than at first connect. It is defined in
[subitems.ki](../kosrunner/subitems.ki) and exported from the baseline, so it
rides in on `import-all-from kosrunner` with no per-file `import os`.

### Storage

- **aiosqlite** single long-lived connection (opened at construction, WAL on),
  as the index. Chosen deliberately to lean into Koskari's async model:
  `(db.execute ...)` / `(db.commit)` auto-await and read synchronously, no
  `defer`, no manual thread marshalling. Serialization onto one thread is the
  library's job, so there is no dedicated-writer to hand-roll. Pure Python, no
  build step, so it costs nothing in the alpine image. Escape hatch if the UI
  ever contends with a write: a second read-only WAL connection.
- The store's async surface stays tiny: `record-start`, `record-finish`,
  `list-runs`, `get-run`, `prune`, plus artifact-path helpers.
- **Bulk stays out of the DB.** Runs are unified under one directory, then
  bucketed by job name, then by run id:

  ```
  <root>/index.db
  <root>/runs/<safe-job>/<runid>/trigger.json   ; the args, so re-trigger can replay
  <root>/runs/<safe-job>/<runid>/output.log     ; captured handler logs
  <root>/runs/<safe-job>/<runid>/result.json    ; return value / exception
  ```

  The DB holds queryable metadata plus the artifact path; the UI streams log
  files straight from disk. Root resolution and the job-name sanitizer are
  detailed in Phase 0.
- **The store hangs off the `JobSet`** (`js.store`), constructed once at the
  configured root. `Job.run` writes via `self.jobset.store`; the admin and UI
  varieties read via the same register-time back-reference they already hold.

### Frontend

One process, no second pod. The web UI is a **sibling job variety to admin**,
not a separate service:

- `admin` stays the **JSON API** (`/admin/jobs`, `/admin/runs`, retrigger
  POSTs) -- independently useful for curl and scripting, and what the UI's
  buttons POST to.
- `ui` is the **human layer** (`/ui`), rendering the same data as Jinja2 HTML.
  Because it is in-process it reads the live `JobSet` and `js.store` directly,
  no HTTP hop.
- Both mount on the shared `HttpServer` (interned by host:port) under their own
  prefix. **nginx fronts the pod and auths by path**: `/ui` and `/admin`
  protected, `/webhook` open.

The rule of thumb: if it's one process, read in-process; if it's ever a
separate pod, make it a real HTTP client of admin. Avoid the muddy middle -- a
separate service talking to itself over localhost.

### Re-trigger

`Job.run` is signature-agnostic, so `trigger.json` stores whatever args the
handler was called with (webhook's payload, kafka's value, cron's nothing).
Re-trigger reads them and replays through `Job.run(cause: 'manual', parent:
<id>)`. It works on a disabled job, which is the point for failure replay. Per
variety: cron = fire now ignoring schedule; webhook = replay stored payload;
kafka = replay the stored value without re-consuming from the broker (real
offset/DLQ semantics are out of scope for a micro-jenkins).

## Plan

Sequenced so each phase lands something runnable; the keystone first.

### Phase 0 -- store foundation (no behavior change yet)

Phase 0 stands up the store and wires it to the `JobSet`, but does **not**
funnel jobs through it yet (that is the Phase 1 keystone). Doing storage first
makes it independently demonstrable by hand before any job depends on it.

**Root resolution -- `config -> env -> cwd`:**

1. `config storage:` `root:` field, if set -- used verbatim.
2. else env `KOSRUNNER_STORAGE_ROOT`, if set -- used verbatim.
3. else `<cwd>/.kosrunner` -- the `.kosrunner` holder is applied **only** in
   this default case, to keep the working dir tidy for a manual run. An
   explicit config or env root is never suffixed.

The env layer is a convenience: an author can already write
`root: (reqenv "KOSRUNNER_STORAGE_ROOT")` in the config, but baking the
precedence in means the common env-driven deployment needs no boilerplate while
an explicit `root:` still wins.

**Job-name -> directory segment (`safe-segment`):**

- Allowlist `[A-Za-z0-9._-]`; replace anything else with `_`; neutralize `..`
  and leading dots; cap length.
- **If the sanitized form equals the original, use it verbatim** (no suffix).
  Only when sanitization actually changed the name (munging or truncation)
  append `-<h>`, where `h` is the first 8 hex of a deterministic
  `hashlib.sha256` of the *original* name -- NOT the builtin `hash`, which is
  per-process randomized; the same job must map to the same dir every run so
  history accumulates in one place.
- Docs note (the `foo-bar` / `foo_bar` / `foo/bar` case): all three coexist in
  distinct dirs -- `foo-bar` and `foo_bar` are clean and used verbatim, while
  `foo/bar` munges to `foo_bar-<hash>` and so cannot collide with the clean
  `foo_bar`. Different originals hash to different suffixes, so munged names do
  not collide with each other either. The only residual is a clean job named
  literally `foo_bar-<that exact hash>` -- self-inflicted and negligible.

**Run id:** sortable `YYYYMMDDThhmmssZ` (UTC) + a 6-hex random suffix for
same-second uniqueness, so `ls runs/<job>/` is chronological and the id is
self-describing. The DB still carries real timestamp columns for querying.

**New module `kosrunner/store.ki`:**

- `StorageConfig` dataclass: `root :: str` (None), `keep_runs :: int` (0 =
  unlimited, per-job cap), `keep_days :: int` (0 = unlimited).
- `config/storage` syntax -- the `()` unnamed case binding `default/storage`
  with the first-wins `&`-guard, same shape as other `config/<type>`. Storage
  is singular per jobset, so only the unnamed case is supported (documented, no
  named variant).
- Exported pure helpers `resolve-storage-root [config]` and
  `safe-segment [name]`, kept in the baseline so their free names never hit the
  use-site resolution trap.
- `RunStore`:
  - `__init__ [self root config: None]` -- records root + config, opens nothing
    (import stays side-effect-free).
  - `open [self]` -- create `<root>` + `<root>/runs`, connect one aiosqlite
    connection, `PRAGMA journal_mode=WAL`, create schema if absent, run
    `reconcile`. Idempotent; must run inside a live loop.
  - `close`, `reconcile` (`running -> interrupted` for orphans),
    `record-start [self job type cause args: None parent: None] -> runid`,
    `record-finish [self runid status result: None error: None]`,
    `list-runs [self job: None status: None limit: 50]`, `get-run [self runid]`,
    `run-log-path [self runid]`, `prune [self]` (retention method defined,
    wiring deferred).
  - Small JSON artifact writes go through `asyncio.to_thread` so the loop never
    stalls; the sqlite calls are async via aiosqlite.

**Schema (`runs` table):** `run_id` PK, `job`, `job_dir`, `type`, `cause`,
`parent`, `status`, `started_at`, `finished_at`, `error`; indexes on
`(job, started_at)` and `(status)`. `job_dir` is stored so artifacts are
locatable without re-sanitizing.

**`JobSet` wiring ([job.ki](../kosrunner/job.ki)):** `__init__` gains
`self.store None`; a new `bind-store [self config]` resolves the root and
constructs the `RunStore` (no open); `__call__` runs `(self.store.open)` before
`(self.start)` and `_teardown` adds `(self.store.close)` -- opening at
`__call__` rather than import because aiosqlite needs a running loop.

**`jobset` macro tail ([subitems.ki](../kosrunner/subitems.ki)):** one
type-agnostic addition after JobSet construction --

```
(define default/storage)
(.bind-store <jsname> &default/storage)
```

Configs are emitted first, so `default/storage` is set (or declared-falsey)
before the JobSet is built; `bind-store` falls through to env/cwd when it is
unset. No per-type knowledge is added to the macro.

**Exports & deps:** export `StorageConfig`, `RunStore`, `config/storage`,
`resolve-storage-root`, `safe-segment` from
[__init__.ki](../kosrunner/__init__.ki); add `aiosqlite` to `pyproject.toml`;
add the CWD store dir (`.kosrunner/`) to the example `.gitignore`.

**Manual demonstration (the payoff of storage-first):** because the store hangs
off `workload` and everything is a real binding, it is validated by hand before
any job writes to it. From a REPL against the example, `(workload.store.open)`
then `(workload.store.record-start "demo" "fake" "manual" args: #{x: 1})`, then
inspect `./.kosrunner/runs/demo/<runid>/trigger.json`. A scratch script using
the `fake` variety with `config storage:` on a temp dir is the pre-fork green
check: record start+finish, assert the row + files exist, reopen to prove
`reconcile` flips a leftover `running` to `interrupted`, and feed `safe-segment`
the nasty cases (`a/b`, `..`, unicode). `assert` is not a Koskari form -- use a
`check` helper; run via `PYTHONPATH=examples/scheduler uv run koskari …`.

### Phase 1 -- the `Job.run` keystone -- DONE (2026-07-09)

- Base `Job.run [self *: args cause: "manual" parent: None]` ([job.ki](../kosrunner/job.ki)):
  record-start via `self.jobset.store`, set the log-capture contextvar,
  `_invoke` the callback with the args, record the outcome (status + result +
  captured log), and return the handler result (webhook needs it back). On a
  callback exception it records `failed` and dispatches to the job's
  `on_error_cb` (the kafka `on-error` hook, now generalized onto the base) or
  logs. `_job-kind` derives the `type` column from the class name
  (`CronJob` -> `cron`); degrades to a plain invoke when there is no store.
  `JobSet.register` now stamps `job.name`.
- Every variety funnels through it: cron's `fire` -> `(self.run cause: "schedule")`,
  kafka's `_consume` -> `(self.run value cause: "kafka")`, the webhook route
  mounts `(lambda [payload] (self.run payload cause: "webhook"))`.
- **Log capture** ([store.ki](../kosrunner/store.ki)): a `_RunLogHandler`
  (subclass of `logging.Handler`, `emit` **@demand**-decorated because Python
  calls it synchronously) is attached to the `kosrunner` logger by
  `install-run-logging` (run once from `RunStore.open`). `Job.run` sets the
  `_current-run` contextvar to a fresh buffer around the invoke -- **in
  `Job.run`'s own context**, not a separate awaited store method, or the buffer
  isn't visible in `emit`. `record-finish` flushes the buffer to `output.log`.
  Caveat: capture only sees records on a `kosrunner.*` logger at/above the
  configured level (a jobset sets `config logging:`); handlers that `print`
  are NOT captured -- stdout capture is a later item, and nudging the example
  handlers onto a `kosrunner.*` logger is an easy follow-up to make capture
  visible in the demo.

Verified: an 11-assert direct-run check (success + result + captured log +
failure + on-error), and a live `example.ki` run recording `heartbeat` (cron),
`ping`/`echo` (webhook) end to end with correct type/cause/status. This is the
load-bearing change; everything downstream reads what it writes.

### Phase 2 -- admin JSON API growth -- DONE (2026-07-09)

[admin.ki](../kosrunner/admin.ki) mounts three run routes: `GET {prefix}/runs`
(recent runs via `store.list-runs`), `GET {prefix}/runs/{id}` (a new
`server.add-detail` GET-with-path-param helper -> `store.run-detail`, the row
plus its `output`/`result`/`trigger` artifacts read from disk), and `POST
{prefix}/retrigger/{id}` (`add-action` -> `js.retrigger`).

`JobSet.retrigger [self runid]` ([job.ki](../kosrunner/job.ki)) reads the run's
`store.get-trigger` (its `trigger.json`), finds the job by name, and replays via
`(job.run *: args cause: "manual" parent: runid)` -- a new run linked to the
original, working even on a disabled job. Runs inline for now (the request
awaits the replay); non-blocking scheduling is a possible refinement.

Deviations from the original sketch: the log is returned inline in the detail
JSON rather than a separate `/runs/{id}/log` stream, and retrigger is
`/retrigger/{id}` (the `add-action` shape) rather than `/runs/{id}/retrigger`;
query-param filtering on `/runs` is not wired (the method takes filters, the
route passes none yet).

Verified: a 14-assert direct check (get-trigger / run-detail / retrigger /
parent linkage / unknown-run error) and a live e2e -- seed a webhook run, then
`GET /admin/runs`, `GET /admin/runs/{id}` (detail carried `trigger.args` +
result), `POST /admin/retrigger/{id}` produced a new `manual` run linked to the
parent.

### Phase 3 -- `ui` sibling variety

- New `kosrunner/ui.ki`: `UiConfig` (a `ServerConfig` subclass, prefix `/ui`) +
  `UiJob` mounting Jinja2 HTML routes on the shared `HttpServer`, reading `js`
  and `js.store` in-process. `config/ui` + `job/ui` syntax, exported from
  [__init__.ki](../kosrunner/__init__.ki).
- Pages: job list with enable/disable buttons (POST to admin), run-history
  table, run detail with log view, retrigger button.
- Deps/packaging: add `jinja2` to `pyproject.toml`, ship `templates/` as
  package-data.

### Phase 4 -- deployment shape

- Split the [Containerfile](../Containerfile) into the baseline (build wheel,
  install, bake `example.ki` as default `run.ki`) and a documented ~3-line job
  layer.
- Document the nginx pattern (auth on `/ui` + `/admin`, `/webhook` open) and
  the storage PVC mount + `STORAGE_ROOT` env.
- Parked for later: per-webhook trigger UUID as a bearer-ish token in the job
  description, as minimal auth for open webhooks.

## Open decisions

### Enable/disable persistence

Does a runtime enable/disable survive a redeploy?

- **git-owned + ephemeral (recommended for v1):** desired-state is the
  `disabled` keyword in the definition; a redeploy resets to declared state;
  runtime toggles are transient. Pure GitOps, and it dodges the reconciliation
  subtlety of what wins when git's declared state and a persisted override
  disagree after a redeploy.
- **persisted override:** an operator toggle is written to the PVC store and
  replayed on startup. The one scenario that argues for this is an incident --
  an API click to disable a broken job beats commit-CI-redeploy, and you don't
  want a routine redeploy re-enabling it.

Since the store exists as of Phase 0, adding a persisted-override table later is
cheap. Leaning ephemeral-first, add persistence only if the ephemerality bites.

## Verifying as we go

The `fake` variety ([fake.ki](../kosrunner/fake.ki)) is the local exercise
loop: run a jobset of fake jobs, watch runs land in the store and render in the
UI, well before the formal test suite arrives at fork time. Keep it as the
injectable seam -- the `RunStore` should be constructible against a throwaway
root so a future in-memory / temp-dir test store drops in.
