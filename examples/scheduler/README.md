# kosrunner -- a Koskari job scheduler

A small asyncio job scheduler whose configuration *is* a Koskari program.
The definition reads like YAML, but each indented block is either **data**
(a config) or **code** (a job handler), and the whole thing lowers to real
module bindings plus a live, controllable `JobSet`.

```koskari
jobset workload:

    config webhook:
        port: 8080
        prefix: "/webhook"

    job cron heartbeat [* * * * *]:
        handler []:
            (print f"The time is {(datetime.now)}")

    job webhook ping:
        handler [payload]:
            #{ok: True msg: "pong"}
```

Three job varieties ship: **cron** (aiocron), **webhook** (FastAPI/uvicorn),
and **kafka** (aiokafka), plus a broker-free **fake** variety used in tests.

See [example.ki](example.ki) for the full definition.


## Running

Install the package and its dependencies, then run the definition with the
`koskari` CLI:

```bash
pip install .
koskari example.ki
```

`example.ki` starts a cron heartbeat (fires every minute) and a webhook
server on `:8080`. Drive the webhook with any HTTP client:

```bash
curl -X POST localhost:8080/webhook/ping -d '{}'
# => {"ok": true, "msg": "pong"}
```

The control plane shares that same server under `/admin`:

```bash
curl localhost:8080/admin/jobs                    # list jobs + state
curl -X POST localhost:8080/admin/enable/orders   # enable a dormant job
curl localhost:8080/admin/runs                    # recent runs
curl localhost:8080/admin/runs/<run-id>           # one run + its output/result/trigger
curl -X POST localhost:8080/admin/retrigger/<run-id>   # replay a run (e.g. a failed one)
```

Every fire is recorded to the storage root (`config storage:`, or
`$KOSRUNNER_STORAGE_ROOT`, or `./.kosrunner`): a row in `index.db` plus a
`runs/<job>/<run-id>/` directory holding the trigger args, captured log, and
result. Re-trigger replays a run's stored args as a new run linked to the
original, so a failed job can be re-run without re-sending its input.

A human-facing **web UI** shares that same server under `/ui` -- point a browser
at `http://localhost:8080/ui`. It renders a jobs + recent-runs dashboard with
enable/disable and one-click retrigger, and a per-run detail page
(`/ui/runs/<run-id>`) showing the captured output and result. It reads the live
`JobSet` and run store in-process; its form actions post to `/ui` and redirect
back, so `/admin` stays a clean JSON API for scripting.

The **kafka** jobs in the example are marked `disabled` because they need a
running broker. To enable them, point `config kafka:` at your broker and drop
the `disabled` keyword from the job headers.

A container build is also provided:

```bash
podman build -t kosrunner -f Containerfile .
```


## Deploying: the compose stack

[compose.yml](compose.yml) stands up the real deployment shape -- the scheduler
behind an **oauth2-proxy OIDC frontend**, with run history on a named volume.
Unlike a bare app run, this stack needs real auth wiring first: copy
[.env.example](.env.example) to `.env` and fill in your IdP details, then drop a
full-chain TLS cert at `./certs/tls.crt` and its key at `./certs/tls.key`.

```bash
cp .env.example .env      # then edit: issuer, client id/secret, cookie secret
podman compose up --build
```

- **oauth2-proxy** is the only host-facing service (`:8443`, HTTPS). It
  terminates TLS, runs the OIDC login against your IdP, and reverse-proxies to
  kosrunner. `/webhook/*` is passed **open** (the inbound-trigger surface);
  everything else requires a login. It only proves a real login happened -- it
  injects the verified identity as an `X-Forwarded-Email` header and leaves
  *who may do what* to kosrunner's own access config.
- **kosrunner** runs the definition, not published to the host -- only the proxy
  reaches it. Its run history lives on the `runs:` volume
  (`KOSRUNNER_STORAGE_ROOT`), so it survives `compose down`/`up`, the way a pod's
  state survives on a PVC.

```bash
curl -k -X POST https://localhost:8443/webhook/ping -d '{}'   # open
curl -k https://localhost:8443/ui                             # 302 -> IdP login
open  https://localhost:8443/ui                               # browser logs in
```

> **No committed secrets, and no ACME.** The client id/secret and cookie signing
> key come from `.env` (see [.env.example](.env.example)); the TLS cert is a file
> you mount, since internal PKI rarely offers ACME. Register
> `https://<host>/oauth2/callback` as an allowed redirect URI on the OIDC client
> when you create it. For a no-auth local loop, just run the app or container
> directly (it serves `/ui` on `:8080`) -- the compose stack is the authenticated
> shape.

The compose stack requires the `koskari:local` base image the Containerfile
builds `FROM` (see the repo Makefile). For a production gitops flow you would
instead publish a versioned baseline image and layer each job definition on top
(`FROM kosrunner:<ver>` + one `COPY`); that split is out of scope for this
example.


## The surface language

A `jobset` is a flat, ordered collection of two kinds of entry: **configs**
and **jobs**. They may be intermixed freely.

