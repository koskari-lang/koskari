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
            #{"ok": True "msg": "pong"}
```

Four job varieties ship: **cron** (aiocron), **webhook** (FastAPI/uvicorn),
**kafka** (aiokafka), and **umb** (stompman), plus a broker-free **fake**
variety used in tests.

See [example.ki](example.ki) for the full definition.


## Running

Install the package and its dependencies, then run the definition with the
`koskari` CLI:

```bash
pip install '.[all]'
koskari example.ki
```

The two broker varieties are **extras**, so a definition that declares no
kafka or umb job need not carry their client libraries:

| install | brings | enables |
|---|---|---|
| `pip install .` | the framework: cron, webhook, admin, ui, storage, access | everything but the brokers |
| `pip install '.[kafka]'` | aiokafka | `job kafka` |
| `pip install '.[umb]'` | stompman (needs Python 3.11+) | `job umb` |
| `pip install '.[all]'` | both | everything -- what the container image installs |

`import kosrunner` never needs an extra: both variety modules stay
importable, and each resolves its client library in the job
**constructor**. A definition naming a variety whose extra was never
installed therefore dies at load time with the install line to fix it --
the same bargain `reqenv` makes for a missing secret, rather
than a pod that comes up healthy and fails at first connect:

```
ImportError: the umb job variety needs the 'stompman' package:
pip install koskari-kosrunner[umb]
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
at `http://localhost:8080/ui`. It is a three-level hierarchy with a breadcrumb
bar (`Dashboard / <job> / <run>`): the dashboard is a gateway listing jobs
grouped by variety, each with its last run; a job's page carries the actions --
gated enable/disable, a manual-trigger form when the job supports one, and its
run history as a paged table; a run's page shows the captured output, result,
and stored trigger. It reads the live `JobSet` and run store in-process; its
form actions post back to the job they concern, so `/admin` stays a clean JSON
API for scripting.

The **kafka** and **umb** jobs in the example are marked `disabled: True`
in their config blocks because they need a running broker. To enable them,
point `config kafka:` / `config umb:` at your broker and drop the
`disabled` lines.

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
other field still carrying its default. With no base at all -- no `via:`, no
default of the type declared -- the block builds a **fresh** config of the
job's variety from the overrides alone, so a small definition never needs a
standalone `config <type>:` block just to set one field.

Every config also carries **`disabled`** (default False): a config declaring
`disabled: True` registers its jobs dormant until the control plane enables
them. Because it is an ordinary merged field, a **shared or named config
carrying `disabled: True` dims every job wired to it** -- a useful group
switch ("this broker is down"), and deliberate: a single job opts back in
with its own `config: disabled: False`.

### Jobs

```koskari
job <type> <name> <trigger-key...>:
    <sub-item>
    ...
```

The header is the job's **identity and trigger**: its type, its name, and
the type-specific trigger key (cron's 5 fields, kafka's topic, nothing for
webhook). Jobs are enabled by default; declaring one dormant is config
data -- `disabled: True` in its `config:` block (see Configs above).

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
    handler [msg]:
        (print f"audit: {msg}")
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


## The job varieties

Every job shares the header shape `job <type> <name> <trigger-key...>`
and these inherited sub-items:

