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
  package + all deps (aiocron, fastapi, uvicorn, aiosqlite, jinja2) and every
  extra (aiokafka, stompman), installed as `koskari-kosrunner[all]`. The broker
  client libraries are packaging extras so a *library* consumer can skip them,
  but the baseline image is one artifact serving every job layer, so it carries
  the lot. Built once, versioned, reused. All framework machinery (run tracking,
  store, admin API, UI) lives here.
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
  prefix. **The front-end fronts the pod and auths by path**: `/ui` and `/admin`
  protected, `/webhook` open. (Originally nginx basic-auth; now oauth2-proxy
  OIDC -- see the Phase 4 "Superseded (auth)" note.)

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

### Phase 3 -- `ui` sibling variety -- DONE (2026-07-10)

- New [ui.ki](../kosrunner/ui.ki): `UiConfig` (`ServerConfig` subclass, prefix
  `/ui`) + `UiJob` mounting Jinja2 HTML on the shared `HttpServer`, reading the
  live JobSet and `js.store` **in-process** (no HTTP hop to admin). `config/ui`
  + `job/ui` exported from [__init__.ki](../kosrunner/__init__.ki).
- Routes: `GET /ui` (dashboard: jobs + recent runs), `GET /ui/runs/{id}` (detail
  + captured output + result), and form actions `POST /ui/{enable,disable}/{name}`
  / `POST /ui/retrigger/{id}` that act and `303`-redirect back to `/ui`. The UI
  handles its own actions (self-contained) rather than pointing forms at the
  admin JSON API. Two new server helpers: `add-page` (GET -> HTML) and
  `add-page-item` (GET path-param -> HTML).
- **Templates** ship as package data (`templates/*.html`, a `base.html` the two
  pages extend). Loaded via `jinja2.FileSystemLoader` over the package dir,
  which is located with `find_spec("kosrunner").submodule_search_locations`
  because koskari modules have no `__file__`. `autoescape` on. Env is built in
  `UiJob.start` (runtime, when a loop and the package are fully present).
- Deps: `jinja2` in `pyproject.toml`; `package-data` extended to
  `templates/*.html`. `example.ki` gains `config ui:` + `job ui web` on `:8080`
  (shares the webhook/admin server).

Verified: `example.ki` builds 8 jobs incl. the UI; a live e2e drove `GET /ui`
(dashboard lists jobs + runs), `GET /ui/runs/{id}` (detail), `POST /ui/retrigger`
and `POST /ui/disable` (both `303` -> `/ui`, retrigger produced a linked `manual`
run). Templates also rendered to static previews with sample data.

### Phase 4 -- deployment shape

> **Superseded (auth): the front-end is now oauth2-proxy OIDC, not nginx
> basic-auth.** The notes below record the original nginx/basic-auth stack and
> stay as the reasoning-of-record. What actually ships: a single oauth2-proxy
> service terminates TLS and runs an OIDC login against an external IdP,
> reverse-proxying to kosrunner with `/webhook/*` left open. It injects the
> verified identity as `X-Forwarded-Email` and leaves authz to kosrunner's own
> committed access config (an `email -> level` map behind a swappable
> `resolve-level` seam; groups + per-job `allow:` are the next step), because
> the IdP will not emit group/role claims. TLS is a mounted full-chain cert (no
> ACME on internal PKI). The `nginx/` directory is removed; see
> [compose.yml](../compose.yml) and [.env.example](../.env.example).

Decided against splitting the Containerfile into a baseline + thin job layer.
That split only pays off in a multi-definition gitops/CI world (a registry
baseline that each job repo builds `FROM`, so a definition edit rebuilds seconds
of layer, not the whole image) -- which is the *work* deployment, not what the
example needs to teach. For the example, a **compose stack** shows the actual
topology, which is the more instructive artifact. The two-tier split stays a
documented "for production gitops" note, not built here.

- **[compose.yml](../compose.yml)**: `kosrunner` (built from the single
  [Containerfile](../Containerfile), run history on a named `runs:` volume via
  `KOSRUNNER_STORAGE_ROOT`, not published) + `nginx` (the only host-facing
  service). `podman compose up --build` stands up the whole shape; state
  survives `down`/`up` on the volume (the PVC stand-in).
