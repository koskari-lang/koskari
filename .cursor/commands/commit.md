# Commit (explicit user request only)

Follow `.cursor/rules/git.mdc` for git constraints and message format.

## Do not edit the working tree

When the user asks you to commit:

- **Do not** change, create, delete, rename, or reformat any file before or during the commit unless the user **explicitly** asked for those edits in the same request (or a separate explicit instruction).
- **Do not** “fix” wording, imports, tests, or docs to “match” the commit.
- **Do not** run formatters, linters, or regen tools as part of committing unless the user asked for that explicitly.
- Stage and commit **only** what is already changed in the repository (or what the user told you to stage).

If you notice problems in the diff, **describe them** and wait for instructions; do not correct them under the guise of committing.

## What to do

1. Show or rely on the full diff (`git --no-pager diff` and/or status) as required by `.cursor/rules/git.mdc`.
2. Write the commit message **from that diff** (brief summary; body word-wrapped at 72 characters per `.cursor/rules/git.mdc`).
3. `git add` only as needed for the paths the user asked to commit (typically `git add -A` only when they asked to commit everything).
4. Invoke `git commit` via `git commit -F - <<'EOF' ... EOF` per `.cursor/rules/git.mdc`.
