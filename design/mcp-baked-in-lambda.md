# Baked-in corpus MCP on AWS Lambda

Design record for migrating the Koskari corpus MCP server off live Hugging
Face Dataset Viewer queries and onto a **dataset baked into the image**, then
running that image as a public, persistent service on **AWS Lambda** at
`corpus.koskari.org`.

This supersedes Track B ("thin shim over the Dataset Viewer API") of
[archive/corpus-mcp-server.md](archive/corpus-mcp-server.md). The stdio
container for local MCP clients is retained; the HF-relay data backend is
removed.

**Status:** Designed, not implemented.

---

## Why reverse the earlier decision

The archived design explicitly rejected baking the parquet into the image, on
the reasoning that a snapshot goes stale and conflicts with a "publish once to
HF, read live" story. That reasoning held for a per-session stdio shim run on a
developer's laptop. It inverts once the server becomes a **public, always-on
service**:

1. **Third-party runtime coupling.** Every tool call currently hits
   `datasets-server.huggingface.co`. A public MCP turns each user query into an
   outbound HF Viewer request, subject to HF's rate limits and uptime. A baked
   dataset makes the service self-contained and its latency deterministic.
2. **Query-surface limits we already hit.** The Viewer `/filter` endpoint
   cannot predicate on list columns, so intent-tag filtering has to be faked in
   Python after fetching a window. Owning the query engine lifts that limit
   (tags, references become first-class filters).
3. **Change control is a feature, not a bug.** For a public service, "the data
   updates only when I deploy" is the desired property. The corpus already
   changes through CI; a deploy step is where versioned data belongs. Every
   exported row carries `git_ref`, so the served snapshot is identifiable.
4. **Scale makes staleness a non-issue.** The full corpus parquet is well under
   1 MB (hundreds of records). Rebuild-and-redeploy on corpus change is cheap
   and fast; there is no download or cache-warming cost at runtime.

Non-goals unchanged from the archived design: no fine-tuned model, the corpus
YAML remains authoritative, and `docs/` does not link here.

---

## Architecture

```text
docs/corpus/records/*.yaml
        │  tools/corpus/export.ki  (--all-views)
        ▼
build/corpus/datasets/
  koskari-corpus/train.parquet     (config: corpus)
  koskari-docs/train.parquet       (config: docs)
  koskari-programs/train.parquet   (config: programs)
        │  COPY into image at build
        ▼
image: koskari runtime + mcp deps + /srv/data/*.parquet + Lambda adapter
        │
        ├── CMD (default): stdio            → local MCP clients (unchanged UX)
        └── --lambda: stateless HTTP :8080  → AWS Lambda Web Adapter
                                                 │
                                                 ▼
                              API Gateway HTTP API  ──  corpus.koskari.org
                                                 │
                                                 ▼
                                        remote MCP clients
```

Two things change in the codebase (data backend + launch mode) and two things
are added around it (Lambda-ready image + AWS/CI wiring). They are independent
and can land in that order.

---

## Part 1 — Replace the HF backend with a baked, in-memory query layer

### The seam

The server already separates fetching from shaping. Everything that operates on
already-fetched row dicts is backend-agnostic and stays as-is:

- `compact-example`, `pick-fields`, `format-example(s)`
- `feature-matches?`, `filter-rows-by-feature`
- `row-invalid?`, `demote-invalid`, `filter-by-polarity`
- the `search_examples` / `search_docs` orchestration bodies
- the tool signatures and `INSTRUCTIONS`

Only the fetch layer and its SQL-clause helpers are replaced. Delete or rewrite:

- `viewer-base-params`, `viewer-get`, `viewer-rows`, `viewer-search-rows`
- `escape-sql-string`, `doc-like-clause`, `search-like-clause`,
  `feature-where-clause`
- `fts-search`, `like-search`, `feature-search`, `search-docs-config`
- `resolve-references` (reimplement against the in-memory docs table)
- the `/rows` paging loops inside `list_features` and `list_tags`
- the `import httpx` and the `http` client