- **[nginx](../nginx/nginx.conf)** frontend: `/webhook/*` proxied **open**
  (inbound triggers); everything else (`/ui`, `/admin`) behind **basic auth**.
  View-vs-control splitting (read-only UI open, mutations gated) is a noted
  future refinement; for now the whole control surface is gated as one.
- **Credentials** come from `KOSRUNNER_USER` / `KOSRUNNER_PASS` env with demo
  defaults `ADMIN` / `ADMIN`. The htpasswd is generated at nginx start
  ([nginx/10-htpasswd.sh](../nginx/10-htpasswd.sh) via `openssl passwd`), so no
  hash is committed -- the repo holds only the literal `ADMIN` placeholder, which
  reads as a demo default, not a leaked secret. `.env.example` documents it.
- Parked for later: per-webhook trigger UUID as a bearer-ish token in the job
  description, as minimal auth for the open webhook surface. (Promoted to a real
  requirement in Phase 5 as R1.)

### Phase 5 -- front-end auth, webhook doors & reply model -- DONE (2026-07-20)

> **All five requirements landed 2026-07-20**, in the planned order, plus a
> koskari-authored unittest suite (`tests/`, tox-driven -- see "Verifying as
> we go") that arrived ahead of the fork-time schedule. Each requirement
> below carries a *Landed:* note recording what shipped and where it
> deviated from the sketch. The two deliberate deviations: R3 dropped the
> `input:` sub-item for the handler's own typed formals, and R4 made groups
> first-class values instead of string keys.

The next body of work is a single entangled collection, not a menu. Every item
below is committed; none is dropped to focus on the others. The ordering is by
how deep each reaches into the request path, least-coupled first, which is also
the order they can be built without redoing shared substrate.

The through-line: the route layer in [server.ki](../kosrunner/server.ki)
currently hides the `Request` from handlers (`add-route` touches only
`request.json`; `add-action`/`add-page` inners take a path item or nothing). R1,
R4, and R5 all need what it hides -- headers, then identity. So there is **one
request-context seam**, born small in R1 and widened twice, rather than three
separate reaches into the request.

Auth is split the way the earlier phases settled it: **authn** is oauth2-proxy
(identity as the trusted `X-Forwarded-Email` header, or absent = anonymous);
**authz** is kosrunner's own committed lists. Anonymous is a first-class value,
the read floor of one ladder that runs `anonymous (read)` -> in-group `control`
(trigger per `allow:`) -> `operator` (trigger all, enable/disable). "Admin" in
the deploy sense (rebuild the image/compose with new config) is the config-time
authority that authors everything below; it is never a runtime role.

#### The requirements

**R1 -- webhook token (machine-door guard).**
- *Intent:* the open `/webhook/*` surface gains a per-webhook secret the caller
  must present; missing or wrong -> `401`, no run.
- *Surface:* a per-job `token:` (idiomatically `(reqenv "…")`, matching the
  secrets pattern), enforced only when set, so an unset webhook stays open and
  backward-compatible.
- *Lands:* `WebhookJob` ([webhook.ki](../kosrunner/webhook.ki)) holds the token;
  the check runs at the route boundary in `add-route`, before `self.run`. This
  is the first handler that must read a **header** off the `Request` -- it
  births the request-context seam.
- *Choice:* where the token rides -- header (`X-Kosrunner-Token`), path segment,
  or query. Header keeps it out of URLs and logs.
- *Landed:* as sketched. `WebhookConfig` gained `token` (None = open);
  `check-token` compares the `X-Kosrunner-Token` header in constant time at
  the route boundary, before `record-start`, so a rejected call leaves no
  run. The seam is `RequestContext` in [server.ki](../kosrunner/server.ki);
  `add-route` passes `(handler payload ctx)` and lets `HTTPException`
  through as the real HTTP error.

**R2 -- async webhook reply (background run).**
- *Intent:* a webhook can reply immediately with a receipt
  (`{run_id, status: "started"}`) and run in the background, versus the current
  await-and-return-the-result.
- *Surface:* a per-webhook `wait:` / `mode:` flag; sync returns the callback
  result, async returns the receipt.
- *Lands:* `Job.run` ([job.ki](../kosrunner/job.ki)) splits. Today it does
  record-start -> invoke -> record-finish -> return, all inline. Async records
  the start (getting `runid`), spins the invoke+finish as a `create-task`, and
  returns the receipt now. The `_current-run` contextvar Phase 1 was careful to
  set in `Job.run`'s own context moves into *the task's* context -- set, invoke,
  and flush must stay together inside the task, or the log buffer goes invisible
  again.
- *Choice:* async-default or sync-default. Either way the receipt's run id is
  the handle the read surface polls, so R2 wires here to status-visibility.
- *Note:* independent of the request-context seam; it touches execution, not the
  request. Can be built in parallel with R1.
- *Landed:* sync-default, per-webhook `wait:` on `WebhookConfig`. `Job.run`
  split exactly as sketched -- `_tracked-invoke` is the recorded body and
  the `_current-run` contextvar set/invoke/flush stays together inside it,
  so the log buffer survives the move into the task's context. `JobSet`
  grew `_inflight` + `spawn-run`/`_run-done` (the loop holds tasks weakly,
  so the set is the strong ref; escaped crashes are logged) and `_teardown`
  awaits the in-flight set before the store closes.

**R3 -- webhook input parameters (double duty).**
- *Intent:* a webhook *declares* its inputs, and that one declaration drives both
  (a) inbound coercion/validation of the POSTed JSON and (b) the UI -- the
  manual-trigger form and the reveal of what a past run received.
- *Surface:* a new `input:` sub-item on the webhook job (peer of
  `handler`/`configure`) declaring name/type/required/default. Today the handler
  gets the whole raw payload as one positional; with a spec the framework coerces
  the payload and binds named args, so `handler [ref force]` works and malformed
  input is a `400`.
- *Lands:* `webhook.ki` (the new sub-item + coercion), `job.ki` (arg binding into
  `run`), `ui.ki` (render a form from the spec; render stored args on the
  run-detail page). Normalized args already land in `trigger.json`, so retrigger
  and past-run reveal ride existing storage; R3 adds *fresh typed input*.
- *Reuse lever:* the annotations / `inspect` FastAPI work may allow building a
  dynamic Pydantic model from the spec instead of hand-rolled coercion.
- *Note:* needs no auth, but produces the trigger forms and typed-args reveal
  that R5 later projects. Build before R5 so R5 has something concrete to filter.
- *Landed (deviation):* there is no `input:` sub-item. The handler's own
  typed formals ARE the input contract -- `handler [[ref :: str]
  [force :: bool value: False]]:` -- read off the callback via
  `inspect.signature` when `WebhookJob.handler` stores it (the reuse lever,
  taken at the surface level: one declaration, nothing to drift). Hand-rolled
  `coerce-input` validates at dispatch (unknown key / missing required /
  uncoercible -> 400); an unannotated handler takes the raw payload
  unchanged. `Job.run` carries a kwargs channel recorded beside args in
  trigger.json, so retrigger replays typed inputs for free. The UI half
  (forms + reveal) shipped with R5.

**R4 -- access lists (membership at access time).**
- *Intent:* at action time, resolve the proxy-set identity -> level / group
  membership against the committed lists; deny with a relevant page/JSON on
  failure.
- *Surface:* `config access:` (groups -> member emails, mapped to levels) plus
  per-job `allow:`. A small `access.ki`: `resolve-identity [request]` (read
  `X-Forwarded-Email`), `resolve-level`, `member-of?`.
- *Lands:* the action handlers in `ui.ki` / `admin.ki` gain a check before acting
  -- `403` HTML for the UI, `403` JSON for admin. The membership test is trivial;
  the work is plumbing identity to the check point, which **widens the R1 seam**
  from "read a token header" to "carry resolved identity."
- *Landed (deviation):* groups are first-class VALUES, keyed by binding
  rather than string. `group deployers:` (a `Group` macro in
  [access.ki](../kosrunner/access.ki), named after its own binding) defines
  the value; `config access:` lists `groups: #[deployers]` and a job says
  `allow #[deployers]` -- a typo'd group is a load-time NameError, never a
  silently empty string match, and the membership test runs directly
  against the referenced values. `resolve-identity`/`resolve-level` are the
  swappable seam as planned; every route helper in server.ki now builds a
  `RequestContext`. Enable/disable/shutdown require operator; retrigger
  requires trigger rights on the run's job; reads stay the anonymous floor.
  AdminJob/UiJob split `mount` from `start` so the guarded routes are
  testable without uvicorn.

**R5 -- per-user surface projection (reveal only what you may touch).**
- *Intent:* the dashboard / webhook page shows only the jobs a user may trigger;
  buttons appear only for permitted ones.
- *Surface:* a `visible-jobs [identity]` filtering `list-jobs` through per-job
  `allow:` intersected with the user's groups (operator sees all; anonymous sees
  the read-only floor).
