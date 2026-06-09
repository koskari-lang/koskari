---
name: commit
description: Stage and commit current changes following project git conventions. Use only when the user explicitly requests a commit.
disable-model-invocation: true
tools: Bash
---

# Commit

Follow `.cursor/rules/git.mdc` and `.cursor/rules/gh.mdc` for all constraints.

## Do not edit the working tree

- Do **not** change, create, delete, rename, or reformat any file before or during the commit unless the user **explicitly** asked for those edits in the same request.
- Do **not** run formatters, linters, or regen tools as part of committing.
- Stage and commit **only** what is already changed (or what the user explicitly named).

If you notice problems in the diff, **describe them** and wait for instructions; do not correct them under the guise of committing.

## Steps

1. Inspect repo state:

```bash
git --no-pager status
git --no-pager diff
git --no-pager diff --cached
```

2. Write a commit message **from that diff**:
   - First line: short imperative summary (≤72 characters)
   - Blank line, then optional body word-wrapped at 72 characters
   - No AI attribution lines, no "Co-authored-by" unless user asks

3. Stage paths the user specified (or `git add -A` only if they asked to commit everything).

4. Commit using the heredoc form:

```bash
git commit -F - <<'EOF'
<message here>
EOF
```

5. Do **not** push. Do not create or delete branches.