### The replacement

At startup, load the three parquet files into in-memory lists of row dicts,
one table per config:

```text
CORPUS   = load-parquet  /srv/data/koskari-corpus/train.parquet
DOCS     = load-parquet  /srv/data/koskari-docs/train.parquet
PROGRAMS = load-parquet  /srv/data/koskari-programs/train.parquet
```

Loading uses `pyarrow.parquet` (`read_table().to_pylist()`) -- already a build
dependency of the export pipeline, and the only new runtime dep. Data path is
configurable via `KOSKARI_CORPUS_DATA` (default `/srv/data`).

Reimplement the fetch functions as pure in-memory operations over those lists:

| Old (HF)                         | New (in-memory)                                        |
|----------------------------------|--------------------------------------------------------|
| `/search` full-text              | tokenize query; score rows by token overlap over the string columns; sort by score |
| `/filter` `LIKE '%x%'`           | Python substring test over the same columns            |
| `/filter` `feature = / LIKE`     | reuse existing `feature-matches?` predicate            |
| `/filter` `corpus_id =`          | linear scan for the id                                 |
| `/filter` `path =`               | linear scan of the docs/programs table                 |
| `/rows` paging in list_*         | iterate the whole table (no paging needed)             |

At a few hundred rows a linear scan is sub-millisecond; no index is warranted.
This is also where the intent-tag limitation disappears -- filtering on the
`tags` list column is a plain `in?` test.

**Ranking note.** The current `/search` gives HF's full-text ranking, with a
substring fallback for keywords the FTS index drops as stop words. The
in-memory scorer must reproduce the observable contract the tests and corpus
consumers rely on: content-word matches rank first, and stop-word-only queries
(`while`, `for-each`, `if`, `and`, `or`, `not`) still resolve via substring.
Keep `demote-invalid` and `filter-by-polarity` exactly as they are -- they run
after ranking and are unaffected.

**Escalation path (not now).** If the corpus grows to where linear scans or
naive scoring degrade, swap the in-memory layer for DuckDB over the same
parquet (`match_bm25` via the `fts` extension, `list_contains` for tags)
without changing any tool signature. Deferred because it adds a native
dependency with musl-wheel risk on the Alpine base, for no benefit at current
scale.

### Provenance surface

Since the snapshot is now fixed at build time, expose which snapshot is
serving. The rows already carry `git_ref`; read it once at load and log it at
startup, and optionally return it from a lightweight `corpus_version` tool or
health response so a running deployment is identifiable.

---

## Part 2 — Add a Lambda serving mode to the script

The script has three launch modes, and `--serve` and `--lambda` are **mutually
exclusive**:

| Mode          | Transport                        | Use                                    |
|---------------|----------------------------------|----------------------------------------|
| (no flag)     | stdio                            | local MCP clients as a subprocess      |
| `--serve`     | streamable HTTP (stateful, SSE)  | self-hosted VM/service behind a proxy  |
| `--lambda`    | streamable HTTP (stateless JSON) | behind the AWS Lambda Web Adapter      |

`--serve` is unchanged: normal streamable HTTP with its existing host, port, and
allowed-hosts knobs, driving `mcp.run_streamable_http_async`. `--lambda` is a
distinct mode, not a preset of `--serve`; passing both is an argument error.

### `--lambda`

FastMCP's default streamable-HTTP transport keeps per-client session state in
process memory, keyed by `Mcp-Session-Id`. Under Lambda, consecutive requests
from one client can land on different execution environments, so session state
must not live in the process. Lambda mode therefore serves **stateless**, with
plain JSON responses rather than an SSE stream, before serving:

```text
mcp.settings.stateless_http = True
mcp.settings.json_response  = True
mcp.settings.host           = "127.0.0.1"
mcp.settings.port           = int(getenv("PORT", "8080"))
```