### Configs

```koskari
config <type> [name]:
    key: value
    ...
```

The block is data. Each config type is a **dataclass** that declares its
valid fields, their types, and sensible defaults, so the `key: value` lines
are checked against that shape: unknown keys and missing required fields are
caught when the config is built, not silently at read time. Fields you omit
take the dataclass default (e.g. a kafka config gets `security_protocol
"PLAINTEXT"`, `auto_offset_reset "latest"` for free).

The **first** config of a type (whether named or not) becomes that type's
**default**; further configs of the same type must be named and are used only
when a job asks for one by name.

```koskari
config kafka:                 ; unnamed -> the default kafka config
    servers: "localhost:9092"

config kafka audit-cluster:   ; named -> addressable, does not steal default
    servers: "audit-1:9092"
```

A job's `config:` sub-item **merges** onto the base it selects: the result is
a new config instance with the base's fields plus the job's overrides, every
other field still carrying its default.

### Jobs

```koskari
job <type> <name> <trigger-key...> [disabled]:
    <sub-item>
    ...
```

The header is the job's **identity and trigger**: its type, its name, the
type-specific trigger key (cron's 5 fields, kafka's topic, nothing for
webhook), and an optional trailing `disabled`. Jobs are enabled by default;
`disabled` registers the job but leaves it dormant until the control plane
enables it.

The body is a set of **sub-items**. The mental model:

> The header is the job's constructor arguments; each sub-item is a method
> call on the constructed job.

```
job <type> <args...>:      =>  j = <Type>Job(<args...>)
    config: ...            =>  j.configure(via: .. **overrides)   [inherited]
    handler [f]: body      =>  j.handler(lambda [f] body)         [inherited]
    <setter> ...           =>  j.<setter>(...)                    [variety]
```

`handler` and `config` are inherited by every variety; a variety adds its own
sub-items just by defining more methods (kafka adds `on-error`). A job with no
`config:` sub-item is wired to the default config of its type automatically.

```koskari
job kafka audit "topic.audit":
    config:                        ; select a named base + override a key
        via: audit-cluster
        group_id: "audit-consumers"
    on-error [err msg]:            ; kafka-variety code setter
        (print f"dropped {msg}: {err}")
    handler [msg topic]:
        (print f"audit {topic}: {msg}")
```

### Control plane

The `JobSet` tracks its jobs by name and exposes `list-jobs`, `enable`, and
`disable` in-process: `(workload.enable "nightly-report")` starts a dormant
job; `(workload.disable "orders")` stops a running one.

The same surface is exposed over HTTP by an **admin job**:

```koskari
config admin:
    port: 8080          ; same port as the webhook -> shared server
    prefix: "/admin"

job admin control       ; no sub-items; serves from its config alone
```

The admin job mounts `GET {prefix}/jobs`, `POST {prefix}/enable/<name>`,
`POST {prefix}/disable/<name>`, plus the run-history surface: `GET {prefix}/runs`,
`GET {prefix}/runs/<id>`, and `POST {prefix}/retrigger/<id>`. It reaches its
`JobSet` through a back-reference the JobSet sets when it registers each job
(`job.jobset`), and the run store hangs off that same JobSet (`job.jobset.store`).

Because HTTP servers are **interned by host:port** (not by config), giving the
admin config the same port as a webhook makes them share one FastAPI server,
each mounting under its own prefix -- `/webhook/*` and `/admin/*` on `:8080`.
Give admin a different port for a standalone control server.


## How it works

The surface lowers in two symmetric halves that meet at real module bindings.

### Dispatch: three separated responsibilities

Sub-items are variety-specific, but the machinery that reads them stays
generic. Three concerns land in three places:

1. **Layout** (per sub-item head) folds a block into a form. `config:` uses
   `inline-block` (its `key: value` lines splice in flat); everything else uses
   the default `layout/block` (children stay as forms).
2. **The `job` macro** is a thin dispatcher: `(job kafka audit ...)` becomes
   `(job/kafka audit ...)` by synthesizing the `job/<type>` symbol. Each
   `job/<type>` owns its header grammar and tag-injects each sub-item with its
   `(<type> <name>)` address.
3. **The sub-item macros** (`config`, `handler`, `on-error`) route the tagged
   form to a method call on the named instance. Their base case is a generic
   fall-through:

   ```koskari
   case (config (tp name) params ...):   ; tp ignored, not a literal
       (.configure name params ...)
   ```

   A variety overrides only when it must, via `extend-syntax` with its type as
   a literal (newer rules win, so the override beats the fall-through). This is
   the same open-dispatch trick as `(help (pragma key))` in the bootstrap.

`config/<type>` follows the same `<prefix>/<type>` convention as `job/<type>`:
the `jobset` macro rewrites `config kafka name: ...` into `(config/kafka name
...)`, and each variety defines `config/kafka` to build its config object.

### Defaulting: first-wins via ref truthiness

Each config emits an idempotent guard so the first config of a type claims the
shared default slot, with no registry and no duplicate check:

