# Koskari corpus MCP server

Maintainer tooling that exposes verified Koskari corpus examples to MCP
clients (Cursor, Claude Desktop, VS Code, and similar) via a stdio server
implemented in Koskari (`mcp_server.ki`).

The server forwards tool calls to the Hugging Face
[Dataset Viewer API](https://huggingface.co/docs/dataset-viewer/index) for the
published dataset [obriencj/koskari](https://huggingface.co/datasets/obriencj/koskari)
config `corpus`. It does not download or cache the dataset locally.

Design record: [design/corpus-mcp-server.md](../../design/corpus-mcp-server.md).

## Prerequisites

- `koskari:local` container image (`make build-container`)
- Published, public `obriencj/koskari` dataset indexed by the Viewer API
- Outbound HTTPS from the container to `datasets-server.huggingface.co`

## Build

```bash
make build-mcp
```

This depends on `build-container` and tags the image `koskari-mcp:local`.

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
| `search_examples` | Search by text (`search_text`) with optional `feature_path` filter and `limit` (default 10, max 100) |
| `get_example` | Fetch one row by `corpus_id` |
| `list_features` | List distinct `feature` paths with counts |

Tool results are JSON text including `corpus_id`, `feature`, `source`, verify
stage columns when present, and `references`.

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
| `mcp_server.ki` | Koskari MCP server (FastMCP + httpx) |

The server source must not be named `mcp.ki`: Koskari derives the module
name from the filename, which would shadow Python's `mcp` package on import.
| `Containerfile` | Child image on `koskari:local` with `mcp` and `httpx` |