It binds the loopback port the Lambda Web Adapter proxies to. No allowed-hosts
override is needed: the adapter forwards from API Gateway on localhost, and host
protection is enforced at the API Gateway custom domain. Each request is
self-contained, which is already true of this server's tools.

The stdio default and the `--serve` public-HTTP path are untouched. The same
`.ki` file serves all three deployment shapes through mutually exclusive modes.

---

## Part 3 — Lambda-ready image

### Why the Lambda Web Adapter (LWA)

The server is a Koskari program running on the koskari runtime; it cannot be a
stock Lambda Python handler. Two ways to run it on Lambda:

- **Mangum handler** -- adapt the ASGI app to the Lambda handler signature.
  Requires exposing a module-level handler and threading Starlette internals
  through Koskari. Awkward in `.ki`.
- **Lambda Web Adapter** -- a Lambda extension (a static binary copied into the
  image) that translates API Gateway / Function URL events into ordinary HTTP
  requests against a local port. The app just serves HTTP. **Chosen**: it needs
  no handler plumbing, reuses the existing `--serve` path verbatim, and keeps
  the app portable to Fargate or a VM unchanged.

### Containerfile changes

Extend `tools/mcp/Containerfile` (a second stage or a parallel
`Containerfile.lambda`, so the stdio image stays minimal):

```dockerfile
FROM koskari:local

# Lambda Web Adapter as a Lambda extension.
COPY --from=public.ecr.aws/awsguru/aws-lambda-adapter:0.9.1 \
     /lambda-adapter /opt/extensions/lambda-adapter

RUN pip install --no-cache-dir --disable-pip-version-check \
    --root-user-action=ignore \
    mcp pyarrow          # httpx dropped; pyarrow added

WORKDIR /srv
COPY mcp_server.ki ./mcp_server.ki
COPY data/ ./data/       # baked parquet: corpus/docs/programs train.parquet

ENV PORT=8080 AWS_LWA_INVOKE_MODE=buffered
CMD ["mcp_server.ki", "--lambda"]
```

Notes:

- `AWS_LWA_INVOKE_MODE=buffered` suits the stateless JSON responses (small,
  sub-second). Switch to `response_stream` only if streaming responses are
  added later, which also requires a Function URL rather than API Gateway.
- The parquet in `data/` is produced by `tox -qe regen-corpus -- --all-views`
  (writes `build/corpus/datasets/<view>/train.parquet`) and copied into
  `tools/mcp/data/` before the build. `data/` is build output -- gitignore it.
- **Base-image check:** LWA and `pyarrow` must resolve on the koskari base
  arch. `koskari:local` is Alpine/musl; `pyarrow` publishes musllinux wheels,
  and LWA ships per-arch static binaries, so both should install. Verify on
  first build; if a wheel is missing for the target arch, either add a builder
  stage or base the Lambda image on a glibc koskari build. Build the image for
  the architecture the Lambda function is configured with (`x86_64` or
  `arm64`); arm64 is cheaper per ms and a fine default.

---

## Part 4 — Standing it up on AWS (first-time walkthrough)

Target: an HTTPS endpoint at `https://corpus.koskari.org/mcp` backed by the Lambda
image, with basic throttling. Recommended shape:

```text
ECR (image)  →  Lambda (container, arm64)  →  API Gateway HTTP API  →  corpus.koskari.org
```

API Gateway HTTP API is chosen over a raw Function URL because it gives a
**custom domain and stage throttling directly**, without needing CloudFront in
front. (Trade-off: per-IP rate limiting is not native to HTTP APIs -- see
"Rate limiting" below.)

### One-time setup

1. **ECR repository.** Create a private repo, e.g. `koskari-mcp`. This holds the
   Lambda image. `aws ecr create-repository --repository-name koskari-mcp`.
2. **IAM execution role** for the function: attach
   `AWSLambdaBasicExecutionRole` (CloudWatch Logs only -- the function needs no
   other AWS access, since its data is baked in).