```koskari
(define default/kafka)                   ; declare-only, idempotent
(unless &default/kafka                   ; the ref is falsey until first set
    (set! default/kafka audit-cluster))  ; so the FIRST config wins
```

A job with no explicit config wires to `default/<type>` (guarded by
`&default/<type>`, so a type with no config -- cron here -- simply skips it).

### Lowering

```
surface text
  | layout reader (default block; inline-block for config)
read forms   (jobset (config kafka ..) (job kafka audit ..) ..)
  | jobset macro: partition (configs first) + rewrite entries
  | job macro + sub-item dispatch
construction code:
    (define default/kafka ..)            ; configs first, guarded
    (define audit (KafkaJob "topic.audit"))
    (.configure audit via: default/kafka)
    (.handler   audit (lambda [msg] ..))
    (define workload (JobSet "workload"))
    (.register workload "audit" audit)
  | compile + eval
LIVE   named jobs wired to resolved configs; `workload` holds name -> job
       and enabled flags; the REPL can name every job and config directly.
```

Because jobs and configs become **real module bindings**, you can attach a
REPL and inspect or poke any of them (`audit`, `default/kafka`, `workload`)
by name.


## Framework layout

| File | Contents |
|---|---|
| [kosrunner/job.ki](kosrunner/job.ki) | `Config`, `Job` base (configure/handler/`run` tracking/enabled, `jobset` back-ref), `JobSet` (tracking + control plane incl. `retrigger`) |
| [kosrunner/subitems.ki](kosrunner/subitems.ki) | `jobset`, `job`, `config`, `handler`, `on-error`, `with-subitems`; the `config/logging` service config; `layout/config` |
| [kosrunner/store.ki](kosrunner/store.ki) | `StorageConfig` + `RunStore` (aiosqlite run history, per-run artifacts, log capture) + `config/storage` |
| [kosrunner/server.ki](kosrunner/server.ki) | `ServerConfig` (host/port/prefix dataclass), `HttpServer` (FastAPI/uvicorn, interned by host:port) + `get-server`; route/listing/action/page helpers |
| [kosrunner/cron.ki](kosrunner/cron.ki) | `CronJob` + `config/cron` / `job/cron` (aiocron) |
| [kosrunner/kafka.ki](kosrunner/kafka.ki) | `KafkaJob` + `on-error` + `config/kafka` / `job/kafka` (aiokafka) |
| [kosrunner/webhook.ki](kosrunner/webhook.ki) | `WebhookJob` + `config/webhook` / `job/webhook` (mounts routes on a shared `HttpServer`) |
| [kosrunner/admin.ki](kosrunner/admin.ki) | `AdminJob` + `config/admin` / `job/admin` -- the JSON control plane over HTTP |
| [kosrunner/ui.ki](kosrunner/ui.ki) | `UiJob` + `config/ui` / `job/ui` -- the Jinja2 web UI (jobs/runs dashboard, retrigger), in-process on the shared server |
| [kosrunner/fake.ki](kosrunner/fake.ki) | `FakeJob` -- a broker-free variety for tests |


## Adding a job variety

A variety is a `Job` subclass plus two syntaxes. Nothing in the core
enumerates types, so this is the whole recipe:

1. Declare `<Type>Config` as a `define-dataclass` subclass of `Config` (or of
   `ServerConfig` if it needs a shared HTTP server) -- its fields, types, and
   defaults are the config's contract. The base `Config` supplies dataclass
   `merge` for free.
2. Subclass `Job`; implement `start`/`stop`, and store the trigger key in
   `__init__`. Read effective config from `self.config` by attribute
   (`self.config.servers`), call `self.callback` as the handler. Add variety
   sub-item methods (like kafka's `on-error`) as needed.
3. Define `config/<type>` (construct the dataclass, bind the name, run the
   `&default/<type>` guard) with the `()`-sentinel unnamed case.
4. Define `job/<type>` (parse the header, construct the job, wire the default
   config, tag-inject sub-items).
5. Export all of it. Remember: a macro's free names resolve at the **use
   site**, so everything a `job/<type>` or `config/<type>` expansion names
   (its class, helpers) must be exported for `import-all-from kosrunner`.

`kosrunner/fake.ki` is the smallest complete worked example.


## Design constraints

The surface was designed to satisfy seven requirements; each maps to a
feature above:

1. Jobs have names, tracked at the jobset level -- `JobSet` indexes by name.
2. Configs connect to jobs by type (+ optional name) -- `via:` / defaulting.
3. Blocks read like YAML, DATA or CODE -- layouts + the sub-item split.
4. A jobset is a flat collection of intermixed configs and jobs -- the
   partition-and-splice `jobset` macro.
5. First config of a type is the default; others are named -- the `&ref` guard.
6. Jobs are enabled by default; a job may declare itself disabled -- the
   trailing `disabled` header keyword.
7. The jobset offers list/enable/disable -- `JobSet`'s control-plane methods,
   exposed over HTTP by the admin job.