| sub-item | kind | meaning |
|---|---|---|
| `config:` | data | select a base config (`via: <name>`) and/or override fields; merged onto the base by dataclass `merge` |
| `handler [..]:` | code | the callback fired per trigger; its formals vary by variety (below) |
| `allow #[<group> ..]` | data | the Group values whose members may trigger this job (see [Access](#access-who-may-do-what)) |

A body-less job is written with **no trailing colon** (`job admin control`);
a colon with an empty body produces a nil sub-item that fails the variety's
pattern match. Every fire funnels through `Job.run` as a tracked run, with
`cause` reflecting the door it came through (`schedule`, `webhook`,
`kafka`, `umb`, or `manual`).

### cron

```koskari
job cron heartbeat [* * * * *]:
    handler []:
        (print f"The time is {(datetime.now)}")

job cron nightly "0 0 * * *":
    handler []:
        (print "nightly")
```

The trigger key is the classic five-field crontab schedule, either as a
bracket of five fields (joined into the schedule string at expand time) or
as a single string. Scheduling is [aiocron](https://github.com/gawel/aiocron)
(croniter syntax -- see its docs for the field grammar). The handler takes
no arguments. `config cron:` exists (`timezone`) but cron jobs normally
carry no config at all.

### webhook

```koskari
job webhook ping:
    handler [payload]:
        #{"ok": True "msg": "pong"}

job webhook deploy:
    config:
        token: (reqenv "DEPLOY_TOKEN")
        wait: False
    handler [[ref :: str] [force :: bool value: False]]:
        (print f"deploying {ref} force={force}")
        #{"ok": True}
```

No trigger key: the job's name is the route path segment, mounted as
`POST {prefix}/{name}` on the shared server. The handler's return value is
the HTTP response body.

**The handler's formals are its input contract.** An untyped handler
(`handler [payload]`) receives the parsed JSON body as one positional. With
typed formals, each annotated parameter is a declared input: bare
annotation (`[ref :: str]`) means required, a `value:` default
(`[force :: bool value: False]`) means optional. The POSTed JSON is
validated and coerced against the signature -- unknown keys, missing
required keys, and uncoercible values are a `400` -- and the handler is
called with named args. The same declaration drives the UI's manual-trigger
form, and the coerced args land in `trigger.json` so re-trigger replays
them.

`WebhookConfig` extends the shared server shape (`host`, `port`, `prefix`)
with two per-webhook knobs:

- `token` -- when set, callers must present it in the `X-Kosrunner-Token`
  header (compared in constant time) or receive a `401` before any run is
  recorded. Unset = open. Idiomatically `(reqenv "...")`, the secrets
  pattern.
- `wait` -- `True` (default) awaits the handler and returns its result;
  `False` replies immediately with a `{run_id, status: "started"}` receipt
  and runs the handler in the background. The receipt's run id is the
  handle to poll, through the read-back below.

Every webhook mounts a second route beside its POST door:

```
POST {prefix}/{route}            fire it       -> result, or a receipt
GET  {prefix}/{route}/{run_id}   follow it     -> the run's detail
```

The read-back answers the receipt. It takes the **same `token`** the POST
takes, so a webhook whose fire is guarded has no unguarded reader, and it
is **scoped to that webhook's own runs** -- a run belonging to another job
reads as a `404` exactly like an unknown id, so the route never confirms
that someone else's run exists. The body is the same `run-detail` shape
`/admin/runs/{id}` serves: `status`, the `result` artifact, the captured
`output`, and the stored `trigger`.

The two routes stand or fall together, by design: an open (token-less)
webhook has an open read-back. A definition that leaves the door unguarded
is declaring that its environment decides who reaches the pod, and a
caller admitted to *trigger* the job on those terms is admitted to *review*
what it did.

That scoping is the point. `/admin`'s run surface is the anonymous read
floor over *every* job's history (see [Access](#access-who-may-do-what)),
and it exists at all only if the definition declares `job admin control` --
possibly on another port. Pointing a machine caller there to follow its own
run would hand it a far wider grant than the one webhook it was trusted
with, so the receipt is answered on the door that issued it.

### kafka

```koskari
job kafka audit "topic.audit":
    config:
        via: audit-cluster
        group_id: "audit-consumers"
        disabled: True
    on-error [err msg]:
        (print f"audit dropped {msg}: {err}")
    handler [msg]:
        (print f"audit: {msg}")
```

The trigger key is the topic. Each consumed message is decoded utf-8 and
passed to the handler as one positional. Kafka adds the `on-error [err msg]:`
code sub-item -- called with the exception and the message when the handler
raises (the run is still recorded as `failed`).

`KafkaConfig` fields pass through to
[aiokafka's `AIOKafkaConsumer`](https://aiokafka.readthedocs.io/) nearly
1:1 -- `servers` (its `bootstrap_servers`), `group_id`,
`security_protocol`, `ssl_cafile`/`ssl_context`, the `sasl_*` trio,
`auto_offset_reset`, `enable_auto_commit` -- see the aiokafka docs for
their semantics. Local conveniences: an `ssl_context` is built from
`ssl_cafile` automatically when the protocol involves SSL, and the
politeness knobs `fetch_max_wait_ms` (1000) / `retry_backoff_ms` (500)
default gentler than aiokafka's own.

### umb

```koskari
config umb:
    servers: "umb-broker01.example.com,umb-broker02.example.com"
    ssl_certfile: (reqenv "UMB_CERT")
    ssl_keyfile: (reqenv "UMB_KEY")
    ssl_cafile: "/etc/pki/umb/ca.crt"

job umb builds "/topic/VirtualTopic.eng.brew.build.complete":
    on-error [err msg]:
        (print f"build event dropped {msg}: {err}")
    handler [msg]:
        (print f"build: {msg}")

job umb status "/queue/kosrunner.status.request":
    config:
        reply_to: "/queue/kosrunner.status.reply"
    handler [msg]:
        #{"ok": True "asked": msg}
```

The Unified Message Bus, over STOMP 1.2. The trigger key is the
destination. Each received message is decoded utf-8 and passed to the
handler as one positional, exactly as kafka does, and umb adds the same
`on-error [err msg]:` code sub-item.

**Replying.** A handler that returns a non-None value has it published
back as a reply -- to the incoming message's `reply-to` header, or to the
config's `reply_to` when the requester named none, carrying a
`correlation-id` echoed from the requester (falling back to its
`message-id`, which is what a requester that sent none will be waiting
on). `bytes` ride as-is and a `str` is utf-8 encoded, both with no
declared content type; anything else is JSON encoded and declared
`application/json`. Return `None` for a plain event consumer that answers
nothing. A handler that *raises* records a failed run and dispatches to
`on-error`, and never replies -- the message is still consumed, same as
kafka's offset still commits.

`UmbConfig` covers the connection: `servers` is the same comma-separated
`host:port` string kafka takes (a bare host takes the config's `port`,
defaulting to 61612, and the list is tried in turn), plus `login` /
`passcode` for a broker that wants them -- normally empty, since UMB
authenticates with an x509 client cert. That cert is the `ssl_certfile` /
`ssl_keyfile` / `ssl_password` / `ssl_cafile` group, from which an
`ssl.SSLContext` is built automatically; set `ssl_context` directly for
anything more exotic, and leave all of them unset for a plaintext
connection. `connect_headers` reaches the CONNECT frame (where UMB wants
`client-id` for a durable subscription) and `subscribe_headers` the
SUBSCRIBE frame (`selector`, `activemq.subscriptionName`). `ack` defaults
to `client-individual`. The heartbeat pair (`heartbeat_send_ms` /
`heartbeat_receive_ms`, both 10000) defaults a good deal quieter than
[stompman](https://github.com/community-of-python/stompman)'s own 1s/1s,
and the connect trio (`connect_retry_attempts`, `connect_retry_interval`,
`connect_timeout`) bounds a reconnect storm. `connection_class` selects
the transport -- unset takes plain TCP; point it at
`stompman.connection_ws.WebSocketConnection` for STOMP over a websocket.

> Note: there is no maintained asyncio-native AMQP 1.0 client -- the
> canonical `python-qpid-proton` binding is reactor-based, and the one
> asyncio bridge over it (`qpid-bow`) was archived in 2019. STOMP is the
> protocol UMB exposes that has one, which is why this variety speaks it.
> Check that your service account has the STOMP endpoint enabled.

### admin

```koskari
config admin:
    port: 8080          ; same port as the webhook -> shared server
    prefix: "/admin"

job admin control
```

No trigger key and normally no body: it serves the JSON control plane from
its config alone (`AdminConfig` is the server shape with `prefix`
defaulting to `/admin`). Routes: `GET {prefix}/jobs`, `GET {prefix}/runs`,
`GET {prefix}/runs/{id}`, `POST {prefix}/enable/{name}` /
`disable/{name}` / `retrigger/{id}` / `shutdown`. Reads are open; the
mutations are guarded by the access lists (below), answering `403` as
JSON.

### ui

```koskari
config ui:
    port: 8080
    prefix: "/ui"

job ui web
```

The human layer, same shape as admin (`UiConfig` defaults `prefix` to
`/ui`; a prefix of `"/"` or `""` mounts it at the server root and all
links stay root-relative). The pages form a hierarchy under a breadcrumb
bar (`Dashboard / <job> / <run>`):

- `GET {prefix}/` -- the **dashboard**, a gateway: jobs grouped by
  variety, one status line each (`name`, enabled, running, last run),
  the name linking to the job page and the last run linking straight
  into that run. No actions live here.
- `GET {prefix}/jobs/{name}` -- the **job page**: type and status, gated
  enable/disable buttons, the manual-trigger form when the job supports
  one, and the job's run history as a paged table (20 per page,
  `?page=N`, with per-run retrigger).
- `GET {prefix}/runs/{id}` -- the **run page**: metadata, the stored
  trigger, captured output, and result, under its job in the breadcrumb.

Whether a job gets a trigger form is the job type's call, via its
`trigger-spec`: cron says "fire now" with no fields, a typed webhook
supplies its input fields, and everything else (kafka, umb, untyped
webhooks, admin/ui) is not manually triggerable -- re-run a past run instead,
which replays its real recorded input. The actions
(`POST {prefix}/{enable,disable}/{name}`, `{prefix}/retrigger/{id}`,
`{prefix}/trigger/{name}`) redirect back to the job page they concern.

Every page is projected per user: buttons and forms render only where
the caller may act, while the pages themselves stay visible to everyone
-- the read floor. Refusals are `403` HTML, and the action handlers
enforce the same checks the projection draws, so a hidden button is
never the only control.

### fake

```koskari
job fake demo:
    config:
        note: "hello"
    handler []:
        (print "fired")
```

A broker-free variety for tests and experiments: `start`/`stop` just flip
`running` and log. `FakeConfig` (`note`, `delay`) is the smallest example
of a variety declaring its config shape. It is also the smallest complete
worked example of the variety recipe (see below).

### Service configs

Config-only types, no jobs:

- `config logging:` -- the block's `key: value` pairs are passed to
  Python's `logging.basicConfig` (see the stdlib logging docs). Setting
  `level: "DEBUG"` (or any level at/below your handlers' calls) is what
  routes handler log output into per-run capture.
- `config storage:` -- where run history lives: `root` (resolution:
  explicit `root:` -> `$KOSRUNNER_STORAGE_ROOT` -> `<cwd>/.kosrunner`),
  plus the `keep_runs`/`keep_days` retention caps (declared; pruning not
  wired yet). Singular per jobset, so only the unnamed form exists.
- `config access:` -- see next section.

### Run mnemonics

A run's id is a timestamp, which is a poor way to find the run you want.
Any handler may hang a short label on its own run instead:

```koskari
job kafka reindex "topic.documents":
    handler [msg]:
        define doc (json.loads msg)
        (mnemonic f"{(doc.get 'id')} reindexed")
        ...
```

The label is optional, non-unique, and belongs to the **run** rather than
to its trigger, so a retrigger computes its own rather than inheriting
one. It shows as the primary text of each row in a job's history (with
the run id demoted beneath it), on the dashboard's last-run cell, and at
the head of the run page; a run without one falls back to its id
everywhere.

`mnemonic` may be called from anywhere inside a handler, including an
`on-error` hook, and the last call wins, so a handler can set a
provisional label early and refine it once it knows more. The label is
read when the run closes, which means a run that labelled itself and then
failed keeps its label -- the case where it is worth the most. Whatever
is passed gets stringified, collapsed to a single line, and capped at 200
characters, since it has to fit a table cell. Outside a run the call does
nothing, so handlers stay directly callable in a test or a REPL.

Storage is a `mnemonic` column on `runs`, written by the UPDATE that
closes a run, so labelling costs no extra query. A store created before
this feature gains the column on open: `RunStore.migrate` adds any column
the running code expects that the store predates, so upgrading keeps your
history instead of discarding it.

### Watching memory

A runner that stays up for months is worth measuring. `memory-report`
(from [kosrunner/memwatch.ki](kosrunner/memwatch.ki)) is written to be the
entire body of a cron handler:

```koskari
job cron memwatch [*/5 * * * *]:
    handler []:
        (memory-report)
```

Each fire logs process totals -- RSS, live pymalloc blocks, GC-tracked
objects, threads, tasks -- followed by the types that moved furthest,
each with its live count, its movement since the previous fire, and its
drift from this process's first sample:

```
sample=5 rss=56812KiB(+340) blocks=253845(+472) live=79566(-75) collected=217 threads=3 tasks=1
  ReferenceType                    2598  last -81  first -100
  builtin_function_or_method       1376  last +0  first +22
```

Every figure is a total *and* a movement, since a total alone cannot show
a trend and a delta alone cannot show the scale it moved against. The
header's parenthesised figures are drift from the first sample; the first
sample is the baseline, so it prints bare.

It logs to a `kosrunner.*` logger, so every sample is captured into its
own run's `output.log` and the run history becomes the trend line.

Those types are ranked by the **size** of their drift, sign included. A
list of only the things that grew is worse than no list: plenty of counts
jitter around a stable mean, and one sampled while it happens to be up
looks exactly like a leak when the samples where it fell are filtered
out. Only a `first` that climbs sample after sample is retention; one
wandering either side of zero while `last` flips sign is noise.

Read `blocks` first. `live` counts only GC-**tracked** containers, and
CPython untracks a dict whose values are all scalars, so a pile-up of
strings, ints, or scalar dicts moves `blocks` while leaving `live` flat.
Blocks climbing alongside RSS means the process is holding something and
the growers name it; blocks flat under a climbing RSS means allocator
fragmentation or allocation outside pymalloc (thread stacks, sqlite, the
extension), which no amount of pruning will recover.

## Access: who may do what

Authentication is the front-end's job (oauth2-proxy injects the verified
identity as `X-Forwarded-Email`; see the compose stack above).
Authorization is the definition's own committed lists:

```koskari
group deployers:
    "dev@example.com"
    "carol@example.com"

jobset workload:

    config access:
        operators: #["root@example.com"]
        groups: #[deployers]

    job webhook deploy:
        allow #[deployers]
        handler [[ref :: str]]:
            ...
```

A `group` is a first-class value bound at module level, named after its
own binding -- `allow` and `groups:` reference the **binding**, so a
typo'd group is a load-time NameError, never a silently empty match.

The ladder: **anonymous** (no or unlisted identity) may read everything --
job lists, run history, dashboards. A **group member** may additionally
trigger (and re-trigger) the jobs whose `allow` list carries one of their
groups. **Operators** may trigger anything and enable/disable/shutdown. A
job with no `allow` list is operator-only; a jobset with no
`config access:` at all is fully open, which is the right shape for a
local loop. The UI projects this ladder (buttons and forms appear only
where you may act) while the admin/ui action handlers enforce it
underneath -- hiding a button is never the only control.

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
(var default/kafka)                   ; declare-only, idempotent
(unless &default/kafka                   ; the ref is falsey until first set
    (set! default/kafka audit-cluster))  ; so the FIRST config wins
```

The slot is a name the variety macros share, and a name a template writes is
private to its expansion. So each variety macro writes it raw, as
`-'default/kafka`: the template emits `default/kafka` unmarked, and every config
and job expansion reaches the one module binding.

```koskari
case (config/kafka () setting ...):
    (begin
        (define -'default/kafka)
        (unless &-'default/kafka
            (set! -'default/kafka (KafkaConfig setting ...))))
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
| [kosrunner/job.ki](kosrunner/job.ki) | `Config`, `Job` base (configure/handler/`run` tracking/enabled, `jobset` back-ref), `JobSet` (tracking + control plane incl. `retrigger`), `require-extra`, `watch-task` |
| [kosrunner/subitems.ki](kosrunner/subitems.ki) | `jobset`, `job`, `config`, `handler`, `on-error`, `allow`, `with-subitems`, `reqenv`; the `config/logging` service config and `configure-logging` |
| [kosrunner/store.ki](kosrunner/store.ki) | `StorageConfig` + `RunStore` (aiosqlite run history, per-run artifacts, log capture, additive column migration) + `mnemonic` + `config/storage` |
| [kosrunner/memwatch.ki](kosrunner/memwatch.ki) | `memory-report` / `memory-census` / `vmrss` -- process memory sampling for a cron handler |
| [kosrunner/server.ki](kosrunner/server.ki) | `ServerConfig` (host/port/prefix dataclass), `HttpServer` (FastAPI/uvicorn, interned by host:port) + `get-server`; route/listing/action/page helpers |
| [kosrunner/cron.ki](kosrunner/cron.ki) | `CronJob` + `config/cron` / `job/cron` (aiocron) |
| [kosrunner/kafka.ki](kosrunner/kafka.ki) | `KafkaJob` + `on-error` + `config/kafka` / `job/kafka` (aiokafka) |
| [kosrunner/umb.ki](kosrunner/umb.ki) | `UmbJob` + `on-error` + `config/umb` / `job/umb` -- UMB over STOMP (stompman), with reply-on-return |
| [kosrunner/webhook.ki](kosrunner/webhook.ki) | `WebhookJob` + `config/webhook` / `job/webhook` (mounts the POST door and its token-guarded, job-scoped run read-back on a shared `HttpServer`) |
| [kosrunner/admin.ki](kosrunner/admin.ki) | `AdminJob` + `config/admin` / `job/admin` -- the JSON control plane over HTTP |
| [kosrunner/ui.ki](kosrunner/ui.ki) | `UiJob` + `config/ui` / `job/ui` -- the Jinja2 web UI (dashboard -> job page -> run page hierarchy, gated actions, trigger forms, paged history), in-process on the shared server |
| [kosrunner/access.ki](kosrunner/access.ki) | `Group` + `group`, `AccessConfig` + `config/access`, the identity/level seam (`resolve-identity`/`resolve-level`) and the operator/trigger guards behind `/admin` and `/ui` mutations |
| [kosrunner/fake.ki](kosrunner/fake.ki) | `FakeJob` -- a broker-free variety for tests |


## Adding a job variety

A variety is a `Job` subclass plus two syntaxes. Nothing in the core
enumerates types, so this is the whole recipe:

1. Declare `<Type>Config` as an `@dataclass` class subclassing `Config` (or
   `ServerConfig` if it needs a shared HTTP server) -- its fields, types, and
   defaults are the config's contract. The base `Config` supplies dataclass
   `merge` and the `disabled` field for free; note base fields precede yours
   in the init, so default every field and guard requirements at `start`.
2. Subclass `Job`; set `config-class` to your config type (it backs fresh
   construction when a job configures with no base), implement `start`/`stop`,
   and store the trigger key in `__init__`. Read effective config from
   `self.config` by attribute (`self.config.servers`), call `self.callback`
   as the handler. Add variety sub-item methods (like kafka's `on-error`) as
   needed. If `start` spawns a long-lived background task, hand it to
   `watch-task` (`job: self`): a task nobody retrieves the exception from
   dies silently, leaving the job reporting itself healthy while doing
   nothing, and storing its traceback for the life of the process.
3. Define `config/<type>` (construct the dataclass, bind the name, run the
   `&-'default/<type>` guard) with the `()`-sentinel unnamed case. Write the
   slot as `-'default/<type>` wherever it appears, so it is the shared module
   binding and not one private to the expansion.
4. Define `job/<type>` (parse the header, construct the job, configure it --
   `via:` the type default when one exists, fresh otherwise -- and tag-inject
   sub-items), naming the slot the same way.
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
   `disabled` field every config carries, set in the job's `config:` block
   (or on a shared config, dimming every job wired to it).
7. The jobset offers list/enable/disable -- `JobSet`'s control-plane methods,
   exposed over HTTP by the admin job.