3. **Push the first image** by hand to prove the pipeline (CI automates it
   later): `docker build`, `aws ecr get-login-password | docker login`,
   `docker push`.
4. **Create the Lambda function** from the image: package type `Image`, the
   ECR image URI, the execution role, architecture `arm64`, memory **512 MB**
   (parquet is tiny; more memory buys faster cold-start CPU), timeout **30 s**.
5. **API Gateway HTTP API.** Create an HTTP API with a single `$default` route
   (`ANY /{proxy+}`) integrated to the Lambda (payload format 2.0). Enable a
   default stage with auto-deploy. Note the invoke URL and smoke-test
   `POST /mcp` with an MCP `initialize` request.
6. **Custom domain `corpus.koskari.org`.**
   - Request an **ACM certificate** for `corpus.koskari.org` in the API's region.
     Validate by adding the ACM-provided CNAME to koskari.org DNS (any DNS
     provider works; Route 53 is not required).
   - Create an API Gateway **custom domain name** `corpus.koskari.org` using that
     cert; map it to the HTTP API's stage.
   - At the DNS provider, add a **CNAME** `corpus.koskari.org` → the API Gateway
     regional domain target. (If koskari.org is in Route 53, an alias A record
     instead.)
7. **Verify** end-to-end against `https://corpus.koskari.org/mcp`, then register it
   in an MCP client (`"type": "http", "url": "https://corpus.koskari.org/mcp"`).

### Cold start and warmth

Container-image Lambdas have a slower first-invoke (image init + koskari
bootstrap + parquet load), on the order of a couple of seconds. For an
informational tool this is usually acceptable. If it is not:

- **Provisioned concurrency = 1** keeps one environment warm for a small,
  predictable monthly cost. (Lambda SnapStart is not available for container
  images, so provisioned concurrency is the lever.)
- Higher memory shortens cold start by giving more CPU during init.

### Rate limiting

- **Baseline (do first):** HTTP API stage-level throttling -- set a modest
  steady rate and burst (for example 20 rps / burst 40). This caps cost and
  crude abuse globally and needs no extra service.
- **Reserved concurrency** on the function caps blast radius independently of
  the gateway.
- **Per-IP limiting (upgrade):** AWS WAF does not attach to HTTP APIs directly.
  To get per-IP rate-based rules, either front the function with **CloudFront +
  WAF** (also enables response streaming and edge caching), or switch the
  gateway to a **REST API**, which supports WAF and usage-plan API keys. Defer
  until traffic justifies it; global throttling covers the common case for an
  unauthenticated informational endpoint.

### Cost shape

At informational traffic this sits near the Lambda and API Gateway free/low
tiers: pay-per-request, scales to zero, no idle charge. The only standing costs
are ECR image storage (cents) and optional provisioned concurrency if enabled.
This is the cheapest option that still autoscales, which was the original
requirement; Fargate's ~$25/mo always-on floor is the fallback only if a
persistent-connection need appears later.

---

## Part 5 — CI/CD

### This server

Add a GitHub Actions workflow (sibling to `publish_corpus_dataset.yml`) that,
on change to the corpus records or the MCP server, redeploys:

1. Export parquet: `tox -qe regen-corpus -- --all-views`.
2. Stage it into the build context (`tools/mcp/data/`).
3. Build the Lambda image for `arm64`.
4. Authenticate to AWS via **GitHub OIDC** (`permissions: id-token: write`)
   assuming a deploy role -- no long-lived AWS keys, mirroring the existing
   HF Trusted-Publishers OIDC pattern.
5. Push to ECR.
6. `aws lambda update-function-code --function-name koskari-mcp
   --image-uri <ecr>@<digest>` and wait for the update to settle.

Trigger on push to the default branch under the relevant paths, plus
`workflow_dispatch` for manual redeploys. Because the data is baked, a corpus
change and a code change take the identical path -- both are just a new image.

