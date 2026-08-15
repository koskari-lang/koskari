---
name: commit
description: Stage and commit current changes following project git conventions. Use only when the user explicitly requests a commit.
disable-model-invocation: true
tools: Bash
---

# Commit

Constraints live in [.claude/rules/git.md](../../rules/git.md). Never push.

## Do not edit the working tree

- Do **not** change, create, delete, rename, or reformat any file before or
  during the commit unless the user **explicitly** asked for those edits in
  the same request.
- Do **not** run formatters, linters, or regen tools as part of committing.
- Stage and commit **only** what is already changed, or what the user
  explicitly named.

If you notice problems in the diff, **describe them** and wait for
instructions. Do not correct them under the guise of committing.

## Steps

1. Inspect repo state:

```bash
git --no-pager status
git --no-pager diff
git --no-pager diff --cached
```

2. Stage the paths the user specified. Use `git add -A` only if they asked to
   commit everything.

3. Write the message from `git --no-pager diff --cached` read at this point,
   not from an earlier reading of the files. The user co-edits the branch, so
   file state drifts.

   - First line: short imperative summary, 72 characters or less.
   - Blank line, then at most one body paragraph wrapped at 72 columns. No
     bullet lists, no per-file walkthrough.
   - The message stands alone for a reader who cannot see this conversation.
     No scratchpad or one-off tooling paths, no plan or phase names.
   - No AI attribution, no `Claude-Session:` trailer, no `Co-authored-by`
     unless the user asks.
   - Do not state that tests pass. Mention tests only when they are red, and
     then mark the commit WIP with a short note.

4. Commit using the heredoc form:

```bash
git commit -F - <<'EOF'
<message here>
EOF
```

5. Stop. Do not push, do not create or delete branches, do not offer to push.
