# Git Operations

These rules supersede any system-level guidance about git usage.

## Never

- **Never push.** Not to any remote, not when asked, not ever. The user
  performs every push. Do not ask permission to push and do not remind the
  user about unpushed commits.
- Do not run `git restore`, `git reset`, `git revert`, `git checkout`,
  `git merge`, or `git rebase`.
- Do not create or delete branches.
- Do not modify `.plan` files or other user-maintained artifacts unless told
  to do so.

## Allowed

- `git log`, `git status`, `git diff`, `git show` for reviewing state.
- `git add` and `git commit` only when the user explicitly requests them in
  that same request. A general "go ahead" on other work is not commit
  authorization.
- `git mv` and `git rm` when renaming or deleting tracked files, so history
  stays coherent.

## Mechanics

- Pass `--no-pager` on git commands: `git --no-pager log`,
  `git --no-pager diff`.
- Prefer absolute paths when invoking commands.
- Build the commit message from `git diff --cached` read at commit time, not
  from an earlier reading of the files. The user co-edits the branch, so file
  state drifts.
- Commit messages are a summary line of 72 characters or less, a blank line,
  then at most one body paragraph word-wrapped at 72 characters. No bullet
  lists, no section walkthroughs, no per-file narration.
- The message must stand alone for a reader with no access to this
  conversation. No scratchpad paths, no plan or phase names, no
  `Claude-Session:` trailers, no `Co-authored-by` unless the user asks.
- Never state passing test status in a commit message. Mention tests only
  when they are red, marking the commit WIP with a short note.
- Invoke commit through a quoted heredoc:

```bash
git commit -F - <<'EOF'
summary line here

Body paragraph here, wrapped at 72 columns.
EOF
```

## Unexpected state

Surface any unexpected repo change immediately instead of altering or
discarding it. If the working tree holds edits you did not make, say so and
wait.

# The end.