- *Lands:* the UI **GET** handlers ([ui.ki](../kosrunner/ui.ki)) -- so identity is
  needed at render time, not just on actions, threading it through the read
  surface too. Templates render the filtered set and the R3 trigger forms.
- *The edge:* projection is display only. Hiding a button without R4 enforcing
  the action is a leak -- a user could POST the hidden trigger. R5's filter and
  R4's enforcement are two halves of one guarantee; neither stands alone.
- *Landed (interpretation):* the jobs/runs tables stay visible to everyone
  -- that IS the anonymous read floor, matching the admin API's open reads;
  a control-level user must never see less than anonymous. What projects is
  the ACTIONABLE surface: `JobSet.visible-jobs [identity]` annotates rows
  with `may_trigger`/`may_control` (via an identity-pure `may-trigger?`
  split out of `trigger-allowed?`), the dashboard gates its buttons on
  those flags, and a new Trigger section lists only permitted jobs, each
  with a manual-trigger form whose fields derive from the R3 typed formals
  (bool -> checkbox, defaults prefilled). `POST /ui/trigger/{name}` -- via
  a new `add-form` route helper (urlencoded; python-multipart joined the
  deps) -- coerces through the same `coerce-input` and fires a tracked
  `manual` run, 403 HTML / 400 escaped on refusal. The run page reveals
  the stored trigger and hides its retrigger button by the same flag.
  Enforcement under projection is tested: the hidden form still 403s.

