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
- **Bulk stays out of the DB.** Layout under the PVC root:

  ```
  <root>/index.db
  <root>/runs/<run_id>/trigger.json    ; the args, so re-trigger can replay
  <root>/runs/<run_id>/output.log      ; captured handler logs
  <root>/runs/<run_id>/result.json     ; return value / exception
  ```

  The DB holds queryable metadata plus the artifact path; the UI streams log
  files straight from disk.
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

- New `kosrunner/store.ki`: the `RunStore` over an aiosqlite connection, schema
  + WAL on open, and a **reconciliation pass** that marks any row still
  `running` from a dead pod as `interrupted`.
- `config storage:` service config (a `StorageConfig` dataclass + `config/
  storage` syntax, mirroring `config/logging`): the PVC root and retention
  knobs, root resolved via `reqenv`/env so it is a mount path, not baked.
- Hang the store off `JobSet` (`self.store`), constructed at start.

### Phase 1 -- the `Job.run` keystone

- Base `Job.run(self, *args, cause, parent: None)`: record-start via
  `self.jobset.store`, open per-run log capture (a contextvar-keyed logging
  handler writing to that run's `output.log`), invoke `self.callback`, capture
  return/exception to `result.json`, record-finish, and return the handler
  result (webhook needs it back).
- Refactor each variety to funnel through it: cron's `fire` ->
  `(self.run cause: 'schedule')`, kafka's `_consume` ->
  `(self.run value cause: 'kafka')`, the webhook route -> returns
  `(self.run payload cause: 'webhook')`.
- Nudge handlers from `print` to `logger` so capture is clean (print-capture
  stays a stretch).

This is the load-bearing change; everything downstream reads what it writes.

### Phase 2 -- admin JSON API growth

Reusing the existing `add-listing` / `add-action` / `add-route` helpers in
[admin.ki](../kosrunner/admin.ki), add: `GET /runs` (filter by job / status /
time), `GET /runs/{id}`, `GET /runs/{id}/log` (stream the file), `POST
/runs/{id}/retrigger`. Retrigger looks the run up, finds the job by name on the
jobset, and replays via `Job.run`.

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
