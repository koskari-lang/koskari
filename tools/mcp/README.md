# Koskari corpus MCP server

Maintainer tooling that exposes verified Koskari corpus examples and
documentation to MCP clients (Cursor, Claude Desktop, VS Code, and similar)
via a stdio server implemented in Koskari (`mcp_server.ki`).

The corpus, docs, and programs parquet views are baked into the image at build
time and loaded into memory at startup, so every query runs against those
in-process tables and the server makes no outbound requests. The served
snapshot is identified by its git ref (see the `corpus_version` tool). The
underlying views are the same ones published to
[obriencj/koskari](https://huggingface.co/datasets/obriencj/koskari) (configs
`corpus`, `docs`, and `programs`); the dataset is now a downstream artifact, no
longer on the request path.

Design record: [design/mcp-baked-in-lambda.md](../../design/mcp-baked-in-lambda.md).

## Prerequisites

- `koskari:local` container image (`make build-container`)
- `tox` (to export the corpus parquet views during the build)

## Build

```bash
make build-mcp
```

This stages the corpus parquet views into `tools/mcp/data/` (via `make
mcp-data`, which runs the `regen-corpus` export), then builds and tags the
image `koskari-mcp:local` on top of `koskari:local`. Rebuild whenever the
corpus records change to pick up a fresh snapshot.

## Run

```bash
podman run -i --rm koskari-mcp:local
```

Or:

```bash
make run-mcp
```

**Important:** use `-i` so stdin stays open for MCP JSON-RPC. Do **not** pass
`-t`; a TTY corrupts the protocol stream.

To see what the deployed service serves -- landing page and stateless `/mcp`
on <http://localhost:8080/> -- run the Lambda image locally:

```bash
make build-mcp-lambda && make run-mcp-web
```

It runs the same image AWS runs (the Lambda Web Adapter sits idle outside
Lambda), on the host network so the loopback bind is reachable.

## Serving modes

The same `mcp_server.ki` serves three mutually exclusive transports:

| Mode | Invocation | Use |
|------|------------|-----|
| stdio (default) | `mcp_server.ki` | local MCP clients as a subprocess |
| `--serve` | `mcp_server.ki -- --serve` | self-hosted HTTP behind a reverse proxy |
| `--lambda` | `mcp_server.ki -- --lambda` | stateless HTTP behind the AWS Lambda Web Adapter |

`--lambda` serves stateless JSON on the loopback `PORT` (default 8080) the
adapter proxies to; `--serve` and `--lambda` cannot be combined. The `--`
separates koskari launcher options from script arguments.

Both HTTP modes also answer `GET /` with the landing page (`index.html`,
baked in beside the server): the client configuration, the tool list, and a
terminal that speaks JSON-RPC to `/mcp` from the browser. The page and the
endpoint share an origin, so it needs no CORS allowance. `KOSKARI_CORPUS_PAGE`
overrides the file; if it is missing, the root serves a plain pointer at
`/mcp` instead.

For AWS, `make build-mcp-lambda` builds the Lambda image
(`Containerfile.lambda`, with the Lambda Web Adapter baked in); deploy it with
the SAM stack and workflow under [deploy/](deploy/README.md).

## MCP client configuration

Example Cursor / Claude Desktop entry (use `docker` instead of `podman` if
needed):

```json
{
  "mcpServers": {
    "koskari-corpus": {
      "command": "podman",
      "args": ["run", "-i", "--rm", "koskari-mcp:local"]
    }
  }
}
```

## Tools

| Tool | Description |
|------|-------------|
| `search_docs` | Search documentation pages and example programs by text; optional `kind` (`doc`/`example`) and `limit` |
| `get_reference` | Fetch one documentation page or example program by its path; given a feature path instead, answers with the reference paths that feature's records cite |
| `search_examples` | Search verified corpus examples by text (`search_text`) with optional `feature_path` filter, `polarity` (`valid`/`invalid`) narrowing, and `limit` (default 10, max 100); invalid/wrong-usage records are demoted below valid ones |
| `get_example` | Fetch one corpus record by `corpus_id` |
| `list_features` | List distinct `feature` paths with counts |
| `list_tags` | List searchable tags with counts, grouped into the intent (goal-based) vocabulary and structural tags |
| `corpus_version` | Report the baked snapshot's git ref and per-view row counts |

Tool results are JSON text including `corpus_id`, `feature`, `source`, verify
stage columns when present, and `references`.

Two kinds of path travel between these tools and are not interchangeable.
*Feature paths* (`special/compile/cond`) come from `list_features` and are
accepted only by `search_examples` as `feature_path`, where the match is exact
unless the value ends in `/`, which makes it a prefix over the family.
*Reference paths* (`reference/specials/compile-cond.md`, `examples/…`) are what
`get_reference` takes, and come from `search_docs` hits or from a record's
`references` field. Because a feature path is the likeliest wrong guess at
`get_reference`, it is answered there with that feature's references rather
than a miss, and each empty result names the next call to try.

## Manual smoke test

1. `make build-container && make build-mcp`
2. `podman run -i --rm koskari-mcp:local` — process waits; no stdout before a client connects
3. Connect with an MCP client or inspector using the command above
4. Call `list_features` — non-empty feature list
5. Call `get_example` with `corpus_id` `conditional-cond-single-true-001`
6. Call `search_examples` with `search_text` `cond`

If Koskari bootstrap prints to stdout on startup, MCP clients will desync;
report that as a CLI/bootstrap issue.

## Files

| File | Role |
|------|------|
| `mcp_server.ki` | Koskari MCP server (FastMCP over the baked, in-memory dataset) |
| `index.html` | Landing page served at `GET /` by the HTTP modes |
| `Containerfile` | stdio image on `koskari:local` with `mcp` and `pyarrow`, baking `data/` |
| `Containerfile.lambda` | Lambda image: same, plus the AWS Lambda Web Adapter |
| `data/` | Corpus parquet views staged by `make mcp-data`; build output, gitignored |
| `deploy/` | AWS SAM stack and notes for serving on Lambda |

The server source must not be named `mcp.ki`: Koskari derives the module
name from the filename, which would shadow Python's `mcp` package on import.