### Generalizing to future MCP servers

The stated intent is more than one MCP server over time. Make the AWS side
**parameterized infrastructure-as-code** rather than click-ops:

- Define the per-server stack (ECR repo + Lambda + HTTP API route + custom
  domain mapping) once as an **AWS SAM** template or Terraform module taking
  `(name, image, subdomain)`. SAM is the gentler on-ramp for a first Lambda
  project and expresses function + HTTP API + domain in one template.
- A **single API Gateway** with per-server base-path mappings
  (`corpus.koskari.org/koskari-corpus`, `/next-server`, ...) keeps one throttling
  and TLS surface for every server, or give each its own subdomain -- both are
  a parameter, not a rewrite.
- The GitHub workflow becomes a reusable job keyed on server name, so adding a
  server is "instantiate the module + add a matrix entry."

---

## Testing and verification

- **Backend parity:** the in-memory query functions are pure and deterministic
  over the baked tables, so they are unit-testable without a network. Add tests
  that pin ranking order and the stop-word substring fallback, and that
  tag/feature/polarity filtering returns the same rows the HF path did for a
  set of representative queries.
- **Stdio smoke:** `make run-mcp` (default CMD unchanged) still serves stdio.
- **Lambda-local smoke:** run the Lambda image locally with the adapter's local
  runtime (or just `--lambda`) and drive it with an MCP client over
  HTTP before the first cloud deploy.
- Per project rules, defect-style tests assert correct behavior and stay red
  until fixed -- no pinning of any known-wrong HF-era output.

---

## Rollout order

1. Land Part 1 (baked in-memory backend) behind the existing stdio and
   `--serve` modes; the local container UX is unchanged, just offline.
2. Land Part 2 (`--lambda`) and Part 3 (Lambda image +
   Containerfile), verified locally.
3. Do the one-time AWS bring-up (Part 4) by hand to understand the moving
   parts, endpoint live at `corpus.koskari.org`.
4. Codify it: SAM/Terraform + the GitHub deploy workflow (Part 5), then tear
   down and re-create from IaC to prove reproducibility.
5. Retire the HF Viewer relay from the server and update
   `tools/mcp/README.md`; keep the HF dataset publication (Track A) as the
   public dataset artifact, no longer on the request path.

---

## Open questions and risks

- **Alpine wheels.** Confirm `pyarrow` (musllinux) and the LWA binary resolve on
  the `koskari:local` base for the chosen arch; else add a builder stage or use
  a glibc koskari base for the Lambda image only.
- **Ranking fidelity.** The in-memory scorer approximates HF full-text ranking.
  Decide the acceptable contract via tests before removing the HF path; a
  simple token-overlap score is expected to suffice at this corpus size.
- **Cold-start tolerance.** Measure real cold-start latency before deciding
  whether provisioned concurrency is worth its standing cost.
- **Region choice.** Pick one region for Lambda + ACM + API Gateway (the
  regional custom-domain cert must be in that region; a CloudFront upgrade for
  WAF would additionally need a us-east-1 cert).

---

## References

- [archive/corpus-mcp-server.md](archive/corpus-mcp-server.md) -- superseded
  Track B (HF Viewer relay) and the still-current Track A (HF publication)
- [corpus-schema-philosophy.md](corpus-schema-philosophy.md) -- corpus as
  source of truth, datasets as views
- `tools/mcp/mcp_server.ki` -- the server being migrated
- `tools/corpus/export.ki`, `tools/corpus/views/` -- parquet export and view
  registry (`koskari-corpus`, `koskari-docs`, `koskari-programs`)
- `.github/workflows/publish_corpus_dataset.yml` -- existing OIDC publish
  workflow, the CI/CD pattern to mirror
- AWS Lambda Web Adapter (`public.ecr.aws/awsguru/aws-lambda-adapter`)
- AWS SAM -- Lambda + HTTP API + custom domain as one template

# The end.
</content>
</invoke>