#### Approach

One seam, born small, widened twice; the rest lands on it in detachment order.

1. **R1 births the request-context seam.** `add-route` and kin begin passing a
   context (headers now, room for identity later) to handlers. Build it minimal
   for the token but shaped so R4 can hang identity on the same hook. R1 being
   the most detached is what lets it validate the seam end to end in isolation.
2. **R2 in parallel.** A `Job.run` split plus a `wait:` flag; independent of the
   seam. Its only cross-tie is that the receipt is the handle R5's status view
   renders.
3. **R3 next, self-contained.** The `input:` vocabulary, coercion, and form
   rendering. No auth involved, but it yields the trigger forms and typed reveal
   R5 will project.
4. **R4 widens the seam.** `access.ki` + `config access:` / `allow:`, with the
   action handlers enforcing. The seam now carries resolved identity, not just a
   token.
5. **R5 composes R3 + R4.** Filtered render over R4's identity, trigger forms
   from R3, action still enforced by R4 underneath.

#### Edges to hold

- **One seam, born in R1, widened in R4/R5.** Build it extensibly the first time.
- **R5 display always paired with R4 enforcement.** Filtering the view is not a
  control; the action check is.
- **R2 receipt + run store + status view is one poll loop**, and **R3 args flow
  through it from either door** -- machine POST or UI form -- landing in the same
  `trigger.json`.
- **Token, `wait:`, `input:`, and `access:` / `allow:` are all Layer-0 committed
  config**, authored in the same definitions and shipped by the same rebuild the
  deploy-admin owns.

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

The formal suite arrived with Phase 5, ahead of the fork-time schedule: a
koskari-authored unittest suite under [tests/](../tests/), run by tox
(`make test`). The tests are `IsolatedAsyncioTestCase` subclasses written
in Koskari itself, driven by `python -m unittest` naming the modules
(`tests.webhook_token` .. `tests.webhook_surface`); the koskari CLI cannot
host them, since its ambient loop collides with the runner's. Two boundary
rules hold throughout: anything unittest calls or cleans up is wrapped
`@coroutine-fn` so it presents as a native coroutine function, and
tasks/futures (e.g. `asyncio.gather`) are auto-await-exempt, so drains are
explicit `(await ...)`. The throwaway-root `RunStore` seam paid off exactly
as planned -- the harness stands each test's store up on a temp dir and
drives the real HTTP surface over ASGI, no uvicorn, via the AdminJob/UiJob
`mount` split.

The `fake` variety ([fake.ki](../kosrunner/fake.ki)) remains the local
exercise loop: run a jobset of fake jobs, watch runs land in the store and
render in the UI.
